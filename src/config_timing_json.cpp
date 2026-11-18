#include "config_timing_json.h"

#include <stdio.h>

#include <ArduinoJson.h>

static bool fail_msg(char *err, size_t err_len, const char *msg) {
  if (err != nullptr && err_len > 0 && msg != nullptr) snprintf(err, err_len, "%s", msg);
  return false;
}

static bool read_ms(JsonVariantConst v, uint16_t *out, char *err, size_t err_len) {
  if (v.is<bool>() || !v.is<long>()) return fail_msg(err, err_len, "時間參數必須是整數");
  const long value = v.as<long>();
  if (value < 0 || value > 65535) return fail_msg(err, err_len, "時間參數超出範圍");
  *out = static_cast<uint16_t>(value);
  return true;
}

static bool read_named_ms(JsonObject obj, const char *key, const char *missing, uint16_t *out, char *err,
                          size_t err_len) {
  const JsonVariantConst v = obj[key];
  if (v.isNull()) return fail_msg(err, err_len, missing);
  return read_ms(v, out, err, err_len);
}

bool config_timing_parse(const char *body, size_t len, ChordTiming *out, char *err, size_t err_len) {
  if (out == nullptr || body == nullptr) return fail_msg(err, err_len, "JSON 格式不對");
  JsonDocument doc;
  if (deserializeJson(doc, body, len) != DeserializationError::Ok) {
    return fail_msg(err, err_len, "JSON 格式不對");
  }
  const JsonObject obj = doc.as<JsonObject>();
  if (obj.isNull()) return fail_msg(err, err_len, "JSON 格式不對");
  ChordTiming timing;
  if (!read_named_ms(obj, "debounce_ms", "缺少去彈跳", &timing.debounce_ms, err, err_len) ||
      !read_named_ms(obj, "release_guard_ms", "缺少放開保護", &timing.release_guard_ms, err, err_len) ||
      !read_named_ms(obj, "repeat_delay_ms", "缺少連發延遲", &timing.repeat_delay_ms, err, err_len) ||
      !read_named_ms(obj, "repeat_interval_ms", "缺少連發間隔", &timing.repeat_interval_ms, err,
                     err_len)) {
    return false;
  }
  *out = timing;
  return true;
}

bool config_timing_format(const ChordTiming &cur, const ChordTiming &factory,
                          const ChordTimingLimits &limits, bool with_ok, char *buf, size_t cap,
                          size_t *wrote) {
  if (buf == nullptr || cap == 0) return false;
  JsonDocument doc;
  if (with_ok) doc["ok"] = true;
  doc["debounce_ms"] = cur.debounce_ms;
  doc["release_guard_ms"] = cur.release_guard_ms;
  doc["repeat_delay_ms"] = cur.repeat_delay_ms;
  doc["repeat_interval_ms"] = cur.repeat_interval_ms;
  JsonObject defaults = doc["defaults"].to<JsonObject>();
  defaults["debounce_ms"] = factory.debounce_ms;
  defaults["release_guard_ms"] = factory.release_guard_ms;
  defaults["repeat_delay_ms"] = factory.repeat_delay_ms;
  defaults["repeat_interval_ms"] = factory.repeat_interval_ms;
  JsonObject caps = doc["limits"].to<JsonObject>();
  caps["debounce_max"] = limits.debounce_max;
  caps["release_max"] = limits.release_max;
  caps["bounce_margin_ms"] = limits.bounce_margin_ms;
  caps["repeat_delay_min"] = limits.repeat_delay_min;
  caps["repeat_delay_max"] = limits.repeat_delay_max;
  caps["repeat_interval_min"] = limits.repeat_interval_min;
  caps["repeat_interval_max"] = limits.repeat_interval_max;
  const size_t n = serializeJson(doc, buf, cap);
  if (n == 0 || n >= cap) return false;
  if (wrote != nullptr) *wrote = n;
  return true;
}
