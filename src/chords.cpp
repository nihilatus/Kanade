#include "chords.h"

#include <string.h>

#include "buttons.h"
#include "hid_keyboard.h"

// required ⊆ max_chord ⊆ required ∪ optional.
// Prefer more required bits, then fewer optional bits, then smaller id.
// See docs/chord-map-machine.md.

struct ChordRule {
  uint16_t id;
  uint16_t required;
  uint16_t optional;
  const char *kana;
  const char *output;
};

static const ChordRule kChordMap[] = {
#include "chord_map.inc"
};

static constexpr size_t kChordMapCount = sizeof(kChordMap) / sizeof(kChordMap[0]);

// After the debounced mask is empty, wait this long before firing so a
// release bounce cannot split the chord or send it twice.
static constexpr uint32_t kReleaseGuardMs = 25;

enum class Phase : uint8_t { Idle, Holding, ReleaseGuard };

static Phase g_phase = Phase::Idle;
static uint16_t g_max_chord = 0;
static uint8_t g_max_count = 0;
static uint32_t g_guard_since = 0;

static_assert(BTN_COUNT <= 16, "buttons_mask is uint16_t");

static uint8_t popcount16(uint16_t value) {
  return static_cast<uint8_t>(__builtin_popcount(static_cast<unsigned>(value)));
}

static void note_snapshot(uint16_t mask) {
  const uint8_t count = popcount16(mask);
  // Largest set wins. Same size: keep the later snapshot.
  if (count > g_max_count || (count == g_max_count && mask != g_max_chord)) {
    g_max_chord = mask;
    g_max_count = count;
  }
}

static void format_mask(uint16_t mask, char *buf, size_t buflen) {
  if (buflen == 0) return;
  buf[0] = '\0';
  size_t used = 0;
  bool any = false;
  for (uint8_t i = 0; i < BTN_COUNT; ++i) {
    if ((mask & static_cast<uint16_t>(1u << i)) == 0) continue;
    const char *name = kButtons[i].name;
    const size_t len = strlen(name);
    const size_t extra = any ? 1 : 0;
    if (used + extra + len + 1 > buflen) break;
    if (any) buf[used++] = '+';
    memcpy(buf + used, name, len);
    used += len;
    buf[used] = '\0';
    any = true;
  }
  if (!any) {
    strncpy(buf, "-", buflen);
    buf[buflen - 1] = '\0';
  }
}

static void log_chord(const char *kind, uint16_t id, uint16_t mask, const char *kana,
                      const char *output) {
#if ARDUINO_USB_CDC_ON_BOOT
  char keys[80];
  format_mask(mask, keys, sizeof(keys));
  Serial.print(F("chord "));
  Serial.print(kind);
  Serial.print(F(" id="));
  Serial.print(id);
  Serial.print(F(" keys="));
  Serial.print(keys);
  Serial.print(F(" kana="));
  Serial.print(kana != nullptr ? kana : "");
  Serial.print(F(" output="));
  Serial.println(output != nullptr ? output : "");
#else
  (void)kind;
  (void)id;
  (void)mask;
  (void)kana;
  (void)output;
#endif
}

static int find_rule(uint16_t mask) {
  int best = -1;
  int best_req = -1;
  int best_opt = 255;
  uint16_t best_id = 0xFFFF;

  for (size_t i = 0; i < kChordMapCount; ++i) {
    const ChordRule &row = kChordMap[i];
    if ((row.required & mask) != row.required) continue;
    const uint16_t allowed = static_cast<uint16_t>(row.required | row.optional);
    if ((mask & static_cast<uint16_t>(~allowed)) != 0) continue;

    const int req_n = popcount16(row.required);
    const int opt_n = popcount16(row.optional);
    const bool better = best < 0 || req_n > best_req ||
                        (req_n == best_req && opt_n < best_opt) ||
                        (req_n == best_req && opt_n == best_opt && row.id < best_id);
    if (!better) continue;
    best = static_cast<int>(i);
    best_req = req_n;
    best_opt = opt_n;
    best_id = row.id;
  }
  return best;
}

static void fire(uint16_t mask) {
  if (mask == 0) return;

  const int index = find_rule(mask);
  if (index < 0) {
    log_chord("no-match", 0, mask, "", "");
    return;
  }

  const ChordRule &row = kChordMap[index];
  log_chord("send", row.id, mask, row.kana, row.output);
  if (row.output == nullptr || row.output[0] == '\0') return;
  if (!hid_keyboard_ready()) return;
  hid_type_recipe(row.output);
}

void chords_begin() {
  g_phase = Phase::Idle;
  g_max_chord = 0;
  g_max_count = 0;
  g_guard_since = 0;
}

void chords_poll() {
  const uint16_t mask = buttons_mask();
  const uint32_t now = millis();

  switch (g_phase) {
    case Phase::Idle:
      if (mask != 0) {
        g_phase = Phase::Holding;
        g_max_chord = 0;
        g_max_count = 0;
        note_snapshot(mask);
      }
      break;

    case Phase::Holding:
      if (mask == 0) {
        g_phase = Phase::ReleaseGuard;
        g_guard_since = now;
      } else {
        note_snapshot(mask);
      }
      break;

    case Phase::ReleaseGuard:
      if (mask != 0) {
        // Bounce or a finger that was not actually up. Keep max_chord.
        g_phase = Phase::Holding;
        note_snapshot(mask);
      } else if ((now - g_guard_since) >= kReleaseGuardMs) {
        const uint16_t chord = g_max_chord;
        g_phase = Phase::Idle;
        g_max_chord = 0;
        g_max_count = 0;
        fire(chord);
      }
      break;
  }
}
