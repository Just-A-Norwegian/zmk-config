/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

void zmk_behavior_system_enter_shift_state_changed(bool pressed, int64_t timestamp);
