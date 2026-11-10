#include "chords.h"

#include <string.h>

#include <vector>

#include <ArduinoJson.h>
#include <LittleFS.h>

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

static const ChordRule kFactoryMap[] = {
#include "chord_map.inc"
};

static constexpr size_t kFactoryCount = sizeof(kFactoryMap) / sizeof(kFactoryMap[0]);
static constexpr size_t kMaxChords = 400;
static constexpr size_t kMaxKanaBytes = 48;
static constexpr size_t kMaxOutputBytes = 64;
static constexpr size_t kMaxJsonBytes = 65536;
static const char *kChordFile = "/chords.json";

struct LiveChord {
  uint16_t id;
  uint16_t required;
  uint16_t optional;
  String kana;
  String output;
};

static std::vector<LiveChord> g_live;
static bool g_from_file = false;
static bool g_fs_ready = false;

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

static int key_index(const char *name) {
  if (name == nullptr) return -1;
  for (uint8_t i = 0; i < BTN_COUNT; ++i) {
    if (strcmp(name, kButtons[i].name) == 0) return i;
  }
  return -1;
}

static bool text_ok(const char *text, size_t max_bytes, bool ascii_only) {
  if (text == nullptr) return false;
  size_t n = 0;
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p) {
    if (*p < 0x20 || *p == 0x7F) return false;
    if (ascii_only && *p > 0x7E) return false;
    if (++n > max_bytes) return false;
  }
  return true;
}

static void load_factory() {
  g_live.clear();
  g_live.reserve(kFactoryCount);
  for (size_t i = 0; i < kFactoryCount; ++i) {
    const ChordRule &row = kFactoryMap[i];
    LiveChord live;
    live.id = row.id;
    live.required = row.required;
    live.optional = row.optional;
    live.kana = row.kana != nullptr ? row.kana : "";
    live.output = row.output != nullptr ? row.output : "";
    g_live.push_back(live);
  }
  g_from_file = false;
}

static bool read_key_array(JsonArray arr, uint16_t &mask, String &error, const char *label) {
  mask = 0;
  if (arr.isNull()) {
    error = String(label) + " 必須是按鍵陣列";
    return false;
  }
  for (JsonVariant item : arr) {
    if (!item.is<const char *>()) {
      error = String(label) + " 裡有不是文字的項目";
      return false;
    }
    const int bit = key_index(item.as<const char *>());
    if (bit < 0) {
      error = String("未知按鍵: ") + item.as<const char *>();
      return false;
    }
    mask = static_cast<uint16_t>(mask | (1u << bit));
  }
  return true;
}

static bool parse_chord_json(const String &body, std::vector<LiveChord> &out, String &error) {
  out.clear();
  if (body.length() > kMaxJsonBytes) {
    error = "設定太大";
    return false;
  }

  JsonDocument doc;
  const DeserializationError json_error = deserializeJson(doc, body);
  if (json_error) {
    error = "JSON 無法解析";
    return false;
  }
  JsonArray chords = doc["chords"].as<JsonArray>();
  if (chords.isNull()) {
    error = "缺少 chords 陣列";
    return false;
  }
  if (chords.size() > kMaxChords) {
    error = "和弦列數超過上限";
    return false;
  }

  for (JsonObject row : chords) {
    LiveChord live;
    const int raw_id = row["id"] | 0;
    if (raw_id <= 0 || raw_id > 65535) {
      error = "id 必須是 1 到 65535";
      return false;
    }
    live.id = static_cast<uint16_t>(raw_id);
    for (const LiveChord &earlier : out) {
      if (earlier.id == live.id) {
        error = String("id 重複: ") + live.id;
        return false;
      }
    }

    const char *kana = "";
    const char *output = "";
    if (!row["kana"].isNull()) {
      if (!row["kana"].is<const char *>()) {
        error = "假名必須是文字";
        return false;
      }
      kana = row["kana"].as<const char *>();
    }
    if (!row["output"].isNull()) {
      if (!row["output"].is<const char *>()) {
        error = "output 必須是文字";
        return false;
      }
      output = row["output"].as<const char *>();
    }
    if (!text_ok(kana, kMaxKanaBytes, false)) {
      error = "假名過長或含有控制字元";
      return false;
    }
    if (!text_ok(output, kMaxOutputBytes, true)) {
      error = "output 只能是 64 個以內的英數與符號";
      return false;
    }
    live.kana = kana;
    live.output = output;

    if (!read_key_array(row["required"].as<JsonArray>(), live.required, error, "required")) {
      return false;
    }
    if (!read_key_array(row["optional"].as<JsonArray>(), live.optional, error, "optional")) {
      return false;
    }
    if (live.required == 0) {
      error = "每一列至少要有一個必按鍵";
      return false;
    }
    if ((live.required & live.optional) != 0) {
      error = "同一顆鍵不能同時是必按與可選";
      return false;
    }
    out.push_back(live);
  }
  return true;
}

static void add_key_array(JsonArray arr, uint16_t mask) {
  for (uint8_t i = 0; i < BTN_COUNT; ++i) {
    if ((mask & static_cast<uint16_t>(1u << i)) != 0) arr.add(kButtons[i].name);
  }
}

static bool write_chord_file(const String &body, String &error) {
  if (!g_fs_ready) {
    error = "快閃檔案系統沒有掛載";
    return false;
  }
  File file = LittleFS.open(kChordFile, "w");
  if (!file) {
    error = "無法寫入和弦檔";
    return false;
  }
  const size_t wrote = file.print(body);
  file.close();
  if (wrote != body.length()) {
    LittleFS.remove(kChordFile);
    error = "和弦檔寫入不完整";
    return false;
  }
  return true;
}

static bool export_live(const std::vector<LiveChord> &rows, const char *source, String &out) {
  JsonDocument doc;
  doc["source"] = source;
  JsonArray chords = doc["chords"].to<JsonArray>();
  for (const LiveChord &row : rows) {
    JsonObject item = chords.add<JsonObject>();
    item["id"] = row.id;
    item["kana"] = row.kana;
    item["output"] = row.output;
    add_key_array(item["required"].to<JsonArray>(), row.required);
    add_key_array(item["optional"].to<JsonArray>(), row.optional);
  }
  out = "";
  const size_t n = serializeJson(doc, out);
  return n > 0 && out.length() == n;
}

const char *chords_source() {
  return g_from_file ? "saved" : "factory";
}

bool chords_export_json(String &out) {
  return export_live(g_live, chords_source(), out);
}

bool chords_save_json(const String &body, String &error) {
  std::vector<LiveChord> parsed;
  if (!parse_chord_json(body, parsed, error)) return false;
  String canonical;
  if (!export_live(parsed, "saved", canonical)) {
    error = "無法組成要儲存的 JSON";
    return false;
  }
  if (!write_chord_file(canonical, error)) return false;
  g_live.swap(parsed);
  g_from_file = true;
  return true;
}

bool chords_reset_saved(String &error) {
  if (g_fs_ready && LittleFS.exists(kChordFile) && !LittleFS.remove(kChordFile)) {
    error = "無法刪除已存的和弦檔";
    return false;
  }
  load_factory();
  return true;
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

  for (size_t i = 0; i < g_live.size(); ++i) {
    const LiveChord &row = g_live[i];
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

  const LiveChord &row = g_live[index];
  log_chord("send", row.id, mask, row.kana.c_str(), row.output.c_str());
  if (row.output.length() == 0) return;
  if (!hid_keyboard_ready()) return;
  hid_type_recipe(row.output.c_str());
}

void chords_begin() {
  g_phase = Phase::Idle;
  g_max_chord = 0;
  g_max_count = 0;
  g_guard_since = 0;

  g_fs_ready = LittleFS.begin(true, "/littlefs", 10, "spiffs");
  load_factory();
  if (!g_fs_ready) {
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.println(F("chord table: factory (no filesystem)"));
#endif
    return;
  }
  if (!LittleFS.exists(kChordFile)) {
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.println(F("chord table: factory"));
#endif
    return;
  }

  File file = LittleFS.open(kChordFile, "r");
  String body = file ? file.readString() : String();
  if (file) file.close();
  std::vector<LiveChord> parsed;
  String error;
  if (parse_chord_json(body, parsed, error)) {
    g_live.swap(parsed);
    g_from_file = true;
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.println(F("chord table: saved"));
#endif
    return;
  }
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.print(F("chord table: factory, saved file rejected: "));
  Serial.println(error);
#endif
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
