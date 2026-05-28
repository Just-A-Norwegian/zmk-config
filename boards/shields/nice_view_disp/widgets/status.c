/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include "status.h"
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <zmk/hid.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/position_state_changed.h>

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

LV_IMG_DECLARE(Shift);
LV_IMG_DECLARE(Ctrl);
LV_IMG_DECLARE(Alt);
LV_IMG_DECLARE(Gui);
LV_IMG_DECLARE(Caps_word);
LV_IMG_DECLARE(Cap_lock);

struct output_status_state
{
    struct zmk_endpoint_instance selected_endpoint;
    int active_profile_index;
    bool active_profile_connected;
    bool active_profile_bonded;
};

struct layer_status_state
{
    zmk_keymap_layer_index_t index;
    const char *label;
};

struct modifier_status_state
{
    uint8_t mods;
    bool caps_word_active;
    bool caps_lock_active;
};

/* Left magic-shift key is at thumb position 40 in corne_choc_pro.keymap. */
#define MAGIC_SHIFT_POSITION 40

static bool is_keyboard_alpha(uint32_t keycode)
{
    return (keycode >= 0x04 && keycode <= 0x1D);
}

static bool is_keyboard_modifier(uint32_t keycode)
{
    return (keycode >= 0xE0 && keycode <= 0xE7);
}

static bool caps_word_should_continue_compat(const struct zmk_keycode_state_changed *ev)
{
    if (ev->usage_page != 0x07)
    {
        return false;
    }

    /* Keep active only across alpha keys (plus held modifiers). */
    if (is_keyboard_alpha(ev->keycode) || is_keyboard_modifier(ev->keycode))
    {
        return true;
    }

    return false;
}

static void draw_top(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state)
{
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_draw_rect_dsc_t rect_white_dsc;
    init_rect_dsc(&rect_white_dsc, LVGL_FOREGROUND);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    // Draw battery
    draw_battery(canvas, state);

    // Draw output status
    char output_text[10] = {};

    switch (state->selected_endpoint.transport)
    {
    case ZMK_TRANSPORT_USB:
        strcat(output_text, LV_SYMBOL_USB);
        break;
    case ZMK_TRANSPORT_BLE:
        if (state->active_profile_bonded)
        {
            if (state->active_profile_connected)
            {
                strcat(output_text, LV_SYMBOL_WIFI);
            }
            else
            {
                strcat(output_text, LV_SYMBOL_CLOSE);
            }
        }
        else
        {
            strcat(output_text, LV_SYMBOL_SETTINGS);
        }
        break;
    }

    lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc, output_text);

    // Draw modifier keys
    static uint8_t active_sym_buf[50];
    static const lv_img_dsc_t *mod_syms[4] = {&Shift, &Ctrl, &Alt, &Gui};
    static const int mod_cx[4] = {0, 34, 0, 34};
    static const int mod_cy[4] = {21, 21, 42, 42};

    lv_draw_img_dsc_t img_dsc;
    lv_draw_img_dsc_init(&img_dsc);

    /* Draw Shift cell (index 0) with three states: caps-lock, caps-word, or normal shift */
    {
        const lv_img_dsc_t *sym;
        bool highlight;
        if (state->caps_lock_active)
        {
            sym = &Cap_lock;
            highlight = true;
        }
        else if (state->caps_word_active)
        {
            sym = &Caps_word;
            highlight = true;
        }
        else
        {
            sym = &Shift;
            highlight = (state->mods >> 0) & 1;
        }
        if (highlight)
        {
            lv_canvas_draw_rect(canvas, mod_cx[0], mod_cy[0], 34, 21, &rect_white_dsc);
            memcpy(active_sym_buf, sym->data + 4, 4);
            memcpy(active_sym_buf + 4, sym->data, 4);
            memcpy(active_sym_buf + 8, sym->data + 8, 42);
            lv_img_dsc_t inv = *sym;
            inv.data = active_sym_buf;
            lv_canvas_draw_img(canvas, mod_cx[0] + 5, mod_cy[0] + 4, &inv, &img_dsc);
        }
        else
        {
            lv_canvas_draw_img(canvas, mod_cx[0] + 5, mod_cy[0] + 4, sym, &img_dsc);
        }
    }

    /* Draw Ctrl, Alt, GUI cells (indices 1-3) with standard active/inactive display */
    for (int i = 1; i < 4; i++)
    {
        bool active = (state->mods >> i) & 1;
        int cx = mod_cx[i];
        int cy = mod_cy[i];

        if (active)
        {
            lv_canvas_draw_rect(canvas, cx, cy, 34, 21, &rect_white_dsc);
            memcpy(active_sym_buf, mod_syms[i]->data + 4, 4);
            memcpy(active_sym_buf + 4, mod_syms[i]->data, 4);
            memcpy(active_sym_buf + 8, mod_syms[i]->data + 8, 42);
            lv_img_dsc_t inv = *mod_syms[i];
            inv.data = active_sym_buf;
            lv_canvas_draw_img(canvas, cx + 5, cy + 4, &inv, &img_dsc);
        }
        else
        {
            lv_canvas_draw_img(canvas, cx + 5, cy + 4, mod_syms[i], &img_dsc);
        }
    }

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

static void draw_middle(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state)
{
    lv_obj_t *canvas = lv_obj_get_child(widget, 1);

    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_draw_rect_dsc_t rect_white_dsc;
    init_rect_dsc(&rect_white_dsc, LVGL_FOREGROUND);
    lv_draw_arc_dsc_t arc_dsc;
    init_arc_dsc(&arc_dsc, LVGL_FOREGROUND, 2);
    lv_draw_arc_dsc_t arc_dsc_filled;
    init_arc_dsc(&arc_dsc_filled, LVGL_FOREGROUND, 9);
    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);
    lv_draw_label_dsc_t label_dsc_black;
    init_label_dsc(&label_dsc_black, LVGL_BACKGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    // Draw circles
    int circle_offsets[5][2] = {
        {13, 13},
        {55, 13},
        {34, 34},
        {13, 55},
        {55, 55},
    };

    for (int i = 0; i < 5; i++)
    {
        bool selected = i == state->active_profile_index;

        lv_canvas_draw_arc(canvas, circle_offsets[i][0], circle_offsets[i][1], 13, 0, 360, &arc_dsc);

        if (selected)
        {
            lv_canvas_draw_arc(canvas, circle_offsets[i][0], circle_offsets[i][1], 9, 0, 359, &arc_dsc_filled);
        }

        char label[2];
        snprintf(label, sizeof(label), "%d", i + 1);
        lv_canvas_draw_text(canvas, circle_offsets[i][0] - 8, circle_offsets[i][1] - 10, 16,
                            (selected ? &label_dsc_black : &label_dsc), label);
    }

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

static void draw_bottom(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state)
{
    lv_obj_t *canvas = lv_obj_get_child(widget, 2);

    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    // Draw layer
    if (state->layer_label == NULL || strlen(state->layer_label) == 0)
    {
        char text[10] = {};

        sprintf(text, "LAYER %i", state->layer_index);

        lv_canvas_draw_text(canvas, 0, 5, 68, &label_dsc, text);
    }
    else
    {
        lv_canvas_draw_text(canvas, 0, 5, 68, &label_dsc, state->layer_label);
    }

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state)
{
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

    widget->state.battery = state.level;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void battery_status_update_cb(struct battery_status_state state)
{
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_status(widget, state); }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh)
{
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);

    return (struct battery_status_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

static void set_output_status(struct zmk_widget_status *widget,
                              const struct output_status_state *state)
{
    widget->state.selected_endpoint = state->selected_endpoint;
    widget->state.active_profile_index = state->active_profile_index;
    widget->state.active_profile_connected = state->active_profile_connected;
    widget->state.active_profile_bonded = state->active_profile_bonded;

    draw_top(widget->obj, widget->cbuf, &widget->state);
    draw_middle(widget->obj, widget->cbuf2, &widget->state);
}

static void output_status_update_cb(struct output_status_state state)
{
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_output_status(widget, &state); }
}

static struct output_status_state output_status_get_state(const zmk_event_t *_eh)
{
    return (struct output_status_state){
        .selected_endpoint = zmk_endpoints_selected(),
        .active_profile_index = zmk_ble_active_profile_index(),
        .active_profile_connected = zmk_ble_active_profile_is_connected(),
        .active_profile_bonded = !zmk_ble_active_profile_is_open(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_output_status, struct output_status_state,
                            output_status_update_cb, output_status_get_state)
ZMK_SUBSCRIPTION(widget_output_status, zmk_endpoint_changed);

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_output_status, zmk_usb_conn_state_changed);
#endif
#if defined(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(widget_output_status, zmk_ble_active_profile_changed);
#endif

static void set_layer_status(struct zmk_widget_status *widget, struct layer_status_state state)
{
    widget->state.layer_index = state.index;
    widget->state.layer_label = state.label;

    draw_bottom(widget->obj, widget->cbuf3, &widget->state);
}

static void layer_status_update_cb(struct layer_status_state state)
{
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_layer_status(widget, state); }
}

static struct layer_status_state layer_status_get_state(const zmk_event_t *eh)
{
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    return (struct layer_status_state){
        .index = index, .label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index))};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer_status, struct layer_status_state, layer_status_update_cb,
                            layer_status_get_state)

ZMK_SUBSCRIPTION(widget_layer_status, zmk_layer_state_changed);

static void set_modifier_status(struct zmk_widget_status *widget,
                                struct modifier_status_state state)
{
    widget->state.mods = state.mods;
    widget->state.caps_word_active = state.caps_word_active;
    widget->state.caps_lock_active = state.caps_lock_active;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void modifier_status_update_cb(struct modifier_status_state state)
{
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node)
    {
        set_modifier_status(widget, state);
    }
}

static struct modifier_status_state modifier_status_get_state(const zmk_event_t *eh)
{
    zmk_mod_flags_t m = zmk_hid_get_explicit_mods();

    static bool caps_word_active = false;
    static bool caps_lock_active = false;
    static int64_t last_magic_shift_press_time = 0;
    static int magic_shift_press_count = 0;

    const struct zmk_keycode_state_changed *kc_ev = as_zmk_keycode_state_changed(eh);
    const struct zmk_position_state_changed *pos_ev = as_zmk_position_state_changed(eh);

    /* Track magic shift position presses to detect single vs double tap. */
    if (pos_ev != NULL && pos_ev->position == MAGIC_SHIFT_POSITION && pos_ev->state)
    {
        /* Reset press counter if more than 250ms since last press (outside tap-dance window). */
        if (pos_ev->timestamp - last_magic_shift_press_time > 250)
        {
            magic_shift_press_count = 0;
        }

        last_magic_shift_press_time = pos_ev->timestamp;
        magic_shift_press_count++;

        /* First press = single tap = activate caps-word immediately. */
        if (magic_shift_press_count == 1)
        {
            caps_word_active = true;
            caps_lock_active = false;
        }
        /* Second press = double tap = toggle caps-lock. */
        else if (magic_shift_press_count == 2)
        {
            caps_lock_active = !caps_lock_active;
            caps_word_active = false;
        }
    }

    /* Deactivate caps-word when a non-alpha key is pressed. */
    if (kc_ev != NULL && kc_ev->state && caps_word_active && !caps_word_should_continue_compat(kc_ev))
    {
        caps_word_active = false;
    }

    return (struct modifier_status_state){
        .mods = (!!(m & (BIT(1) | BIT(5)))) << 0 |
                (!!(m & (BIT(0) | BIT(4)))) << 1 |
                (!!(m & (BIT(2) | BIT(6)))) << 2 |
                (!!(m & (BIT(3) | BIT(7)))) << 3,
        .caps_word_active = caps_word_active,
        .caps_lock_active = caps_lock_active,
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_modifier_status, struct modifier_status_state,
                            modifier_status_update_cb, modifier_status_get_state)

ZMK_SUBSCRIPTION(widget_modifier_status, zmk_keycode_state_changed);
ZMK_SUBSCRIPTION(widget_modifier_status, zmk_position_state_changed);

int top_pos = 92;
int middle_pos = 24;
int bottom_pos = -44;

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent)
{
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);
    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_LEFT, top_pos, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);
    lv_obj_t *middle = lv_canvas_create(widget->obj);
    lv_obj_align(middle, LV_ALIGN_TOP_LEFT, middle_pos, 0);
    lv_canvas_set_buffer(middle, widget->cbuf2, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);
    lv_obj_t *bottom = lv_canvas_create(widget->obj);
    lv_obj_align(bottom, LV_ALIGN_TOP_LEFT, bottom_pos, 0);
    lv_canvas_set_buffer(bottom, widget->cbuf3, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_output_status_init();
    widget_layer_status_init();
    widget_modifier_status_init();

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
