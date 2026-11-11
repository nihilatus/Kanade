#pragma once

#include <stdint.h>

#include "output_policy.h"

// GPIO16, INPUT_PULLUP, closed to GND. The boot level is the mode until restart.
// output_switch_poll debounces with kSwitchDebounceMs. A stable level that
// differs from boot makes output_switch_restart_requested() true; the caller
// releases keys and restarts. The mode is not written to NVS or LittleFS.

void output_switch_begin();
OutputMode output_switch_mode();
void output_switch_poll(uint32_t now_ms);
bool output_switch_restart_requested();
