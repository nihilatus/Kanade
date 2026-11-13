#include "chord_timing_nv.h"

#include <string.h>

void chord_timing_nv_pack(const ChordTiming &timing, ChordTimingNv *out) {
  out->version = kChordTimingNvVersion;
  out->reserved = 0;
  out->debounce_ms = timing.debounce_ms;
  out->release_guard_ms = timing.release_guard_ms;
  out->repeat_delay_ms = timing.repeat_delay_ms;
  out->repeat_interval_ms = timing.repeat_interval_ms;
}

bool chord_timing_nv_unpack(const void *bytes, size_t len, ChordTiming *out) {
  if (bytes == nullptr || out == nullptr) return false;
  if (len != sizeof(ChordTimingNv)) return false;
  ChordTimingNv nv;
  memcpy(&nv, bytes, sizeof(nv));
  if (nv.version != kChordTimingNvVersion) return false;
  out->debounce_ms = nv.debounce_ms;
  out->release_guard_ms = nv.release_guard_ms;
  out->repeat_delay_ms = nv.repeat_delay_ms;
  out->repeat_interval_ms = nv.repeat_interval_ms;
  return true;
}
