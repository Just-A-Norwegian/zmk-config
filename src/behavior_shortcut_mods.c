/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_shortcut_mods

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define SHORTCUT_DELETE_POSITION 45
#define SHORTCUT_POSITION_COUNT 46
#define SHORTCUT_MODIFIER_COUNT 8
#define SHORTCUT_LEFT_GUI_POSITION 15
#define SHORTCUT_LEFT_ALT_POSITION 16
#define SHORTCUT_LEFT_CTRL_POSITION 17
#define SHORTCUT_LEFT_SHIFT_POSITION 18
#define SHORTCUT_RIGHT_SHIFT_POSITION 23
#define SHORTCUT_RIGHT_CTRL_POSITION 24
#define SHORTCUT_RIGHT_ALT_POSITION 25
#define SHORTCUT_RIGHT_GUI_POSITION 26

enum shortcut_side {
    SHORTCUT_SIDE_NONE,
    SHORTCUT_SIDE_LEFT,
    SHORTCUT_SIDE_RIGHT,
};

struct behavior_shortcut_mods_config {
    struct zmk_behavior_binding modifiers[SHORTCUT_MODIFIER_COUNT];
    zmk_keymap_layer_id_t selector_layer;
    zmk_keymap_layer_id_t left_layer;
    zmk_keymap_layer_id_t right_layer;
};

struct behavior_shortcut_mods_data {
    struct k_mutex lock;
    const struct behavior_shortcut_mods_config *config;
    enum shortcut_side selected_side;
    bool handled_positions[SHORTCUT_POSITION_COUNT];
    bool active;
};

static int modifier_index(uint32_t position) {
    switch (position) {
    case SHORTCUT_LEFT_GUI_POSITION:
        return 0;
    case SHORTCUT_LEFT_ALT_POSITION:
        return 1;
    case SHORTCUT_LEFT_CTRL_POSITION:
        return 2;
    case SHORTCUT_LEFT_SHIFT_POSITION:
        return 3;
    case SHORTCUT_RIGHT_SHIFT_POSITION:
        return 4;
    case SHORTCUT_RIGHT_CTRL_POSITION:
        return 5;
    case SHORTCUT_RIGHT_ALT_POSITION:
        return 6;
    case SHORTCUT_RIGHT_GUI_POSITION:
        return 7;
    default:
        return -1;
    }
}

static int set_layer_active(zmk_keymap_layer_id_t layer, bool active) {
    return active ? zmk_keymap_layer_activate(layer) : zmk_keymap_layer_deactivate(layer);
}

static int on_shortcut_mods_binding_pressed(struct zmk_behavior_binding *binding,
                                            struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_shortcut_mods_data *data = dev->data;
    int rc = 0;

    k_mutex_lock(&data->lock, K_FOREVER);
    if (event.position == SHORTCUT_DELETE_POSITION) {
        if (data->active) {
            LOG_WRN("Ignoring unexpected shortcut-mode press");
        } else {
            rc = set_layer_active(data->config->selector_layer, true);
            if (rc < 0) {
                LOG_ERR("Failed to activate shortcut selector layer (err %d)", rc);
            } else {
                data->active = true;
                data->selected_side = SHORTCUT_SIDE_NONE;
            }
        }
        k_mutex_unlock(&data->lock);
        return rc < 0 ? rc : ZMK_BEHAVIOR_OPAQUE;
    }

    int index = modifier_index(event.position);
    if (index < 0 || !data->active) {
        k_mutex_unlock(&data->lock);
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

    enum shortcut_side side = index < 4 ? SHORTCUT_SIDE_LEFT : SHORTCUT_SIDE_RIGHT;
    if (data->selected_side == SHORTCUT_SIDE_NONE) {
        zmk_keymap_layer_id_t layer =
            side == SHORTCUT_SIDE_LEFT ? data->config->left_layer : data->config->right_layer;
        rc = set_layer_active(layer, true);
        if (rc < 0) {
            LOG_ERR("Failed to activate shortcut modifier layer (err %d)", rc);
            k_mutex_unlock(&data->lock);
            return rc;
        }

        rc = set_layer_active(data->config->selector_layer, false);
        if (rc < 0) {
            LOG_ERR("Failed to deactivate shortcut selector layer (err %d)", rc);
            int rollback_rc = set_layer_active(layer, false);
            if (rollback_rc < 0) {
                LOG_ERR("Failed to roll back shortcut modifier layer (err %d)", rollback_rc);
            }
            k_mutex_unlock(&data->lock);
            return rc;
        }

        data->selected_side = side;
    }

    if (data->selected_side == side) {
        data->handled_positions[event.position] = true;
        rc = zmk_behavior_invoke_binding(&data->config->modifiers[index], event, true);
        if (rc < 0) {
            LOG_ERR("Failed to activate shortcut modifier at position %u (err %d)",
                    event.position, rc);
        }
    } else {
        rc = ZMK_BEHAVIOR_TRANSPARENT;
    }

    k_mutex_unlock(&data->lock);
    return rc;
}

static int on_shortcut_mods_binding_released(struct zmk_behavior_binding *binding,
                                             struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_shortcut_mods_data *data = dev->data;
    int rc = 0;

    k_mutex_lock(&data->lock, K_FOREVER);
    if (event.position == SHORTCUT_DELETE_POSITION) {
        if (data->selected_side != SHORTCUT_SIDE_NONE) {
            zmk_keymap_layer_id_t layer =
                data->selected_side == SHORTCUT_SIDE_LEFT ? data->config->left_layer
                                                          : data->config->right_layer;
            rc = set_layer_active(layer, false);
            if (rc < 0) {
                LOG_ERR("Failed to deactivate shortcut modifier layer (err %d)", rc);
            }
            data->selected_side = SHORTCUT_SIDE_NONE;
        }

        int selector_rc = set_layer_active(data->config->selector_layer, false);
        if (selector_rc < 0) {
            LOG_ERR("Failed to deactivate shortcut selector layer (err %d)", selector_rc);
            if (rc >= 0) {
                rc = selector_rc;
            }
        }
        data->active = false;
    } else {
        int index = modifier_index(event.position);
        if (index >= 0 && data->handled_positions[event.position]) {
            data->handled_positions[event.position] = false;
            rc = zmk_behavior_invoke_binding(&data->config->modifiers[index], event, false);
            if (rc < 0) {
                LOG_ERR("Failed to release shortcut modifier at position %u (err %d)",
                        event.position, rc);
            }
        } else {
            rc = ZMK_BEHAVIOR_TRANSPARENT;
        }
    }

    k_mutex_unlock(&data->lock);
    return rc;
}

static const struct behavior_driver_api behavior_shortcut_mods_driver_api = {
    .binding_pressed = on_shortcut_mods_binding_pressed,
    .binding_released = on_shortcut_mods_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

static int behavior_shortcut_mods_init(const struct device *dev) {
    struct behavior_shortcut_mods_data *data = dev->data;
    data->config = dev->config;
    data->active = false;
    data->selected_side = SHORTCUT_SIDE_NONE;
    k_mutex_init(&data->lock);
    return 0;
}

#define SHORTCUT_MODS_INST(n)                                                                      \
    static const struct behavior_shortcut_mods_config behavior_shortcut_mods_config_##n = {        \
        .modifiers = {                                                                             \
            ZMK_KEYMAP_EXTRACT_BINDING(0, DT_DRV_INST(n)),                                         \
            ZMK_KEYMAP_EXTRACT_BINDING(1, DT_DRV_INST(n)),                                         \
            ZMK_KEYMAP_EXTRACT_BINDING(2, DT_DRV_INST(n)),                                         \
            ZMK_KEYMAP_EXTRACT_BINDING(3, DT_DRV_INST(n)),                                         \
            ZMK_KEYMAP_EXTRACT_BINDING(4, DT_DRV_INST(n)),                                         \
            ZMK_KEYMAP_EXTRACT_BINDING(5, DT_DRV_INST(n)),                                         \
            ZMK_KEYMAP_EXTRACT_BINDING(6, DT_DRV_INST(n)),                                         \
            ZMK_KEYMAP_EXTRACT_BINDING(7, DT_DRV_INST(n)),                                         \
        },                                                                                         \
        .selector_layer = DT_NODE_CHILD_IDX(DT_NODELABEL(shortcut_select_layer)),                 \
        .left_layer = DT_NODE_CHILD_IDX(DT_NODELABEL(shortcut_left_layer)),                       \
        .right_layer = DT_NODE_CHILD_IDX(DT_NODELABEL(shortcut_right_layer)),                     \
    };                                                                                             \
    static struct behavior_shortcut_mods_data behavior_shortcut_mods_data_##n;                    \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_shortcut_mods_init, NULL,                                 \
                            &behavior_shortcut_mods_data_##n,                                     \
                            &behavior_shortcut_mods_config_##n, POST_KERNEL,                      \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                  \
                            &behavior_shortcut_mods_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SHORTCUT_MODS_INST)

#endif
