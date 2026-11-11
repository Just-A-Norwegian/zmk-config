/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_magic_shift

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>

#if !defined(CONFIG_ZMK_SPLIT) || defined(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include "behavior_system_enter.h"
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define MAGIC_SHIFT_BINDING_SHIFT 0
#define MAGIC_SHIFT_BINDING_CAPS 1
#define MAGIC_SHIFT_BINDING_COUNT 2
#define MAGIC_SHIFT_TIMER_GRACE_MS 20

enum magic_shift_phase {
    MAGIC_SHIFT_IDLE,
    MAGIC_SHIFT_PRESSED,
    MAGIC_SHIFT_WAITING_FOR_SECOND_TAP,
    MAGIC_SHIFT_SECOND_PRESSED,
    MAGIC_SHIFT_PENDING_ONE_SHOT,
    MAGIC_SHIFT_PRESSED_WITH_PENDING,
};

struct behavior_magic_shift_config {
    uint32_t hold_term_ms;
    uint32_t double_tap_ms;
    uint32_t one_shot_term_ms;
    struct zmk_behavior_binding bindings[MAGIC_SHIFT_BINDING_COUNT];
};

struct behavior_magic_shift_data {
    struct k_mutex lock;
    struct k_work_delayable tap_timer;
    const struct behavior_magic_shift_config *config;
    enum magic_shift_phase phase;
    struct zmk_behavior_binding_event key_event;
    int64_t key_pressed_at;
    int64_t tap_released_at;
    int64_t tap_deadline;
    int64_t one_shot_deadline;
    bool shift_active;
    bool chord_seen;
};

#define MAGIC_SHIFT_INSTANCE_COUNT DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)

static struct behavior_magic_shift_data
    *magic_shift_instances[MAGIC_SHIFT_INSTANCE_COUNT];
static size_t magic_shift_instance_count;

static int invoke_binding(const struct zmk_behavior_binding *binding,
                          struct zmk_behavior_binding_event event, bool pressed) {
    return zmk_behavior_invoke_binding(binding, event, pressed);
}

static int tap_binding(const struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event) {
    int rc = invoke_binding(binding, event, true);
    if (rc < 0) {
        return rc;
    }

    return invoke_binding(binding, event, false);
}

static struct zmk_behavior_binding_event action_event(
    const struct behavior_magic_shift_data *data, int64_t timestamp) {
    struct zmk_behavior_binding_event event = data->key_event;
    event.timestamp = timestamp;
    return event;
}

static void cancel_tap_timer(struct behavior_magic_shift_data *data) {
    int rc = k_work_cancel_delayable(&data->tap_timer);
    if (rc < 0 && rc != -EINPROGRESS) {
        LOG_ERR("Failed to cancel magic-shift tap timer (err %d)", rc);
    }
}

static int set_shift_active(struct behavior_magic_shift_data *data, bool active,
                            int64_t timestamp) {
    if (data->shift_active == active) {
        return 0;
    }

    struct zmk_behavior_binding_event event = action_event(data, timestamp);
    int rc = invoke_binding(&data->config->bindings[MAGIC_SHIFT_BINDING_SHIFT], event, active);
    if (rc >= 0) {
        data->shift_active = active;
    }
    return rc;
}

static int update_shift_state(struct behavior_magic_shift_data *data, int64_t timestamp) {
    return set_shift_active(data, data->phase != MAGIC_SHIFT_IDLE, timestamp);
}

static int toggle_caps_lock(struct behavior_magic_shift_data *data, int64_t timestamp) {
    struct zmk_behavior_binding_event event = action_event(data, timestamp);
    return tap_binding(&data->config->bindings[MAGIC_SHIFT_BINDING_CAPS], event);
}

static int schedule_tap_timer(struct behavior_magic_shift_data *data, int64_t deadline) {
    int64_t remaining = deadline - k_uptime_get();
    if (remaining < 0) {
        remaining = 0;
    }

    int rc = k_work_reschedule(&data->tap_timer, K_MSEC(remaining));
    if (rc < 0) {
        LOG_ERR("Failed to schedule magic-shift tap timer (err %d)", rc);
    }
    return rc;
}

static int start_one_shot_tap(struct behavior_magic_shift_data *data,
                              struct zmk_behavior_binding_event event) {
    data->key_event = event;
    data->tap_released_at = event.timestamp;
    data->tap_deadline = event.timestamp + data->config->double_tap_ms;
    data->one_shot_deadline =
        data->tap_deadline + MAGIC_SHIFT_TIMER_GRACE_MS + data->config->one_shot_term_ms;
    data->chord_seen = false;
    data->phase = MAGIC_SHIFT_WAITING_FOR_SECOND_TAP;

    int rc = update_shift_state(data, event.timestamp);
    if (rc < 0) {
        data->phase = MAGIC_SHIFT_IDLE;
        LOG_ERR("Failed to activate one-shot Shift (err %d)", rc);
        return rc;
    }

    rc = schedule_tap_timer(data, data->tap_deadline + MAGIC_SHIFT_TIMER_GRACE_MS);
    if (rc < 0) {
        data->phase = MAGIC_SHIFT_IDLE;
        int release_rc = update_shift_state(data, event.timestamp);
        if (release_rc < 0) {
            LOG_ERR("Failed to release Shift after timer scheduling error (err %d)", release_rc);
        }
    }
    return rc;
}

static void magic_shift_tap_timer_handler(struct k_work *work) {
    struct k_work_delayable *delayable = k_work_delayable_from_work(work);
    struct behavior_magic_shift_data *data =
        CONTAINER_OF(delayable, struct behavior_magic_shift_data, tap_timer);

    k_mutex_lock(&data->lock, K_FOREVER);
    if (data->phase != MAGIC_SHIFT_WAITING_FOR_SECOND_TAP &&
        data->phase != MAGIC_SHIFT_PENDING_ONE_SHOT &&
        data->phase != MAGIC_SHIFT_PRESSED_WITH_PENDING) {
        k_mutex_unlock(&data->lock);
        return;
    }

    int64_t now = k_uptime_get();
    if (data->phase == MAGIC_SHIFT_WAITING_FOR_SECOND_TAP &&
        now < data->tap_deadline + MAGIC_SHIFT_TIMER_GRACE_MS) {
        int rc = schedule_tap_timer(data, data->tap_deadline + MAGIC_SHIFT_TIMER_GRACE_MS);
        if (rc < 0) {
            data->phase = MAGIC_SHIFT_IDLE;
            int release_rc = update_shift_state(data, now);
            if (release_rc < 0) {
                LOG_ERR("Failed to release Shift after timer error (err %d)", release_rc);
            }
        }
        k_mutex_unlock(&data->lock);
        return;
    }

    if (data->phase == MAGIC_SHIFT_WAITING_FOR_SECOND_TAP) {
        data->one_shot_deadline = now + data->config->one_shot_term_ms;
    }

    if (now >= data->one_shot_deadline) {
        if (data->phase == MAGIC_SHIFT_PRESSED_WITH_PENDING) {
            data->phase = MAGIC_SHIFT_PRESSED;
        } else {
            data->phase = MAGIC_SHIFT_IDLE;
            int rc = update_shift_state(data, now);
            if (rc < 0) {
                LOG_ERR("Failed to release expired one-shot Shift (err %d)", rc);
            }
        }
    } else {
        if (data->phase == MAGIC_SHIFT_WAITING_FOR_SECOND_TAP) {
            data->phase = MAGIC_SHIFT_PENDING_ONE_SHOT;
        }
        int rc = schedule_tap_timer(data, data->one_shot_deadline);
        if (rc < 0) {
            if (data->phase == MAGIC_SHIFT_PRESSED_WITH_PENDING) {
                data->phase = MAGIC_SHIFT_PRESSED;
            } else {
                data->phase = MAGIC_SHIFT_IDLE;
                int release_rc = update_shift_state(data, now);
                if (release_rc < 0) {
                    LOG_ERR("Failed to release Shift after timer error (err %d)", release_rc);
                }
            }
        }
    }
    k_mutex_unlock(&data->lock);
}

static int on_magic_shift_binding_pressed(struct zmk_behavior_binding *binding,
                                          struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_magic_shift_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);
    switch (data->phase) {
    case MAGIC_SHIFT_IDLE:
        data->phase = MAGIC_SHIFT_PRESSED;
        break;
    case MAGIC_SHIFT_WAITING_FOR_SECOND_TAP: {
        int64_t elapsed = event.timestamp - data->tap_released_at;
        if (elapsed >= 0 && elapsed <= data->config->double_tap_ms) {
            cancel_tap_timer(data);
            data->phase = MAGIC_SHIFT_SECOND_PRESSED;
            break;
        }

        data->phase = MAGIC_SHIFT_PRESSED_WITH_PENDING;
        data->one_shot_deadline = event.timestamp + data->config->one_shot_term_ms;
        break;
    }
    case MAGIC_SHIFT_PENDING_ONE_SHOT:
        data->phase = MAGIC_SHIFT_PRESSED_WITH_PENDING;
        break;
    case MAGIC_SHIFT_PRESSED:
    case MAGIC_SHIFT_SECOND_PRESSED:
    case MAGIC_SHIFT_PRESSED_WITH_PENDING:
        LOG_WRN("Ignoring unexpected magic-shift press at position %u", event.position);
        k_mutex_unlock(&data->lock);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    data->key_event = event;
    data->key_pressed_at = event.timestamp;
    data->chord_seen = false;
    int rc = update_shift_state(data, event.timestamp);
    if (rc < 0) {
        data->phase = MAGIC_SHIFT_IDLE;
        int release_rc = update_shift_state(data, event.timestamp);
        if (release_rc < 0) {
            LOG_ERR("Failed to release Shift after press error (err %d)", release_rc);
        }
        LOG_ERR("Failed to activate Shift (err %d)", rc);
    }
    k_mutex_unlock(&data->lock);
#if !defined(CONFIG_ZMK_SPLIT) || defined(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    zmk_behavior_system_enter_shift_state_changed(rc >= 0, event.timestamp);
#endif
    return rc < 0 ? rc : ZMK_BEHAVIOR_OPAQUE;
}

static int on_magic_shift_binding_released(struct zmk_behavior_binding *binding,
                                           struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_magic_shift_data *data = dev->data;
    int rc = 0;

    k_mutex_lock(&data->lock, K_FOREVER);
    data->key_event = event;
    bool is_tap = event.timestamp >= data->key_pressed_at &&
                  event.timestamp - data->key_pressed_at < data->config->hold_term_ms;
    switch (data->phase) {
    case MAGIC_SHIFT_PRESSED:
        if (is_tap && !data->chord_seen) {
            rc = start_one_shot_tap(data, event);
        } else {
            data->phase = MAGIC_SHIFT_IDLE;
            cancel_tap_timer(data);
            rc = update_shift_state(data, event.timestamp);
        }
        break;
    case MAGIC_SHIFT_PRESSED_WITH_PENDING:
        if (data->chord_seen) {
            data->phase = MAGIC_SHIFT_IDLE;
            cancel_tap_timer(data);
            rc = update_shift_state(data, event.timestamp);
        } else if (is_tap) {
            rc = start_one_shot_tap(data, event);
        } else if (k_uptime_get() >= data->one_shot_deadline) {
            data->phase = MAGIC_SHIFT_IDLE;
            cancel_tap_timer(data);
            rc = update_shift_state(data, event.timestamp);
        } else {
            data->phase = MAGIC_SHIFT_PENDING_ONE_SHOT;
            rc = schedule_tap_timer(data, data->one_shot_deadline);
            if (rc < 0) {
                data->phase = MAGIC_SHIFT_IDLE;
                int release_rc = update_shift_state(data, event.timestamp);
                if (release_rc < 0) {
                    LOG_ERR("Failed to release Shift after timer error (err %d)", release_rc);
                }
            }
        }
        break;
    case MAGIC_SHIFT_SECOND_PRESSED:
        cancel_tap_timer(data);
        data->phase = MAGIC_SHIFT_IDLE;
        rc = update_shift_state(data, event.timestamp);
        if (rc >= 0 && is_tap && !data->chord_seen) {
            rc = toggle_caps_lock(data, event.timestamp);
        }
        break;
    case MAGIC_SHIFT_IDLE:
    case MAGIC_SHIFT_WAITING_FOR_SECOND_TAP:
    case MAGIC_SHIFT_PENDING_ONE_SHOT:
        break;
    }
    if (rc < 0) {
        LOG_ERR("Magic-shift release action failed (err %d)", rc);
    }
    k_mutex_unlock(&data->lock);
#if !defined(CONFIG_ZMK_SPLIT) || defined(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    zmk_behavior_system_enter_shift_state_changed(false, event.timestamp);
#endif
    return rc < 0 ? rc : ZMK_BEHAVIOR_OPAQUE;
}

static bool is_same_key(const struct zmk_behavior_binding_event *first,
                        const struct zmk_behavior_binding_event *second) {
    return first->position == second->position
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
           && first->source == second->source
#endif
        ;
}

static int magic_shift_position_state_changed_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *event = as_zmk_position_state_changed(eh);
    if (event == NULL || !event->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    for (size_t i = 0; i < magic_shift_instance_count; i++) {
        struct behavior_magic_shift_data *data = magic_shift_instances[i];
        k_mutex_lock(&data->lock, K_FOREVER);

        if (data->phase == MAGIC_SHIFT_IDLE) {
            k_mutex_unlock(&data->lock);
            continue;
        }

        struct zmk_behavior_binding_event position_event = {
            .position = event->position,
            .timestamp = event->timestamp,
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
            .source = event->source,
#endif
        };
        if (is_same_key(&data->key_event, &position_event)) {
            k_mutex_unlock(&data->lock);
            continue;
        }

        /* Shift is armed before this key event; release it after keymap dispatch consumes it. */
        switch (data->phase) {
        case MAGIC_SHIFT_PRESSED:
        case MAGIC_SHIFT_SECOND_PRESSED:
            data->chord_seen = true;
            break;
        case MAGIC_SHIFT_PRESSED_WITH_PENDING:
            data->chord_seen = true;
            data->phase = MAGIC_SHIFT_PRESSED;
            cancel_tap_timer(data);
            break;
        case MAGIC_SHIFT_WAITING_FOR_SECOND_TAP:
        case MAGIC_SHIFT_PENDING_ONE_SHOT: {
            cancel_tap_timer(data);
            data->phase = MAGIC_SHIFT_IDLE;
            int rc = update_shift_state(data, event->timestamp);
            if (rc < 0) {
                LOG_ERR("Failed to consume one-shot Shift (err %d)", rc);
            }
            break;
        }
        case MAGIC_SHIFT_IDLE:
            break;
        }
        k_mutex_unlock(&data->lock);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(behavior_magic_shift, magic_shift_position_state_changed_listener);
ZMK_SUBSCRIPTION(behavior_magic_shift, zmk_position_state_changed);

static const struct behavior_driver_api behavior_magic_shift_driver_api = {
    .binding_pressed = on_magic_shift_binding_pressed,
    .binding_released = on_magic_shift_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

static int behavior_magic_shift_init(const struct device *dev) {
    struct behavior_magic_shift_data *data = dev->data;
    if (magic_shift_instance_count >= ARRAY_SIZE(magic_shift_instances)) {
        LOG_ERR("Too many magic-shift behavior instances");
        return -ENOMEM;
    }

    data->config = dev->config;
    k_mutex_init(&data->lock);
    k_work_init_delayable(&data->tap_timer, magic_shift_tap_timer_handler);
    data->phase = MAGIC_SHIFT_IDLE;
    magic_shift_instances[magic_shift_instance_count++] = data;
    return 0;
}

#define MAGIC_SHIFT_INST(n)                                                                        \
    static const struct behavior_magic_shift_config behavior_magic_shift_config_##n = {           \
        .hold_term_ms = DT_INST_PROP(n, hold_term_ms),                                             \
        .double_tap_ms = DT_INST_PROP(n, double_tap_ms),                                           \
        .one_shot_term_ms = DT_INST_PROP(n, one_shot_term_ms),                                     \
        .bindings = {                                                                              \
            ZMK_KEYMAP_EXTRACT_BINDING(MAGIC_SHIFT_BINDING_SHIFT, DT_DRV_INST(n)),                 \
            ZMK_KEYMAP_EXTRACT_BINDING(MAGIC_SHIFT_BINDING_CAPS, DT_DRV_INST(n)),                  \
        },                                                                                         \
    };                                                                                             \
    static struct behavior_magic_shift_data behavior_magic_shift_data_##n;                         \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_magic_shift_init, NULL,                                    \
                            &behavior_magic_shift_data_##n,                                        \
                            &behavior_magic_shift_config_##n, POST_KERNEL,                         \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_magic_shift_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MAGIC_SHIFT_INST)

#endif
