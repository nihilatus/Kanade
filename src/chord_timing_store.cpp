#include "chord_timing_store.h"

#include <Preferences.h>

#include "chord_timing_nv.h"
#include "console_log.h"

static constexpr char kNs[] = "chordtime";
static constexpr char kCfg[] = "cfg";
static constexpr char kDebounce[] = "debounce";
static constexpr char kRelease[] = "release";
static constexpr char kDelay[] = "repdelay";
static constexpr char kInterval[] = "repinterval";

static int legacy_key_count(Preferences &prefs) {
  int count = 0;
  if (prefs.isKey(kDebounce)) ++count;
  if (prefs.isKey(kRelease)) ++count;
  if (prefs.isKey(kDelay)) ++count;
  if (prefs.isKey(kInterval)) ++count;
  return count;
}

static void remove_legacy_keys(Preferences &prefs) {
  const char *keys[] = {kDebounce, kRelease, kDelay, kInterval};
  for (const char *key : keys) {
    if (prefs.isKey(key)) prefs.remove(key);
  }
}

static bool write_cfg(Preferences &prefs, const ChordTiming &timing) {
  ChordTimingNv nv;
  chord_timing_nv_pack(timing, &nv);
  return prefs.putBytes(kCfg, &nv, sizeof(nv)) == sizeof(nv);
}

static bool accept_or_log(const ChordTiming &timing) {
  char err[96];
  err[0] = '\0';
  if (!chord_timing_accept(timing, err, sizeof(err))) {
    console_println("時間參數不再合法，改用出廠");
    return false;
  }
  return true;
}

void chord_timing_store_load() {
  chord_timing_use_factory();
  Preferences prefs;
  if (!prefs.begin(kNs, true)) return;

  if (prefs.isKey(kCfg)) {
    uint8_t raw[sizeof(ChordTimingNv)];
    const size_t got = prefs.getBytes(kCfg, raw, sizeof(raw));
    prefs.end();
    ChordTiming timing;
    if (!chord_timing_nv_unpack(raw, got, &timing)) {
      console_println(got != sizeof(ChordTimingNv) ? "時間參數不完整，改用出廠"
                                                   : "時間參數不再合法，改用出廠");
      return;
    }
    if (!accept_or_log(timing)) return;
    chord_timing_apply(timing);
    return;
  }

  const int present = legacy_key_count(prefs);
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
  if (!accept_or_log(timing)) return;
  chord_timing_apply(timing);

  Preferences write;
  if (!write.begin(kNs, false)) return;
  if (write_cfg(write, timing)) remove_legacy_keys(write);
  write.end();
}

bool chord_timing_store_save(const ChordTiming &timing) {
  Preferences prefs;
  if (!prefs.begin(kNs, false)) return false;
  const bool ok = write_cfg(prefs, timing);
  if (ok) remove_legacy_keys(prefs);
  prefs.end();
  return ok;
}

bool chord_timing_store_clear() {
  Preferences prefs;
  if (!prefs.begin(kNs, false)) return false;
  bool ok = true;
  if (prefs.isKey(kCfg) && !prefs.remove(kCfg)) ok = false;
  const char *keys[] = {kDebounce, kRelease, kDelay, kInterval};
  for (const char *key : keys) {
    if (prefs.isKey(key) && !prefs.remove(key)) ok = false;
  }
  prefs.end();
  return ok;
}
