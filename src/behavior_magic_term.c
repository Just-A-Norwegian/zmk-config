#define DT_DRV_COMPAT zmk_behavior_magic_term

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/magic_mode.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_magic_term_pressed(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event)
{
    int err = zmk_magic_mode_clear_shift_word();
    if (err < 0)
    {
        return err;
    }

    return raise_zmk_keycode_state_changed_from_encoded(binding->param1, true, event.timestamp);
}

static int on_magic_term_released(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event)
{
    return raise_zmk_keycode_state_changed_from_encoded(binding->param1, false, event.timestamp);
}

static const struct behavior_driver_api behavior_magic_term_driver_api = {
    .binding_pressed = on_magic_term_pressed,
    .binding_released = on_magic_term_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define MAGIC_TERM_INST(n)                                          \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL, \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,    \
                            &behavior_magic_term_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MAGIC_TERM_INST)

#endif
