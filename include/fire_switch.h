#pragma once

#include <stdint.h>

#include "chord_core.h"

// GPIO21, INPUT_PULLUP, closed to GND. Open is ReleaseAll. Closed is FirstUp.
// The debounced level is the live send timing. A change applies in this
// process and does not restart. The mode is not written to NVS or LittleFS.
// A failed pin config counts as open (release-all).

constexpr ChordFireMode fire_mode_from_closed(bool closed) {
  return closed ? ChordFireMode::FirstUp : ChordFireMode::ReleaseAll;
}

void fire_switch_begin();
ChordFireMode fire_switch_mode();
// True once when the debounced level changes. The new mode is then
// fire_switch_mode(). Uses kSwitchDebounceMs.
bool fire_switch_poll(uint32_t now_ms);
