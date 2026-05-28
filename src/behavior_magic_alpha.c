#define DT_DRV_COMPAT zmk_behavior_magic_alpha

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <dt-bindings/zmk/modifiers.h>
#include <zmk/behavior.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/magic_mode.h>
#include <zmk/matrix.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static uint32_t pressed_keycodes[ZMK_KEYMAP_LEN];

static uint32_t magic_alpha_resolve_encoded(uint32_t encoded)
{
    if (!zmk_magic_mode_is_shifted())
    {
        return encoded;
    }

    return APPLY_MODS(SELECT_MODS(encoded) | MOD_LSFT, STRIP_MODS(encoded));
}

static int on_magic_alpha_pressed(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event)
{
    uint32_t encoded = magic_alpha_resolve_encoded(binding->param1);

    if (event.position < ZMK_KEYMAP_LEN)
    {
        pressed_keycodes[event.position] = encoded;
    }

    return raise_zmk_keycode_state_changed_from_encoded(encoded, true, event.timestamp);
}

static int on_magic_alpha_released(struct zmk_behavior_binding *binding,
                                   struct zmk_behavior_binding_event event)
{
    uint32_t encoded = binding->param1;

    if (event.position < ZMK_KEYMAP_LEN && pressed_keycodes[event.position] != 0)
    {
        encoded = pressed_keycodes[event.position];
        pressed_keycodes[event.position] = 0;
    }
    else
    {
        encoded = magic_alpha_resolve_encoded(encoded);
    }

    return raise_zmk_keycode_state_changed_from_encoded(encoded, false, event.timestamp);
}

static const struct behavior_driver_api behavior_magic_alpha_driver_api = {
    .binding_pressed = on_magic_alpha_pressed,
    .binding_released = on_magic_alpha_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define MAGIC_ALPHA_INST(n)                                         \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL, \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,    \
                            &behavior_magic_alpha_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MAGIC_ALPHA_INST)

#endif
