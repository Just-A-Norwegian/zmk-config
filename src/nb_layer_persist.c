/*
 * layout_persist.c  (nb_layer_persist.c)
 *
 * Saves the active layout (ENG-US / NOR-NB / Colemak) across power cycles
 * using Zephyr NVS settings.
 *
 *   1. On boot  : reads the saved layout; activates the matching overlay layer.
 *   2. On change: whenever layer 1 (NOR-NB) or layer 9 (Colemak) changes,
 *                 recomputes the current layout and writes it to NVS.
 *
 * Settings key: "layout/mode"  (uint8_t, see enum layout_mode below)
 */

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define NB_LAYER 1
#define COLEMAK_LAYER 2

enum layout_mode
{
    LAYOUT_ENG_US = 0,
    LAYOUT_NOR_NB = 1,
    LAYOUT_COLEMAK = 2,
};

#if IS_ENABLED(CONFIG_SETTINGS)

/* ── Settings: restore saved layout on boot ─────────────────────────────── */

static enum layout_mode saved_mode = LAYOUT_ENG_US;
static bool nb_active = false;
static bool colemak_active = false;

static int layout_load_cb(const char *name, size_t len, settings_read_cb read_cb,
                          void *cb_arg)
{
    const char *next;
    if (!settings_name_steq(name, "mode", &next) || next)
    {
        return -ENOENT;
    }

    uint8_t mode;
    if (len != sizeof(mode))
    {
        return -EINVAL;
    }

    int rc = read_cb(cb_arg, &mode, sizeof(mode));
    if (rc < 0)
    {
        return rc;
    }

    saved_mode = (enum layout_mode)mode;
    switch (saved_mode)
    {
    case LAYOUT_NOR_NB:
        zmk_keymap_layer_activate(NB_LAYER);
        nb_active = true;
        break;
    case LAYOUT_COLEMAK:
        zmk_keymap_layer_activate(COLEMAK_LAYER);
        colemak_active = true;
        break;
    default:
        break; /* LAYOUT_ENG_US: layer 0 is always the base, nothing to do */
    }

    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(layout_persist, "layout", NULL, layout_load_cb, NULL, NULL);

/* ── Event listener: save layout whenever NB or Colemak layer changes ────── */

static int layout_listener(const zmk_event_t *eh)
{
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (ev == NULL || (ev->layer != NB_LAYER && ev->layer != COLEMAK_LAYER))
    {
        return ZMK_EV_EVENT_BUBBLE;
    }

    /* Update our local shadow of each overlay's active state. */
    if (ev->layer == NB_LAYER)
    {
        nb_active = ev->state;
    }
    else
    {
        colemak_active = ev->state;
    }

    /* Determine the current layout from the shadow state. */
    enum layout_mode mode;
    if (nb_active)
    {
        mode = LAYOUT_NOR_NB;
    }
    else if (colemak_active)
    {
        mode = LAYOUT_COLEMAK;
    }
    else
    {
        mode = LAYOUT_ENG_US;
    }

    if (mode != saved_mode)
    {
        saved_mode = mode;
        uint8_t val = (uint8_t)mode;
        settings_save_one("layout/mode", &val, sizeof(val));
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layout_persist, layout_listener);
ZMK_SUBSCRIPTION(layout_persist, zmk_layer_state_changed);

#endif /* IS_ENABLED(CONFIG_SETTINGS) */
