#pragma once

#include <stddef.h>
#include <stdint.h>

#include "chord_timing.h"

// Packed NVS blob for chord_timing_store. Host tests cover pack/unpack;
// Preferences I/O stays in chord_timing_store.cpp.

static constexpr uint8_t kChordTimingNvVersion = 1;

#pragma pack(push, 1)
struct ChordTimingNv {
  uint8_t version;
  uint8_t reserved;
  uint16_t debounce_ms;
  uint16_t release_guard_ms;
  uint16_t repeat_delay_ms;
  uint16_t repeat_interval_ms;
};
#pragma pack(pop)

void chord_timing_nv_pack(const ChordTiming &timing, ChordTimingNv *out);

// False when len or version is wrong. Does not call chord_timing_accept.
bool chord_timing_nv_unpack(const void *bytes, size_t len, ChordTiming *out);
