/*
 * Persists the active typing layout (ENG-US / NOR-NB / Colemak) per Bluetooth
 * profile using Zephyr settings.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <zmk/ble.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
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

#if IS_ENABLED(CONFIG_SETTINGS) && IS_ENABLED(CONFIG_ZMK_BLE)

static enum layout_mode profile_modes[ZMK_BLE_PROFILE_COUNT];
static bool profile_mode_valid[ZMK_BLE_PROFILE_COUNT];
static bool profile_mode_dirty[ZMK_BLE_PROFILE_COUNT];
static bool restoring_mode = false;
static enum layout_mode restore_target_mode = LAYOUT_NOR_NB;

static enum layout_mode active_layout_mode(void)
{
    if (zmk_keymap_layer_active(NB_LAYER))
    {
        return LAYOUT_NOR_NB;
    }

    if (zmk_keymap_layer_active(COLEMAK_LAYER))
    {
        return LAYOUT_COLEMAK;
    }

    return LAYOUT_ENG_US;
}

static int set_layer_active(uint8_t layer, bool active)
{
    return active ? zmk_keymap_layer_activate(layer) : zmk_keymap_layer_deactivate(layer);
}

static void persist_profile_mode(int profile_index, enum layout_mode mode)
{
    if (profile_index < 0 || profile_index >= ZMK_BLE_PROFILE_COUNT)
    {
        LOG_ERR("Invalid active Bluetooth profile index: %d", profile_index);
        return;
    }

    if (!profile_mode_valid[profile_index] || profile_modes[profile_index] != mode)
    {
        profile_modes[profile_index] = mode;
        profile_mode_valid[profile_index] = true;
        profile_mode_dirty[profile_index] = true;
    }

    if (!profile_mode_dirty[profile_index])
    {
        return;
    }

    char setting_name[32];
    int name_length = snprintf(setting_name, sizeof(setting_name), "layout/profile/%d",
                               profile_index);
    if (name_length < 0 || name_length >= (int)sizeof(setting_name))
    {
        LOG_ERR("Failed to format Bluetooth profile layout setting name");
        return;
    }

    uint8_t value = (uint8_t)mode;
    int rc = settings_save_one(setting_name, &value, sizeof(value));
    if (rc < 0)
    {
        LOG_ERR("Failed to save layout for Bluetooth profile %d (err %d)", profile_index, rc);
        return;
    }

    profile_mode_dirty[profile_index] = false;
}

static void restore_profile_mode(uint8_t profile_index)
{
    if (profile_index >= ZMK_BLE_PROFILE_COUNT)
    {
        LOG_ERR("Invalid Bluetooth profile index: %u", (unsigned int)profile_index);
        return;
    }

    enum layout_mode mode = profile_mode_valid[profile_index]
                                ? profile_modes[profile_index]
                                : LAYOUT_NOR_NB;

    if (active_layout_mode() == mode)
    {
        restoring_mode = false;
        return;
    }

    restore_target_mode = mode;
    restoring_mode = true;

    switch (mode)
    {
    case LAYOUT_NOR_NB:
        if (!zmk_keymap_layer_active(NB_LAYER))
        {
            int rc = set_layer_active(NB_LAYER, true);
            if (rc < 0)
            {
                LOG_ERR("Failed to activate Norwegian layout layer (err %d)", rc);
                restoring_mode = false;
                return;
            }
        }
        if (zmk_keymap_layer_active(COLEMAK_LAYER))
        {
            int rc = set_layer_active(COLEMAK_LAYER, false);
            if (rc < 0)
            {
                LOG_ERR("Failed to deactivate Colemak layout layer (err %d)", rc);
                restoring_mode = false;
                return;
            }
        }
        break;
    case LAYOUT_COLEMAK:
        if (!zmk_keymap_layer_active(COLEMAK_LAYER))
        {
            int rc = set_layer_active(COLEMAK_LAYER, true);
            if (rc < 0)
            {
                LOG_ERR("Failed to activate Colemak layout layer (err %d)", rc);
                restoring_mode = false;
                return;
            }
        }
        if (zmk_keymap_layer_active(NB_LAYER))
        {
            int rc = set_layer_active(NB_LAYER, false);
            if (rc < 0)
            {
                LOG_ERR("Failed to deactivate Norwegian layout layer (err %d)", rc);
                restoring_mode = false;
                return;
            }
        }
        break;
    default:
        if (zmk_keymap_layer_active(NB_LAYER))
        {
            int rc = set_layer_active(NB_LAYER, false);
            if (rc < 0)
            {
                LOG_ERR("Failed to deactivate Norwegian layout layer (err %d)", rc);
                restoring_mode = false;
                return;
            }
        }
        if (zmk_keymap_layer_active(COLEMAK_LAYER))
        {
            int rc = set_layer_active(COLEMAK_LAYER, false);
            if (rc < 0)
            {
                LOG_ERR("Failed to deactivate Colemak layout layer (err %d)", rc);
                restoring_mode = false;
                return;
            }
        }
        break;
    }

    if (active_layout_mode() == restore_target_mode)
    {
        restoring_mode = false;
    }
}

static int layout_load_cb(const char *name, size_t len, settings_read_cb read_cb,
                          void *cb_arg)
{
    const char *next;
    if (!settings_name_steq(name, "profile", &next) || !next || !*next)
    {
        return -ENOENT;
    }

    char *end;
    unsigned long profile_index = strtoul(next, &end, 10);
    if (end == next || *end != '\0' || profile_index >= ZMK_BLE_PROFILE_COUNT)
    {
        return -ENOENT;
    }

    uint8_t value;
    if (len != sizeof(value))
    {
        return -EINVAL;
    }

    int rc = read_cb(cb_arg, &value, sizeof(value));
    if (rc < 0)
    {
        return rc;
    }
    if (rc != (int)sizeof(value) || value > LAYOUT_COLEMAK)
    {
        return -EINVAL;
    }

    profile_modes[profile_index] = (enum layout_mode)value;
    profile_mode_valid[profile_index] = true;
    profile_mode_dirty[profile_index] = false;
    return 0;
}

static int layout_commit_cb(void)
{
    int profile_index = zmk_ble_active_profile_index();
    if (profile_index < 0 || profile_index >= ZMK_BLE_PROFILE_COUNT)
    {
        LOG_ERR("Invalid active Bluetooth profile index during settings load: %d",
                profile_index);
        return -EINVAL;
    }

    restore_profile_mode((uint8_t)profile_index);
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(layout_persist, "layout", NULL, layout_load_cb,
                               layout_commit_cb, NULL);

static int layout_listener(const zmk_event_t *eh)
{
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (ev == NULL || (ev->layer != NB_LAYER && ev->layer != COLEMAK_LAYER))
    {
        return ZMK_EV_EVENT_BUBBLE;
    }

    enum layout_mode mode = active_layout_mode();
    if (restoring_mode)
    {
        if (mode == restore_target_mode)
        {
            restoring_mode = false;
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    persist_profile_mode(zmk_ble_active_profile_index(), mode);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layout_persist, layout_listener);
ZMK_SUBSCRIPTION(layout_persist, zmk_layer_state_changed);

static int profile_listener(const zmk_event_t *eh)
{
    const struct zmk_ble_active_profile_changed *ev =
        as_zmk_ble_active_profile_changed(eh);
    if (ev != NULL)
    {
        restore_profile_mode(ev->index);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layout_profile_persist, profile_listener);
ZMK_SUBSCRIPTION(layout_profile_persist, zmk_ble_active_profile_changed);

#endif /* IS_ENABLED(CONFIG_SETTINGS) && IS_ENABLED(CONFIG_ZMK_BLE) */
