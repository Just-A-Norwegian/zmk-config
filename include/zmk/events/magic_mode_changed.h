#pragma once

#include <stdint.h>
#include <zmk/event_manager.h>

struct zmk_magic_mode_changed
{
    uint8_t mode;
};

ZMK_EVENT_DECLARE(zmk_magic_mode_changed);
