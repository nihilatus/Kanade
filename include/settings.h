#pragma once

#include <stddef.h>
#include <stdint.h>

// Every tunable number lives here. Change the value, then rebuild.
// USB VID/PID stay in src/hid_keyboard.cpp.
// SSID, passphrase, and WIFI_API_TOKEN stay in include/secrets.h.
// Boot fire mode CHORD_FIRE_DEFAULT_FIRST stays in platformio.ini.
// No Arduino types: host tests and the practice-page wasm include this too.

// --- Key timing ---
// How long a raw GPIO level must hold before the chord mask changes.
// Raise this when a noisy switch splits one chord or repeats a key.
static constexpr uint8_t kChordDebounceMs = 8;
// ReleaseAll waits this long after the debounced mask stays empty.
// Raise this when a release bounce sends the chord twice.
static constexpr uint32_t kChordReleaseGuardMs = 10;
// A subset or superset of the peak is the same chord only until this gap.
// The engine sees debounced edges, so a contact that re-closes as the release
// is accepted shows up about one debounce later. The practice page has no
// GPIO debounce; rolls of 15 ms already clear the guard, and 1–9 ms drops a
// nested chord such as あ after か. This value keeps that bounce and still
// splits a nested mask that arrives with the guard already elapsed.
static constexpr uint32_t kChordBounceMs = 10;
static_assert(kChordBounceMs > kChordDebounceMs,
              "bounce window must cover a debounced return");
static_assert(kChordBounceMs <= kChordReleaseGuardMs,
              "nested chords must split once the release guard has elapsed");
// Gap after one HID report before the next report in a recipe may go out.
// Raise this when the host merges a repeated letter such as "nn" into one key.
static constexpr uint8_t kChordTapGapMs = 12;
// Stored on a modifier press that has no gap of its own. The HID queue
// substitutes kChordTapGapMs when it sees this.
static constexpr uint8_t kChordGapNone = 0;
// Rows with ChordRule::repeat: first tap after the peak stops growing, then
// repeat. The delay is measured from the same moment as the settle. One poll
// sends at most one tap, so a late poll does not catch up.
// These three replace the host's key-repeat rate for repeat=allow rows.
static constexpr uint16_t kChordRepeatSettleMs = 50;
static constexpr uint16_t kChordRepeatDelayMs = 400;
static constexpr uint8_t kChordRepeatIntervalMs = 33;
static_assert(kChordRepeatDelayMs > kChordRepeatSettleMs,
              "repeat starts after the first tap");
static_assert(kChordRepeatIntervalMs > kChordTapGapMs * 2,
              "repeat gap must outlast one press and release");

// --- Recipe and table capacity ---
// Chord keys. Bit i of the mask is key i, order LL LU LR LD LT0 LT1 LT2
// RL RU RR RD RT0 RT1 RT2. Changing this also means a new kButtonPins row.
static constexpr int kChordKeyCount = 14;
// Events stored for one recipe. A recipe that does not fit is not sent.
static constexpr size_t kChordRecipeCap = 96;
// Skipped tokens whose text is kept. Further skips are counted without text.
static constexpr size_t kChordSkipCap = 32;
// One output field, in bytes. A longer token is a too_long skip, not a send.
// The config page's output box must use this same limit.
static constexpr size_t kChordTokenMax = 63;
// Bytes of a skipped token kept for the log, not counting the NUL.
static constexpr size_t kChordSkipText = 23;
// Kana column of a saved row, in bytes.
static constexpr size_t kKanaMax = 47;
// Live table rows, including the factory copy. The practice page uses
// kChordWasmRuleCap instead: that page is not limited by the device pool.
static constexpr size_t kChordsMaxRows = 256;
// Largest chords.json the config AP will parse.
static constexpr size_t kChordsJsonMax = 48 * 1024;
// PSRAM pool for the live table's kana and output strings.
static constexpr size_t kPoolSize = 30 * 1024;
// HID reports waiting to be sent. The last slot is only for release-all.
static constexpr size_t kQueueCap = 192;
// Longest romaji string hid_type_romaji will queue, in bytes.
static constexpr size_t kRomajiMax = 80;
static_assert(kRomajiMax <= kChordRecipeCap, "romaji shares the recipe stroke buffer");
// Rules the practice page may load. Larger than the device table on purpose.
static constexpr uint32_t kChordWasmRuleCap = 512;

// --- Pins ---
// One GPIO per chord key, same order as kChordKeyCount. Active-low to GND.
// GPIO3 is skipped (strapping). Keep docs/pinout.md in step when this changes.
static constexpr int kButtonPins[kChordKeyCount] = {
    1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 21,
};
// Closed to GND enables the SoftAP. Not one of the chord keys.
static constexpr int kWifiEnablePin = 15;

constexpr bool settings_pin_is_chord_key(int pin) {
  for (int i = 0; i < kChordKeyCount; ++i) {
    if (kButtonPins[i] == pin) return true;
  }
  return false;
}
static_assert(!settings_pin_is_chord_key(kWifiEnablePin),
              "wifi switch is not a chord key");

// --- Hotspot ---
// SoftAP address. kConfigOrigin must be "http://" plus these four octets.
static constexpr uint8_t kApIp0 = 192;
static constexpr uint8_t kApIp1 = 168;
static constexpr uint8_t kApIp2 = 4;
static constexpr uint8_t kApIp3 = 1;
static constexpr char kConfigOrigin[] = "http://192.168.4.1";

constexpr int settings_read_octet(const char *text, size_t &index) {
  int value = 0;
  int digits = 0;
  while (text[index] >= '0' && text[index] <= '9') {
    value = value * 10 + (text[index] - '0');
    ++index;
    ++digits;
    if (digits > 3 || value > 255) return -1;
  }
  if (digits == 0) return -1;
  return value;
}

constexpr bool settings_origin_matches_ip() {
  const char *text = kConfigOrigin;
  const char prefix[] = "http://";
  size_t index = 0;
  for (size_t i = 0; prefix[i] != '\0'; ++i) {
    if (text[index++] != prefix[i]) return false;
  }
  const int o0 = settings_read_octet(text, index);
  if (o0 < 0 || text[index++] != '.') return false;
  const int o1 = settings_read_octet(text, index);
  if (o1 < 0 || text[index++] != '.') return false;
  const int o2 = settings_read_octet(text, index);
  if (o2 < 0 || text[index++] != '.') return false;
  const int o3 = settings_read_octet(text, index);
  if (o3 < 0 || text[index] != '\0') return false;
  return o0 == kApIp0 && o1 == kApIp1 && o2 == kApIp2 && o3 == kApIp3;
}
static_assert(settings_origin_matches_ip(), "kConfigOrigin must be http:// plus kApIp");

// Longer than key debounce so a contact bounce does not flap the radio.
static constexpr uint32_t kWifiSwitchDebounceMs = 50;
static_assert(kWifiSwitchDebounceMs > kChordDebounceMs,
              "wifi switch debounce must outlast a key bounce");
// Bytes moved on one config_server_poll. A slow client must not stall the scan
// for a whole bounce window.
static constexpr size_t kHttpSlice = 512;
// Drop a config connection that makes no progress for this long.
static constexpr uint32_t kHttpStallMs = 5000;
// Request header block. A larger block is rejected.
static constexpr size_t kHttpHeaderMax = 1536;
// Body bytes discarded before answering. A normal save is under kChordsJsonMax.
static constexpr long kDrainMax = 256 * 1024;
static_assert(kChordsJsonMax < static_cast<size_t>(kDrainMax),
              "a chord save must fit under the drain cap");
// POST /api/fire body. The mode JSON is a few dozen bytes.
static constexpr size_t kFireBodyMax = 256;
