/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_system_enter

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define SYSTEM_ENTER_HOLD_BINDING 0
#define SYSTEM_ENTER_TAP_BINDING 1
#define SYSTEM_ENTER_BINDING_COUNT 2
#define SYSTEM_ENTER_MAX_POSITION 64

enum system_enter_phase {
    SYSTEM_ENTER_IDLE,
    SYSTEM_ENTER_NORMAL,
    SYSTEM_ENTER_CHORD_PENDING,
    SYSTEM_ENTER_SYSTEM_SELECTED,
};

struct behavior_system_enter_config {
    uint32_t tapping_term_ms;
    uint32_t system_chord_term_ms;
    uint32_t first_non_thumb_position;
    zmk_keymap_layer_id_t system_layer;
    struct zmk_behavior_binding bindings[SYSTEM_ENTER_BINDING_COUNT];
};

struct behavior_system_enter_data {
    struct k_mutex lock;
    struct k_work_delayable timer;
    const struct behavior_system_enter_config *config;
    enum system_enter_phase phase;
    struct zmk_behavior_binding_event enter_event;
    int64_t enter_pressed_at;
    int64_t system_chord_deadline;
    uint32_t hold_layer;
    uint32_t tap_keycode;
    uint64_t system_keys_down;
    bool magic_shift_down;
    bool enter_down;
    bool hold_layer_active;
    bool system_layer_active;
};

#define SYSTEM_ENTER_INSTANCE_COUNT DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)

static struct behavior_system_enter_data
    *system_enter_instances[SYSTEM_ENTER_INSTANCE_COUNT];
static size_t system_enter_instance_count;

static struct zmk_behavior_binding enter_action_binding(
    const struct behavior_system_enter_data *data, size_t index) {
    struct zmk_behavior_binding binding = data->config->bindings[index];
    binding.param1 = index == SYSTEM_ENTER_HOLD_BINDING ? data->hold_layer : data->tap_keycode;
    return binding;
}

static int invoke_enter_action(struct behavior_system_enter_data *data, size_t index,
                               struct zmk_behavior_binding_event event, bool pressed) {
    struct zmk_behavior_binding binding = enter_action_binding(data, index);
    return zmk_behavior_invoke_binding(&binding, event, pressed);
}

static int tap_enter(struct behavior_system_enter_data *data,
                     struct zmk_behavior_binding_event event) {
    int rc = invoke_enter_action(data, SYSTEM_ENTER_TAP_BINDING, event, true);
    if (rc < 0) {
        return rc;
    }

    return invoke_enter_action(data, SYSTEM_ENTER_TAP_BINDING, event, false);
}

static void cancel_timer(struct behavior_system_enter_data *data) {
    int rc = k_work_cancel_delayable(&data->timer);
    if (rc < 0 && rc != -EINPROGRESS) {
        LOG_ERR("Failed to cancel system-enter timer (err %d)", rc);
    }
}

static int schedule_timer(struct behavior_system_enter_data *data, int64_t deadline) {
    int64_t remaining = deadline - k_uptime_get();
    if (remaining < 0) {
        remaining = 0;
    }

    int rc = k_work_reschedule(&data->timer, K_MSEC(remaining));
    if (rc < 0) {
        LOG_ERR("Failed to schedule system-enter timer (err %d)", rc);
    }
    return rc;
}

static int activate_system_layer(struct behavior_system_enter_data *data,
                                 int64_t timestamp) {
    ARG_UNUSED(timestamp);
    if (data->phase == SYSTEM_ENTER_SYSTEM_SELECTED) {
        return 0;
    }

    cancel_timer(data);
    int rc = zmk_keymap_layer_activate(data->config->system_layer);
    if (rc < 0) {
        return rc;
    }

    data->phase = SYSTEM_ENTER_SYSTEM_SELECTED;
    data->system_layer_active = true;
    return 0;
}

static int deactivate_system_layer(struct behavior_system_enter_data *data) {
    if (!data->system_layer_active) {
        return 0;
    }

    int rc = zmk_keymap_layer_deactivate(data->config->system_layer);
    if (rc < 0) {
        LOG_ERR("Failed to deactivate System layer (err %d)", rc);
        return rc;
    }

    data->system_layer_active = false;
    return 0;
}

static void finish_system_chord_if_released(struct behavior_system_enter_data *data) {
    if (data->phase != SYSTEM_ENTER_SYSTEM_SELECTED || data->magic_shift_down ||
        data->enter_down || data->system_keys_down != 0) {
        return;
    }

    if (deactivate_system_layer(data) >= 0) {
        data->phase = SYSTEM_ENTER_IDLE;
    }
}

static int activate_hold_layer(struct behavior_system_enter_data *data,
                               struct zmk_behavior_binding_event event) {
    if (data->hold_layer_active) {
        return 0;
    }

    int rc = invoke_enter_action(data, SYSTEM_ENTER_HOLD_BINDING, event, true);
    if (rc >= 0) {
        data->hold_layer_active = true;
    }
    return rc;
}

static int deactivate_hold_layer(struct behavior_system_enter_data *data,
                                 struct zmk_behavior_binding_event event) {
    if (!data->hold_layer_active) {
        return 0;
    }

    int rc = invoke_enter_action(data, SYSTEM_ENTER_HOLD_BINDING, event, false);
    if (rc >= 0) {
        data->hold_layer_active = false;
    }
    return rc;
}

static int resume_normal_enter(struct behavior_system_enter_data *data, int64_t timestamp) {
    data->phase = SYSTEM_ENTER_NORMAL;
    if (!data->enter_down) {
        data->phase = SYSTEM_ENTER_IDLE;
        return 0;
    }

    int64_t deadline = data->enter_pressed_at + data->config->tapping_term_ms;
    if (timestamp >= deadline) {
        data->enter_event.timestamp = timestamp;
        int rc = activate_hold_layer(data, data->enter_event);
        if (rc < 0) {
            LOG_ERR("Failed to activate Enter hold layer (err %d)", rc);
        }
        return rc;
    }

    return schedule_timer(data, deadline);
}

static int begin_system_chord(struct behavior_system_enter_data *data, int64_t timestamp) {
    if (!data->enter_down || !data->magic_shift_down ||
        data->phase == SYSTEM_ENTER_SYSTEM_SELECTED) {
        return 0;
    }

    cancel_timer(data);
    int rc = deactivate_hold_layer(data, data->enter_event);
    if (rc < 0) {
        LOG_ERR("Failed to cancel Enter hold layer for System chord (err %d)", rc);
        return rc;
    }

    data->phase = SYSTEM_ENTER_CHORD_PENDING;
    data->system_chord_deadline =
        MAX(timestamp, data->enter_pressed_at) + data->config->system_chord_term_ms;
    return schedule_timer(data, data->system_chord_deadline);
}

static void system_enter_timer_handler(struct k_work *work) {
    struct k_work_delayable *delayable = k_work_delayable_from_work(work);
    struct behavior_system_enter_data *data =
        CONTAINER_OF(delayable, struct behavior_system_enter_data, timer);

    k_mutex_lock(&data->lock, K_FOREVER);
    int64_t now = k_uptime_get();
    data->enter_event.timestamp = now;
    if (data->phase == SYSTEM_ENTER_CHORD_PENDING && data->enter_down &&
        data->magic_shift_down) {
        int rc = activate_system_layer(data, now);
        if (rc < 0) {
            LOG_ERR("System chord activation failed (err %d)", rc);
        }
    } else if (data->phase == SYSTEM_ENTER_NORMAL && data->enter_down) {
        int rc = activate_hold_layer(data, data->enter_event);
        if (rc < 0) {
            LOG_ERR("Failed to activate Enter hold layer (err %d)", rc);
        }
    }
    k_mutex_unlock(&data->lock);
}

static int on_system_enter_binding_pressed(struct zmk_behavior_binding *binding,
                                           struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_system_enter_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);
    if (data->phase != SYSTEM_ENTER_IDLE || data->enter_down) {
        LOG_WRN("Ignoring unexpected system-enter press at position %u", event.position);
        k_mutex_unlock(&data->lock);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    data->enter_event = event;
    data->enter_pressed_at = event.timestamp;
    data->hold_layer = binding->param1;
    data->tap_keycode = binding->param2;
    data->enter_down = true;

    int rc;
    if (data->magic_shift_down) {
        rc = begin_system_chord(data, event.timestamp);
    } else {
        data->phase = SYSTEM_ENTER_NORMAL;
        rc = schedule_timer(data, event.timestamp + data->config->tapping_term_ms);
    }
    k_mutex_unlock(&data->lock);
    return rc < 0 ? rc : ZMK_BEHAVIOR_OPAQUE;
}

static int on_system_enter_binding_released(struct zmk_behavior_binding *binding,
                                            struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_system_enter_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);
    if (!data->enter_down) {
        LOG_WRN("Ignoring unexpected system-enter release at position %u", event.position);
        k_mutex_unlock(&data->lock);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if (data->phase == SYSTEM_ENTER_CHORD_PENDING && data->magic_shift_down &&
        event.timestamp >= data->system_chord_deadline) {
        int rc = activate_system_layer(data, event.timestamp);
        if (rc < 0) {
            LOG_ERR("System chord activation failed on release (err %d)", rc);
        }
    }

    data->enter_event = event;
    data->enter_down = false;
    cancel_timer(data);

    int rc = 0;
    switch (data->phase) {
    case SYSTEM_ENTER_NORMAL: {
        int64_t elapsed = event.timestamp - data->enter_pressed_at;
        if (data->hold_layer_active) {
            rc = deactivate_hold_layer(data, event);
        } else if (elapsed >= data->config->tapping_term_ms) {
            rc = activate_hold_layer(data, event);
            if (rc >= 0) {
                rc = deactivate_hold_layer(data, event);
            }
        } else {
            rc = tap_enter(data, event);
        }
        data->phase = SYSTEM_ENTER_IDLE;
        break;
    }
    case SYSTEM_ENTER_CHORD_PENDING:
        rc = tap_enter(data, event);
        data->phase = SYSTEM_ENTER_IDLE;
        break;
    case SYSTEM_ENTER_SYSTEM_SELECTED:
        finish_system_chord_if_released(data);
        break;
    case SYSTEM_ENTER_IDLE:
        break;
    }

    if (rc < 0) {
        LOG_ERR("System-enter release action failed (err %d)", rc);
    }
    k_mutex_unlock(&data->lock);
    return rc < 0 ? rc : ZMK_BEHAVIOR_OPAQUE;
}

static int system_enter_position_state_changed_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *event = as_zmk_position_state_changed(eh);
    if (event == NULL || event->position >= SYSTEM_ENTER_MAX_POSITION) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    uint64_t key_mask = (uint64_t)1 << event->position;
    for (size_t i = 0; i < system_enter_instance_count; i++) {
        struct behavior_system_enter_data *data = system_enter_instances[i];
        k_mutex_lock(&data->lock, K_FOREVER);

        if (event->position < data->config->first_non_thumb_position) {
            if (event->state) {
                if (data->phase == SYSTEM_ENTER_CHORD_PENDING && data->enter_down &&
                    data->magic_shift_down) {
                    data->system_keys_down |= key_mask;
                    int rc = activate_system_layer(data, event->timestamp);
                    if (rc < 0) {
                        LOG_ERR("System chord activation failed on key press (err %d)", rc);
                    }
                } else if (data->phase == SYSTEM_ENTER_SYSTEM_SELECTED) {
                    data->system_keys_down |= key_mask;
                } else if (data->phase == SYSTEM_ENTER_NORMAL && data->enter_down &&
                           !data->magic_shift_down) {
                    data->enter_event.timestamp = event->timestamp;
                    int rc = activate_hold_layer(data, data->enter_event);
                    if (rc < 0) {
                        LOG_ERR("Failed to activate Enter hold layer on key press (err %d)", rc);
                    } else {
                        cancel_timer(data);
                    }
                }
            } else {
                data->system_keys_down &= ~key_mask;
                finish_system_chord_if_released(data);
            }
        }

        k_mutex_unlock(&data->lock);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(system_enter, system_enter_position_state_changed_listener);
ZMK_SUBSCRIPTION(system_enter, zmk_position_state_changed);

void zmk_behavior_system_enter_shift_state_changed(bool pressed, int64_t timestamp) {
    for (size_t i = 0; i < system_enter_instance_count; i++) {
        struct behavior_system_enter_data *data = system_enter_instances[i];
        k_mutex_lock(&data->lock, K_FOREVER);

        data->magic_shift_down = pressed;
        if (pressed) {
            if (data->phase == SYSTEM_ENTER_NORMAL && data->enter_down) {
                data->enter_event.timestamp = timestamp;
                int rc = begin_system_chord(data, timestamp);
                if (rc < 0) {
                    LOG_ERR("Failed to start System chord timer (err %d)", rc);
                }
            }
        } else if (data->phase == SYSTEM_ENTER_CHORD_PENDING) {
            if (data->enter_down && timestamp >= data->system_chord_deadline) {
                int rc = activate_system_layer(data, timestamp);
                if (rc < 0) {
                    LOG_ERR("System chord activation failed on Shift release (err %d)", rc);
                }
            }
            if (data->phase == SYSTEM_ENTER_CHORD_PENDING) {
                cancel_timer(data);
                int rc = resume_normal_enter(data, timestamp);
                if (rc < 0) {
                    LOG_ERR("Failed to resume Enter after cancelled System chord (err %d)", rc);
                }
            } else {
                finish_system_chord_if_released(data);
            }
        } else if (data->phase == SYSTEM_ENTER_SYSTEM_SELECTED) {
            finish_system_chord_if_released(data);
        }

        k_mutex_unlock(&data->lock);
    }
}

static int behavior_system_enter_init(const struct device *dev) {
    struct behavior_system_enter_data *data = dev->data;
    if (system_enter_instance_count >= ARRAY_SIZE(system_enter_instances)) {
        LOG_ERR("Too many system-enter behavior instances");
        return -ENOMEM;
    }

    data->config = dev->config;
    k_mutex_init(&data->lock);
    k_work_init_delayable(&data->timer, system_enter_timer_handler);
    data->phase = SYSTEM_ENTER_IDLE;
    system_enter_instances[system_enter_instance_count++] = data;
    return 0;
}

#define SYSTEM_ENTER_INST(n)                                                                       \
    static const struct behavior_system_enter_config behavior_system_enter_config_##n = {          \
        .tapping_term_ms = DT_INST_PROP(n, tapping_term_ms),                                        \
        .system_chord_term_ms = DT_INST_PROP(n, system_chord_term_ms),                              \
        .first_non_thumb_position = DT_INST_PROP(n, first_non_thumb_position),                      \
        .system_layer = DT_NODE_CHILD_IDX(DT_INST_PHANDLE(n, system_layer)),                        \
        .bindings = {                                                                              \
            ZMK_KEYMAP_EXTRACT_BINDING(SYSTEM_ENTER_HOLD_BINDING, DT_DRV_INST(n)),                  \
            ZMK_KEYMAP_EXTRACT_BINDING(SYSTEM_ENTER_TAP_BINDING, DT_DRV_INST(n)),                   \
        },                                                                                         \
    };                                                                                             \
    static struct behavior_system_enter_data behavior_system_enter_data_##n;                        \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_system_enter_init, NULL,                                   \
                            &behavior_system_enter_data_##n,                                       \
                            &behavior_system_enter_config_##n, POST_KERNEL,                        \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_system_enter_driver_api);

static const struct behavior_driver_api behavior_system_enter_driver_api = {
    .binding_pressed = on_system_enter_binding_pressed,
    .binding_released = on_system_enter_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

DT_INST_FOREACH_STATUS_OKAY(SYSTEM_ENTER_INST)

#endif
