#pragma once

#include <stdbool.h>
#include <stdint.h>

enum zmk_magic_mode
{
    ZMK_MAGIC_MODE_NORMAL = 0,
    ZMK_MAGIC_MODE_SHIFT_WORD = 1,
    ZMK_MAGIC_MODE_CAPS = 2,
};

enum zmk_magic_mode zmk_magic_mode_get(void);
int zmk_magic_mode_set(enum zmk_magic_mode mode);
int zmk_magic_mode_handle_tap(int64_t timestamp);
int zmk_magic_mode_clear_shift_word(void);
void zmk_magic_mode_break_tap_sequence(void);
bool zmk_magic_mode_is_shifted(void);
