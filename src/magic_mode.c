#include <errno.h>

#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/magic_mode_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/magic_mode.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define MAGIC_SHIFT_POSITION 40
#define MAGIC_TAP_TERM_MS 200

ZMK_EVENT_IMPL(zmk_magic_mode_changed);

static enum zmk_magic_mode current_mode = ZMK_MAGIC_MODE_NORMAL;
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

static int magic_mode_position_state_changed_listener(const zmk_event_t *eh)
{
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);

    if (ev == NULL || !ev->state)
    {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->position != MAGIC_SHIFT_POSITION)
    {
        zmk_magic_mode_break_tap_sequence();
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(magic_mode, magic_mode_position_state_changed_listener);
ZMK_SUBSCRIPTION(magic_mode, zmk_position_state_changed);
