#include <errno.h>

#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/magic_mode_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keys.h>
#include <dt-bindings/zmk/modifiers.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <zmk/magic_mode.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define MAGIC_SHIFT_POSITION 40
#define MAGIC_TAP_TERM_MS 200

ZMK_EVENT_IMPL(zmk_magic_mode_changed);

static enum zmk_magic_mode current_mode = ZMK_MAGIC_MODE_NORMAL;
static int64_t magic_press_timestamp = 0;
static int64_t last_magic_tap_timestamp = 0;

static int raise_magic_mode_changed_event(void)
{
    return raise_zmk_magic_mode_changed((struct zmk_magic_mode_changed){
        .mode = (uint8_t)current_mode,
    });
}

enum zmk_magic_mode zmk_magic_mode_get(void) { return current_mode; }

bool zmk_magic_mode_is_shifted(void) { return current_mode != ZMK_MAGIC_MODE_NORMAL; }

void zmk_magic_mode_break_tap_sequence(void) { last_magic_tap_timestamp = 0; }

int zmk_magic_mode_set(enum zmk_magic_mode mode)
{
    if (mode < ZMK_MAGIC_MODE_NORMAL || mode > ZMK_MAGIC_MODE_CAPS)
    {
        return -EINVAL;
    }

    if (current_mode == mode)
    {
        return 0;
    }

    current_mode = mode;
    LOG_DBG("magic mode -> %d", current_mode);
    return raise_magic_mode_changed_event();
}

int zmk_magic_mode_clear_shift_word(void)
{
    zmk_magic_mode_break_tap_sequence();

    if (current_mode != ZMK_MAGIC_MODE_SHIFT_WORD)
    {
        return 0;
    }

    return zmk_magic_mode_set(ZMK_MAGIC_MODE_NORMAL);
}

int zmk_magic_mode_handle_tap(int64_t timestamp)
{
    bool is_double_tap =
        last_magic_tap_timestamp > 0 && (timestamp - last_magic_tap_timestamp) <= MAGIC_TAP_TERM_MS;

    if (is_double_tap)
    {
        zmk_magic_mode_break_tap_sequence();
        if (current_mode == ZMK_MAGIC_MODE_CAPS)
        {
            return zmk_magic_mode_set(ZMK_MAGIC_MODE_NORMAL);
        }

        return zmk_magic_mode_set(ZMK_MAGIC_MODE_CAPS);
    }

    last_magic_tap_timestamp = timestamp;

    if (current_mode == ZMK_MAGIC_MODE_NORMAL)
    {
        return zmk_magic_mode_set(ZMK_MAGIC_MODE_SHIFT_WORD);
    }

    return 0;
}

static bool magic_mode_is_alpha_like(const struct zmk_keycode_state_changed *ev)
{
    return ev->usage_page == HID_USAGE_KEY &&
           ev->keycode >= HID_USAGE_KEY_KEYBOARD_A &&
           ev->keycode <= HID_USAGE_KEY_KEYBOARD_Z;
}

static bool magic_mode_is_shift_key(const struct zmk_keycode_state_changed *ev)
{
    return ev->usage_page == HID_USAGE_KEY &&
           (ev->keycode == HID_USAGE_KEY_KEYBOARD_LEFT_SHIFT ||
            ev->keycode == HID_USAGE_KEY_KEYBOARD_RIGHT_SHIFT);
}

static bool magic_mode_is_shift_word_terminator(const struct zmk_keycode_state_changed *ev)
{
    if (ev->usage_page != HID_USAGE_KEY)
    {
        return false;
    }

    return ev->keycode == HID_USAGE_KEY_KEYBOARD_SPACEBAR ||
           ev->keycode == HID_USAGE_KEY_KEYBOARD_RETURN_ENTER ||
           ev->keycode == HID_USAGE_KEY_KEYBOARD_TAB;
}

static int magic_mode_position_state_changed_listener(const zmk_event_t *eh)
{
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);

    if (ev == NULL)
    {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->position != MAGIC_SHIFT_POSITION)
    {
        if (ev->state)
        {
            zmk_magic_mode_break_tap_sequence();
        }

        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->state)
    {
        magic_press_timestamp = ev->timestamp;
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (magic_press_timestamp == 0)
    {
        return ZMK_EV_EVENT_BUBBLE;
    }

    int64_t held_ms = ev->timestamp - magic_press_timestamp;
    magic_press_timestamp = 0;

    if (held_ms > MAGIC_TAP_TERM_MS)
    {
        zmk_magic_mode_break_tap_sequence();
        return ZMK_EV_EVENT_BUBBLE;
    }

    zmk_magic_mode_handle_tap(ev->timestamp);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(magic_mode, magic_mode_position_state_changed_listener);
ZMK_SUBSCRIPTION(magic_mode, zmk_position_state_changed);

static int magic_mode_keycode_state_changed_listener(const zmk_event_t *eh)
{
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);

    if (ev == NULL || !ev->state)
    {
        return ZMK_EV_EVENT_BUBBLE;
    }

    zmk_magic_mode_break_tap_sequence();

    if (magic_mode_is_shift_key(ev) && current_mode != ZMK_MAGIC_MODE_NORMAL)
    {
        zmk_magic_mode_set(ZMK_MAGIC_MODE_NORMAL);
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (current_mode == ZMK_MAGIC_MODE_SHIFT_WORD && magic_mode_is_shift_word_terminator(ev))
    {
        zmk_magic_mode_set(ZMK_MAGIC_MODE_NORMAL);
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (current_mode != ZMK_MAGIC_MODE_NORMAL && magic_mode_is_alpha_like(ev))
    {
        ev->implicit_modifiers |= MOD_LSFT;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(magic_mode_keycode, magic_mode_keycode_state_changed_listener);
ZMK_SUBSCRIPTION(magic_mode_keycode, zmk_keycode_state_changed);
