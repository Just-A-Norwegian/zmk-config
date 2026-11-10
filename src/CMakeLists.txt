if (NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    zephyr_library_sources(nb_layer_persist.c)
endif ()

zephyr_library_sources(behavior_magic_shift.c)
if (NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    zephyr_library_sources(behavior_shortcut_mods.c)
endif ()
