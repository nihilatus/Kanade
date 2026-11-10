#include "chords.h"

#include <stdio.h>
#include <string.h>

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

#include "buttons.h"
#include "console_log.h"
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

static const ChordRule kFactory[] = {
#include "chord_map.inc"
};

static constexpr size_t kFactoryCount = sizeof(kFactory) / sizeof(kFactory[0]);
static constexpr size_t kKanaMax = 47;
static constexpr size_t kOutputMax = 63;
static constexpr size_t kPoolSize = 30 * 1024;

static_assert(kFactoryCount > 0, "factory chord table is empty");
static_assert(kFactoryCount <= kChordsMaxRows, "factory chord table exceeds kChordsMaxRows");
static_assert(BTN_COUNT <= 16, "buttons_mask is uint16_t");

// Rule rows and the string pool their kana/output pointers reference.
// chords_begin() takes both from PSRAM (about 4 KB + 30 KB). Matching only
// reads them, which is fine at PSRAM speed, and this leaves internal RAM for
// the WiFi stack. The JSON DOM allocator below does the same.
static ChordRule *g_live = nullptr;
static size_t g_live_count = 0;
static char *g_pool = nullptr;
static bool g_from_file = false;
static bool g_fs = false;

static ChordRule *g_stage_rules = nullptr;
static char *g_stage_pool = nullptr;
static size_t g_stage_count = 0;
static size_t g_stage_pool_used = 0;
static bool g_staged = false;

// Debounce has already required the raw level to hold before this mask
// goes empty. This second wait only covers a bounce that returns after
// that stable release. 10 ms is a trial; raise it if a chord splits.
// ReleaseAll waits this long before sending. FirstUp sends on the first
// peak-key release and does not use this wait.
static constexpr uint32_t kReleaseGuardMs = 10;
static constexpr char kPrefsNs[] = "jis";
static constexpr char kPrefsFire[] = "fire";

enum class Phase : uint8_t { Idle, Holding, ReleaseGuard, Draining };
enum class FireMode : uint8_t { ReleaseAll = 0, FirstUp = 1 };

static Phase g_phase = Phase::Idle;
static FireMode g_fire_mode = FireMode::ReleaseAll;
static uint16_t g_max_chord = 0;
static uint8_t g_max_count = 0;
static uint32_t g_guard_since = 0;

static uint8_t popcount16(uint16_t value) {
  return static_cast<uint8_t>(__builtin_popcount(static_cast<unsigned>(value)));
}

// JSON DOM lives in PSRAM so the WiFi stack can keep internal RAM.
struct PsramAllocator : ArduinoJson::Allocator {
  void *allocate(size_t size) override {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  void deallocate(void *pointer) override { heap_caps_free(pointer); }
  void *reallocate(void *pointer, size_t new_size) override {
    return heap_caps_realloc(pointer, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
};

static bool set_err(char *err, size_t err_len, const char *msg) {
  if (err != nullptr && err_len > 0) {
    snprintf(err, err_len, "%s", msg);
  }
  return false;
}

static bool set_row_err(char *err, size_t err_len, unsigned row, const char *msg) {
  if (err != nullptr && err_len > 0) {
    snprintf(err, err_len, "第 %u 列%s", row, msg);
  }
  return false;
}

static void *alloc_spiram_first(size_t size) {
  void *p = ps_malloc(size);
  if (p == nullptr) p = malloc(size);
  return p;
}

static bool ensure_live_bufs() {
  if (g_live == nullptr) {
    g_live = static_cast<ChordRule *>(alloc_spiram_first(sizeof(ChordRule) * kChordsMaxRows));
  }
  if (g_pool == nullptr) {
    g_pool = static_cast<char *>(alloc_spiram_first(kPoolSize));
  }
  return g_live != nullptr && g_pool != nullptr;
}

static bool ensure_stage_bufs() {
  if (g_stage_rules != nullptr && g_stage_pool != nullptr) return true;
  g_stage_rules = static_cast<ChordRule *>(ps_malloc(sizeof(ChordRule) * kChordsMaxRows));
  g_stage_pool = static_cast<char *>(ps_malloc(kPoolSize));
  if (g_stage_rules == nullptr || g_stage_pool == nullptr) {
    free(g_stage_rules);
    free(g_stage_pool);
    g_stage_rules = static_cast<ChordRule *>(malloc(sizeof(ChordRule) * kChordsMaxRows));
    g_stage_pool = static_cast<char *>(malloc(kPoolSize));
  }
  return g_stage_rules != nullptr && g_stage_pool != nullptr;
}

static bool copy_text(char *pool, size_t *used, const char *src, const char **out) {
  const size_t n = strlen(src);
  if (*used + n + 1 > kPoolSize) return false;
  char *dest = pool + *used;
  memcpy(dest, src, n + 1);
  *used += n + 1;
  *out = dest;
  return true;
}

static bool text_ok(const char *s, bool ascii_only) {
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s); *p != 0; ++p) {
    if (*p < 0x20 || *p == 0x7F) return false;
    if (ascii_only && *p > 0x7E) return false;
  }
  return true;
}

static bool mask_from_array(JsonArray arr, uint16_t *mask, char *err, size_t err_len, unsigned row) {
  uint16_t bits = 0;
  for (JsonVariant value : arr) {
    if (!value.is<const char *>()) return set_row_err(err, err_len, row, "有不認識的鍵");
    const char *name = value.as<const char *>();
    int index = -1;
    for (uint8_t i = 0; i < BTN_COUNT; ++i) {
      if (strcmp(name, kButtons[i].name) == 0) {
        index = static_cast<int>(i);
        break;
      }
    }
    if (index < 0) return set_row_err(err, err_len, row, "有不認識的鍵");
    const uint16_t bit = static_cast<uint16_t>(1u << index);
    if ((bits & bit) != 0) return set_row_err(err, err_len, row, "的鍵重複");
    bits = static_cast<uint16_t>(bits | bit);
  }
  *mask = bits;
  return true;
}

static bool take_string(JsonObject obj, const char *key, const char **out, char *err, size_t err_len,
                        unsigned row, const char *bad_msg) {
  JsonVariant value = obj[key];
  if (value.isNull()) {
    *out = "";
    return true;
  }
  if (!value.is<const char *>()) return set_row_err(err, err_len, row, bad_msg);
  *out = value.as<const char *>();
  if (*out == nullptr) *out = "";
  return true;
}

static void add_key_names(JsonArray arr, uint16_t mask) {
  for (uint8_t i = 0; i < BTN_COUNT; ++i) {
    if ((mask & static_cast<uint16_t>(1u << i)) == 0) continue;
    arr.add(kButtons[i].name);
  }
}

static void note_snapshot(uint16_t mask) {
  const uint8_t count = popcount16(mask);
  // Largest set wins. Same size: keep the later snapshot.
  if (count > g_max_count || (count == g_max_count && mask != g_max_chord)) {
    g_max_chord = mask;
    g_max_count = count;
  }
}

static bool peak_broken(uint16_t mask) {
  return g_max_count > 0 && (mask & g_max_chord) != g_max_chord;
}

static void clear_peak() {
  g_max_chord = 0;
  g_max_count = 0;
}

static void begin_hold(uint16_t mask) {
  g_phase = Phase::Holding;
  clear_peak();
  note_snapshot(mask);
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
  if (g_live == nullptr) return -1;
  int best = -1;
  int best_req = -1;
  int best_opt = 255;
  uint16_t best_id = 0xFFFF;

  for (size_t i = 0; i < g_live_count; ++i) {
    const ChordRule &row = g_live[i];
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

static void fire(uint16_t mask);  // defined below; send path uses the live table

static void fire_peak() {
  const uint16_t chord = g_max_chord;
  clear_peak();
  fire(chord);
}

static const char *fire_mode_name(FireMode mode) {
  return mode == FireMode::FirstUp ? "first" : "release";
}

static void log_fire_mode() {
  char line[120];
  if (g_fire_mode == FireMode::FirstUp) {
    snprintf(line, sizeof(line),
             "送出模式：首鍵放開就送（高峰後第一顆放開即送，其餘放開前不接新和弦）");
  } else {
    snprintf(line, sizeof(line), "送出模式：全放再送（全部放開並維持 %u ms）",
             static_cast<unsigned>(kReleaseGuardMs));
  }
  console_println(line);
}

static void load_fire_mode() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, true)) return;
  const uint8_t stored = prefs.getUChar(kPrefsFire, 0);
  prefs.end();
  g_fire_mode = stored == 1 ? FireMode::FirstUp : FireMode::ReleaseAll;
}

static void save_fire_mode() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, false)) {
    console_println("送出模式這次沒寫進記憶體，重開機會回到上次成功的設定");
    return;
  }
  prefs.putUChar(kPrefsFire, g_fire_mode == FireMode::FirstUp ? 1 : 0);
  prefs.end();
}

const char *chords_fire_mode_name() { return fire_mode_name(g_fire_mode); }

bool chords_set_fire_mode_name(const char *name) {
  if (name == nullptr) return false;
  FireMode next;
  if (strcmp(name, "release") == 0) next = FireMode::ReleaseAll;
  else if (strcmp(name, "first") == 0) next = FireMode::FirstUp;
  else return false;

  if (next != g_fire_mode && next == FireMode::FirstUp && g_phase == Phase::ReleaseGuard) {
    // The chord is already fully up; don't keep waiting out the empty guard.
    fire_peak();
    g_phase = Phase::Idle;
  }
  g_fire_mode = next;
  save_fire_mode();
  log_fire_mode();
  return true;
}

static void fire(uint16_t mask) {
  if (mask == 0) return;

  const int index = find_rule(mask);
  if (index < 0) {
    log_chord("no-match", 0, mask, "", "");
    return;
  }

  const ChordRule &row = g_live[index];
  log_chord("send", row.id, mask, row.kana, row.output);
  if (row.output == nullptr || row.output[0] == '\0') return;
  if (!hid_keyboard_ready()) return;
  hid_type_recipe(row.output);
}

size_t chords_count() { return g_live_count; }

bool chords_at(size_t index, ChordView *out) {
  if (out == nullptr || index >= g_live_count) return false;
  const ChordRule &row = g_live[index];
  out->id = row.id;
  out->required = row.required;
  out->optional = row.optional;
  out->kana = row.kana != nullptr ? row.kana : "";
  out->output = row.output != nullptr ? row.output : "";
  return true;
}

bool chords_using_saved() { return g_from_file; }
bool chords_fs_ready() { return g_fs; }

void chords_drop_staged() {
  g_staged = false;
  g_stage_count = 0;
  g_stage_pool_used = 0;
}

void chords_load_factory() {
  chords_drop_staged();
  if (!ensure_live_bufs()) {
    g_live_count = 0;
    g_from_file = false;
    return;
  }
  for (size_t i = 0; i < kFactoryCount; ++i) g_live[i] = kFactory[i];
  for (size_t i = kFactoryCount; i < kChordsMaxRows; ++i) {
    g_live[i].id = 0;
    g_live[i].required = 0;
    g_live[i].optional = 0;
    g_live[i].kana = "";
    g_live[i].output = "";
  }
  g_live_count = kFactoryCount;
  g_from_file = false;
}

bool chords_stage_json(const char *json, size_t len, char *err, size_t err_len) {
  g_staged = false;
  g_stage_count = 0;
  g_stage_pool_used = 0;
  if (json == nullptr) return set_err(err, err_len, "沒有內容");
  if (len >= 3 && static_cast<uint8_t>(json[0]) == 0xEF && static_cast<uint8_t>(json[1]) == 0xBB &&
      static_cast<uint8_t>(json[2]) == 0xBF) {
    json += 3;
    len -= 3;
  }
  if (len == 0) return set_err(err, err_len, "沒有內容");
  if (len > kChordsJsonMax) return set_err(err, err_len, "內容太大");
  if (!ensure_stage_bufs()) return set_err(err, err_len, "記憶體不足");

  PsramAllocator allocator;
  JsonDocument psram_doc(&allocator);
  JsonDocument internal_doc;
  JsonDocument &doc = psramFound() ? psram_doc : internal_doc;
  const DeserializationError error = deserializeJson(doc, json, len);
  if (error == DeserializationError::NoMemory) return set_err(err, err_len, "內容太大");
  if (error) return set_err(err, err_len, "JSON 格式不對");

  JsonArray arr = doc["chords"].as<JsonArray>();
  if (arr.isNull()) return set_err(err, err_len, "缺少 chords");
  if (arr.size() == 0) return set_err(err, err_len, "至少要有一列");
  if (arr.size() > kChordsMaxRows) return set_err(err, err_len, "列數太多");

  size_t pool_used = 0;
  for (size_t i = 0; i < arr.size(); ++i) {
    const unsigned row_no = static_cast<unsigned>(i + 1);
    JsonObject obj = arr[i].as<JsonObject>();
    if (obj.isNull()) return set_row_err(err, err_len, row_no, "不是物件");

    JsonVariant id_value = obj["id"];
    if (!id_value.is<int>()) return set_row_err(err, err_len, row_no, "的 id 不對");
    const long id = id_value.as<long>();
    if (id < 1 || id > 65535) return set_row_err(err, err_len, row_no, "的 id 不對");

    if (!obj["required"].is<JsonArray>()) return set_row_err(err, err_len, row_no, "的必按鍵不對");
    if (!obj["optional"].is<JsonArray>()) return set_row_err(err, err_len, row_no, "的可選鍵不對");

    uint16_t required = 0;
    uint16_t optional = 0;
    if (!mask_from_array(obj["required"].as<JsonArray>(), &required, err, err_len, row_no)) return false;
    if (!mask_from_array(obj["optional"].as<JsonArray>(), &optional, err, err_len, row_no)) return false;
    if ((required & optional) != 0) {
      return set_row_err(err, err_len, row_no, "的同一顆鍵不能又必按又可選");
    }

    const char *kana = "";
    const char *output = "";
    if (!take_string(obj, "kana", &kana, err, err_len, row_no, "的假名不對")) return false;
    if (!take_string(obj, "output", &output, err, err_len, row_no, "的 output 不對")) return false;
    if (strlen(kana) > kKanaMax) return set_row_err(err, err_len, row_no, "的假名太長");
    if (strlen(output) > kOutputMax) return set_row_err(err, err_len, row_no, "的 output 太長");
    if (!text_ok(kana, false)) return set_row_err(err, err_len, row_no, "的假名有無法顯示的字元");
    if (!text_ok(output, true)) return set_row_err(err, err_len, row_no, "的 output 只能用英數與符號");

    for (size_t prev = 0; prev < i; ++prev) {
      if (g_stage_rules[prev].id == static_cast<uint16_t>(id)) {
        return set_row_err(err, err_len, row_no, "的 id 重複");
      }
    }

    const char *kana_copy = "";
    const char *output_copy = "";
    if (!copy_text(g_stage_pool, &pool_used, kana, &kana_copy) ||
        !copy_text(g_stage_pool, &pool_used, output, &output_copy)) {
      return set_err(err, err_len, "內容太大，放不進記憶體");
    }

    ChordRule &dst = g_stage_rules[i];
    dst.id = static_cast<uint16_t>(id);
    dst.required = required;
    dst.optional = optional;
    dst.kana = kana_copy;
    dst.output = output_copy;
  }

  g_stage_count = arr.size();
  g_stage_pool_used = pool_used;
  g_staged = true;
  return true;
}

void chords_commit_staged() {
  if (!g_staged || g_stage_rules == nullptr || g_stage_pool == nullptr) return;
  if (!ensure_live_bufs()) return;
  memcpy(g_live, g_stage_rules, g_stage_count * sizeof(ChordRule));
  memcpy(g_pool, g_stage_pool, g_stage_pool_used);
  for (size_t i = 0; i < g_stage_count; ++i) {
    if (g_live[i].kana != nullptr) {
      g_live[i].kana = g_pool + (g_live[i].kana - g_stage_pool);
    }
    if (g_live[i].output != nullptr) {
      g_live[i].output = g_pool + (g_live[i].output - g_stage_pool);
    }
  }
  for (size_t i = g_stage_count; i < kChordsMaxRows; ++i) {
    g_live[i].id = 0;
    g_live[i].required = 0;
    g_live[i].optional = 0;
    g_live[i].kana = "";
    g_live[i].output = "";
  }
  g_live_count = g_stage_count;
  g_from_file = true;
  chords_drop_staged();
}

char *chords_format_json() {
  PsramAllocator allocator;
  JsonDocument psram_doc(&allocator);
  JsonDocument internal_doc;
  JsonDocument &doc = psramFound() ? psram_doc : internal_doc;
  doc["source"] = g_from_file ? "saved" : "factory";
  doc["count"] = g_live_count;
  JsonArray arr = doc["chords"].to<JsonArray>();
  for (size_t i = 0; i < g_live_count; ++i) {
    const ChordRule &row = g_live[i];
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = row.id;
    add_key_names(obj["required"].to<JsonArray>(), row.required);
    add_key_names(obj["optional"].to<JsonArray>(), row.optional);
    obj["kana"] = row.kana != nullptr ? row.kana : "";
    obj["output"] = row.output != nullptr ? row.output : "";
  }
  if (doc.overflowed()) return nullptr;
  const size_t need = measureJson(doc) + 1;
  char *buf = static_cast<char *>(ps_malloc(need));
  if (buf == nullptr) buf = static_cast<char *>(malloc(need));
  if (buf == nullptr) return nullptr;
  const size_t wrote = serializeJson(doc, buf, need);
  if (wrote == 0 || wrote >= need) {
    free(buf);
    return nullptr;
  }
  return buf;
}

static void log_factory(const char *why) {
  char line[160];
  snprintf(line, sizeof(line), "chords: factory table (%u rows)%s%s",
           static_cast<unsigned>(kFactoryCount), why != nullptr && why[0] != '\0' ? " — " : "",
           why != nullptr ? why : "");
  console_println(line);
}

enum class LoadResult : uint8_t { Ok, Rejected, NoMemory };

static void log_loaded() {
  char line[80];
  snprintf(line, sizeof(line), "chords: loaded %u rows from %s", static_cast<unsigned>(g_live_count),
           kChordsPath);
  console_println(line);
}

// On Ok the JSON is staged. Caller drops the stage on every other result.
static LoadResult stage_saved(const char *path, char *err, size_t err_len) {
  File file = LittleFS.open(path, "r");
  if (!file) {
    set_err(err, err_len, "could not open saved table");
    return LoadResult::Rejected;
  }
  const size_t n = file.size();
  if (n == 0 || n > kChordsJsonMax) {
    file.close();
    set_err(err, err_len, "saved table rejected (too large)");
    return LoadResult::Rejected;
  }

  char *buf = static_cast<char *>(ps_malloc(n + 1));
  if (buf == nullptr) buf = static_cast<char *>(malloc(n + 1));
  if (buf == nullptr) {
    file.close();
    set_err(err, err_len, "saved table rejected (no memory)");
    return LoadResult::NoMemory;
  }
  const size_t got = file.read(reinterpret_cast<uint8_t *>(buf), n);
  file.close();
  buf[got] = '\0';

  const bool ok = got == n && chords_stage_json(buf, got, err, err_len);
  free(buf);
  if (!ok) {
    if (err[0] == '\0') set_err(err, err_len, "saved table rejected");
    if (strcmp(err, "記憶體不足") == 0) return LoadResult::NoMemory;
    return LoadResult::Rejected;
  }
  return LoadResult::Ok;
}

// Power loss can leave a finished .tmp (and the previous .bak) with no
// chords.json. Validate before renaming so a bad file is never installed.
// *halt: stop looking — this file may be the newer copy and could not be judged.
static bool restore_orphan(const char *path, bool *halt, char *err, size_t err_len) {
  *halt = false;
  if (!LittleFS.exists(path)) return false;

  const LoadResult result = stage_saved(path, err, err_len);
  if (result == LoadResult::NoMemory) {
    chords_drop_staged();
    *halt = true;
    log_factory(err[0] != '\0' ? err : "saved table rejected (no memory)");
    return false;
  }
  if (result != LoadResult::Ok) {
    chords_drop_staged();
    return false;
  }
  if (!LittleFS.rename(path, kChordsPath)) {
    chords_drop_staged();
    *halt = true;
    log_factory("could not restore saved table");
    return false;
  }
  chords_commit_staged();
  if (!g_from_file) {
    chords_drop_staged();
    *halt = true;
    log_factory("no memory for live table");
    return false;
  }
  char line[96];
  snprintf(line, sizeof(line), "chords: restored %s", path);
  console_println(line);
  log_loaded();
  return true;
}

static void try_load_saved() {
  if (!g_fs) {
    log_factory("no filesystem");
    return;
  }

  char err[160];
  err[0] = '\0';
  if (!LittleFS.exists(kChordsPath)) {
    bool halt = false;
    if (restore_orphan(kChordsTmpPath, &halt, err, sizeof(err))) return;
    if (halt) return;
    if (restore_orphan(kChordsBakPath, &halt, err, sizeof(err))) return;
    if (halt) return;
    const bool saw = LittleFS.exists(kChordsTmpPath) || LittleFS.exists(kChordsBakPath);
    log_factory(saw ? (err[0] != '\0' ? err : "saved table rejected") : "");
    return;
  }

  const LoadResult result = stage_saved(kChordsPath, err, sizeof(err));
  if (result != LoadResult::Ok) {
    chords_drop_staged();
    log_factory(err[0] != '\0' ? err : "saved table rejected");
    return;
  }
  chords_commit_staged();
  if (!g_from_file) {
    chords_drop_staged();
    log_factory("no memory for live table");
    return;
  }
  log_loaded();
}

void chords_begin() {
  g_phase = Phase::Idle;
  clear_peak();
  g_guard_since = 0;

  console_ensure();
  load_fire_mode();
  log_fire_mode();
  console_println("序列指令：fire release | fire first");
  g_fs = LittleFS.begin(true);
  chords_load_factory();
  try_load_saved();
}

void chords_poll() {
  const uint16_t mask = buttons_mask();
  const uint32_t now = millis();

  switch (g_phase) {
    case Phase::Idle:
      if (mask != 0) begin_hold(mask);
      break;

    case Phase::Holding:
      if (g_fire_mode == FireMode::FirstUp) {
        if (peak_broken(mask)) {
          // A key that belonged to the peak went up. Send that peak now.
          // Keys still down are the tail, not the start of the next chord.
          fire_peak();
          g_phase = mask == 0 ? Phase::Idle : Phase::Draining;
        } else {
          note_snapshot(mask);
        }
      } else if (mask == 0) {
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
        g_phase = Phase::Idle;
        fire_peak();
      }
      break;

    case Phase::Draining:
      // FirstUp already sent. Ignore presses until the board is clear,
      // otherwise the tail and the next chord merge into one mask.
      if (mask == 0) g_phase = Phase::Idle;
      break;
  }
}
