#pragma once

#include <stddef.h>
#include <stdint.h>

#include "settings.h"

// Factory numbers stay in settings.h. This slot is what the scan and the
// chord engine read. It starts as those constants. The hotspot stores another
// set; chord_timing_use_factory() puts the constants back.
// The bounce window is not its own field. It is the running debounce plus
// (kChordBounceMs - kChordDebounceMs), and the release guard must cover it.
// Settle and the tap gap stay compile-time.

struct ChordTiming {
  uint16_t debounce_ms;
  uint16_t release_guard_ms;
  uint16_t repeat_delay_ms;
  uint16_t repeat_interval_ms;
};

// Caps the hotspot may store. debounce_max is kSwitchDebounceMs - 1.
// repeat_delay_min is kChordRepeatSettleMs + 1.
// repeat_interval_min is kChordTapGapMs * 2 + 1.
struct ChordTimingLimits {
  uint16_t debounce_max;
  uint16_t release_max;
  uint16_t bounce_margin_ms;
  uint16_t repeat_delay_min;
  uint16_t repeat_delay_max;
  uint16_t repeat_interval_min;
  uint16_t repeat_interval_max;
};

ChordTiming chord_timing_factory();
ChordTimingLimits chord_timing_limits();

// The value the scan and the engine use on the next poll.
const ChordTiming &chord_timing_current();
uint32_t chord_timing_bounce_ms();

// False leaves *err set when err_len > 0. Does not change the running value.
bool chord_timing_accept(const ChordTiming &timing, char *err, size_t err_len);

// Caller has already accepted the value, or is restoring the factory set.
void chord_timing_apply(const ChordTiming &timing);
void chord_timing_use_factory();
