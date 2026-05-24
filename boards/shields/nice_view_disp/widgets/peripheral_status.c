/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>
#include <zmk/events/position_state_changed.h>

#include "peripheral_status.h"

LV_IMG_DECLARE(balloon);
LV_IMG_DECLARE(mountain);
LV_IMG_DECLARE(black);
LV_IMG_DECLARE(blonde);
LV_IMG_DECLARE(boy1);
LV_IMG_DECLARE(boy2);
LV_IMG_DECLARE(bush);
LV_IMG_DECLARE(crete);
LV_IMG_DECLARE(ginger);
LV_IMG_DECLARE(luigi);
LV_IMG_DECLARE(mask);
LV_IMG_DECLARE(nurse);
LV_IMG_DECLARE(pink);
LV_IMG_DECLARE(shower);
LV_IMG_DECLARE(skinny);
LV_IMG_DECLARE(spread);
LV_IMG_DECLARE(squirtle);
LV_IMG_DECLARE(toy);

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct peripheral_status_state {
    bool connected;
};

static void draw_top(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    // Draw battery
    draw_battery(canvas, state);

    // Draw output status
    lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc,
                        state->connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

    widget->state.battery = state.level;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_status(widget, state); }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    return (struct battery_status_state){
        .level = zmk_battery_state_of_charge(),
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

static struct peripheral_status_state get_state(const zmk_event_t *_eh) {
    return (struct peripheral_status_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

static void set_connection_status(struct zmk_widget_status *widget,
                                  struct peripheral_status_state state) {
    widget->state.connected = state.connected;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void output_status_update_cb(struct peripheral_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_connection_status(widget, state); }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status, struct peripheral_status_state,
                            output_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed);

typedef struct {
    const lv_img_dsc_t **images;
    uint8_t              count;
} art_album_t;

static const lv_img_dsc_t *album_sfw[]  = { &balloon, &mountain };
static const lv_img_dsc_t *album_nsfw[] = {
    &black, &blonde, &boy1, &boy2, &bush, &crete, &ginger, &luigi,
    &mask, &nurse, &pink, &shower, &skinny, &spread, &squirtle, &toy
};
static const art_album_t albums[] = {
    { album_sfw,  ARRAY_SIZE(album_sfw)  },
    { album_nsfw, ARRAY_SIZE(album_nsfw) },
};
#define ALBUM_COUNT  ARRAY_SIZE(albums)
#define ART_CYCLE_MS 30000

static uint8_t     album_idx  = 0;
static uint8_t     image_idx  = 0;
static bool        auto_loop  = true;
static lv_timer_t *loop_timer = NULL;
static lv_obj_t   *art_obj    = NULL;

static void show_current_cb(void *_) {
    lv_img_set_src(art_obj, albums[album_idx].images[image_idx]);
}
static void show_current(void) { lv_async_call(show_current_cb, NULL); }

static void art_timer_cb(lv_timer_t *timer) {
    image_idx = (image_idx + 1) % albums[album_idx].count;
    lv_img_set_src(art_obj, albums[album_idx].images[image_idx]);
}

static void art_next_image(void) {
    image_idx = (image_idx + 1) % albums[album_idx].count;
    show_current();
}
static void art_next_album(void) {
    album_idx = (album_idx + 1) % ALBUM_COUNT;
    image_idx = 0;
    show_current();
}
static void art_toggle_loop(void) {
    auto_loop = !auto_loop;
    if (auto_loop) lv_timer_resume(loop_timer);
    else           lv_timer_pause(loop_timer);
}

#define ART_KEY_NEXT_IMAGE   13
#define ART_KEY_TOGGLE_LOOP  27
#define ART_KEY_NEXT_ALBUM   39

static int art_key_handler(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (!ev || !ev->state) return ZMK_EV_EVENT_BUBBLE;
    switch (ev->position) {
        case ART_KEY_NEXT_IMAGE:  art_next_image();  break;
        case ART_KEY_TOGGLE_LOOP: art_toggle_loop(); break;
        case ART_KEY_NEXT_ALBUM:  art_next_album();  break;
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(art_key_ctrl, art_key_handler);
ZMK_SUBSCRIPTION(art_key_ctrl, zmk_position_state_changed);

int art_pos = 0;
int top_pos = 92;

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);
    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_LEFT, top_pos, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    image_idx = sys_rand32_get() % albums[album_idx].count;
    art_obj = lv_img_create(widget->obj);
    lv_img_set_src(art_obj, albums[album_idx].images[image_idx]);
    lv_obj_align(art_obj, LV_ALIGN_TOP_LEFT, art_pos, 0);
    loop_timer = lv_timer_create(art_timer_cb, ART_CYCLE_MS, NULL);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_peripheral_status_init();

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
