/*
 * nb_layer_persist.c
 *
 * Saves the Norwegian layer (layer 1) toggle state to NVS so it survives power
 * cycles.  No keymap changes are needed — &tog 1 continues to work exactly as
 * before.  This file simply:
 *
 *   1. On boot  : reads the saved value; if it was on, activates layer 1.
 *   2. On change: whenever layer 1 is activated or deactivated, writes the new
 *                 state to NVS.
 */

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define NB_LAYER 1

#if IS_ENABLED(CONFIG_SETTINGS)

/* ── Settings: restore saved state on boot ──────────────────────────────── */

static int nb_layer_load_cb(const char *name, size_t len, settings_read_cb read_cb,
                            void *cb_arg)
{
    const char *next;
    if (!settings_name_steq(name, "on", &next) || next)
    {
        return -ENOENT;
    }

    bool active;
    if (len != sizeof(active))
    {
        return -EINVAL;
    }

    int rc = read_cb(cb_arg, &active, sizeof(active));
    if (rc < 0)
    {
        return rc;
    }

    if (active)
    {
        zmk_keymap_layer_activate(NB_LAYER);
    }

    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(nb_layer_persist, "nb_layer", NULL, nb_layer_load_cb, NULL, NULL);

/* ── Event listener: save state whenever layer 1 changes ────────────────── */

static int nb_layer_listener(const zmk_event_t *eh)
{
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (ev == NULL || ev->layer != NB_LAYER)
    {
        return ZMK_EV_EVENT_BUBBLE;
    }

    bool active = ev->state;
    settings_save_one("nb_layer/on", &active, sizeof(active));

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(nb_layer_persist, nb_layer_listener);
ZMK_SUBSCRIPTION(nb_layer_persist, zmk_layer_state_changed);

#endif /* IS_ENABLED(CONFIG_SETTINGS) */
