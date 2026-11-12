#include "chord_timing_store.h"

#include <Preferences.h>

#include "console_log.h"

static constexpr char kNs[] = "chordtime";
static constexpr char kDebounce[] = "debounce";
static constexpr char kRelease[] = "release";
static constexpr char kDelay[] = "repdelay";
static constexpr char kInterval[] = "repinterval";

static int key_count(Preferences &prefs) {
  int count = 0;
  if (prefs.isKey(kDebounce)) ++count;
  if (prefs.isKey(kRelease)) ++count;
  if (prefs.isKey(kDelay)) ++count;
  if (prefs.isKey(kInterval)) ++count;
  return count;
}

void chord_timing_store_load() {
  chord_timing_use_factory();
  Preferences prefs;
  if (!prefs.begin(kNs, true)) return;
  const int present = key_count(prefs);
  if (present == 0) {
    prefs.end();
    return;
  }
  if (present != 4) {
    prefs.end();
    console_println("時間參數不完整，改用出廠");
    return;
  }
  ChordTiming timing;
  timing.debounce_ms = prefs.getUShort(kDebounce, 0);
  timing.release_guard_ms = prefs.getUShort(kRelease, 0);
  timing.repeat_delay_ms = prefs.getUShort(kDelay, 0);
  timing.repeat_interval_ms = prefs.getUShort(kInterval, 0);
  prefs.end();
  char err[96];
  err[0] = '\0';
  if (!chord_timing_accept(timing, err, sizeof(err))) {
    console_println("時間參數不再合法，改用出廠");
    return;
  }
  chord_timing_apply(timing);
}

bool chord_timing_store_save(const ChordTiming &timing) {
  Preferences prefs;
  if (!prefs.begin(kNs, false)) return false;
  const bool ok = prefs.putUShort(kDebounce, timing.debounce_ms) == sizeof(uint16_t) &&
                  prefs.putUShort(kRelease, timing.release_guard_ms) == sizeof(uint16_t) &&
                  prefs.putUShort(kDelay, timing.repeat_delay_ms) == sizeof(uint16_t) &&
                  prefs.putUShort(kInterval, timing.repeat_interval_ms) == sizeof(uint16_t);
  prefs.end();
  return ok;
}

bool chord_timing_store_clear() {
  Preferences prefs;
  if (!prefs.begin(kNs, false)) return false;
  bool ok = true;
  const char *keys[] = {kDebounce, kRelease, kDelay, kInterval};
  for (const char *key : keys) {
    if (prefs.isKey(key) && !prefs.remove(key)) ok = false;
  }
  prefs.end();
  return ok;
}
