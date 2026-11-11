if (NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    zephyr_library_sources(nb_layer_persist.c)
endif ()

zephyr_library_sources(behavior_magic_shift.c)
if (NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    # Activate System before the keymap listener processes the triggering key.
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/behavior_system_enter.c)
endif ()
if (NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    zephyr_library_sources(behavior_shortcut_mods.c)
endif ()
