#pragma once

#include <stdint.h>

#include "output_policy.h"

// GPIO16, INPUT_PULLUP, closed to GND. The boot level is the mode until restart.
// output_switch_boot_closed() latches that level once, before app_main, after
// the pull-up has settled. A failed pin config counts as open (USB).
// output_switch_begin() uses the latch and does not read again.
// output_switch_poll still reads the pin and debounces with kSwitchDebounceMs.
// A stable level that differs from boot makes output_switch_restart_requested()
// true; the caller releases keys and restarts. The mode is not written to NVS
// or LittleFS.

// Latched boot level. Closed to GND is true. Safe to call before setup().
bool output_switch_boot_closed();
void output_switch_begin();
OutputMode output_switch_mode();
void output_switch_poll(uint32_t now_ms);
bool output_switch_restart_requested();
