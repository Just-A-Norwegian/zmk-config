#define DT_DRV_COMPAT zmk_behavior_magic_mode_tap

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/magic_mode.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_magic_mode_tap_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event)
{
    ARG_UNUSED(binding);
    return zmk_magic_mode_handle_tap(event.timestamp);
}

static int on_magic_mode_tap_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event)
{
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_magic_mode_tap_driver_api = {
    .binding_pressed = on_magic_mode_tap_pressed,
    .binding_released = on_magic_mode_tap_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define MAGIC_MODE_TAP_INST(n)                                      \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL, \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,    \
                            &behavior_magic_mode_tap_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MAGIC_MODE_TAP_INST)

#endif
