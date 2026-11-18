#include "config_timing_json.h"

#include <string.h>

#include <unity.h>

static ChordTiming sample_timing() {
  ChordTiming timing;
  timing.debounce_ms = 8;
  timing.release_guard_ms = 10;
  timing.repeat_delay_ms = 400;
  timing.repeat_interval_ms = 33;
  return timing;
}

static ChordTimingLimits sample_limits() {
  ChordTimingLimits limits;
  limits.debounce_max = 49;
  limits.release_max = 500;
  limits.bounce_margin_ms = 2;
  limits.repeat_delay_min = 51;
  limits.repeat_delay_max = 5000;
  limits.repeat_interval_min = 25;
  limits.repeat_interval_max = 255;
  return limits;
}

static bool parse_text(const char *text, ChordTiming *out, char *err, size_t err_len) {
  return config_timing_parse(text, strlen(text), out, err, err_len);
}

extern "C" void test_missing_field(void) {
  ChordTiming timing{};
  char err[80];
  err[0] = '\0';
  const char *text =
      "{\"release_guard_ms\":10,\"repeat_delay_ms\":400,\"repeat_interval_ms\":33}";
  TEST_ASSERT_FALSE(parse_text(text, &timing, err, sizeof(err)));
  TEST_ASSERT_EQUAL_STRING("缺少去彈跳", err);
}

extern "C" void test_non_integer(void) {
  ChordTiming timing{};
  char err[80];
  const char *boolean =
      "{\"debounce_ms\":true,\"release_guard_ms\":10,\"repeat_delay_ms\":400,\"repeat_interval_ms\":33}";
  TEST_ASSERT_FALSE(parse_text(boolean, &timing, err, sizeof(err)));
  TEST_ASSERT_EQUAL_STRING("時間參數必須是整數", err);

  const char *fraction =
      "{\"debounce_ms\":1.5,\"release_guard_ms\":10,\"repeat_delay_ms\":400,\"repeat_interval_ms\":33}";
  TEST_ASSERT_FALSE(parse_text(fraction, &timing, err, sizeof(err)));
  TEST_ASSERT_EQUAL_STRING("時間參數必須是整數", err);

  const char *text =
      "{\"debounce_ms\":\"8\",\"release_guard_ms\":10,\"repeat_delay_ms\":400,\"repeat_interval_ms\":33}";
  TEST_ASSERT_FALSE(parse_text(text, &timing, err, sizeof(err)));
  TEST_ASSERT_EQUAL_STRING("時間參數必須是整數", err);
}

extern "C" void test_round_trip_and_ok_flag(void) {
  const ChordTiming cur = sample_timing();
  ChordTiming factory = cur;
  factory.debounce_ms = 9;
  const ChordTimingLimits limits = sample_limits();
  char buf[512];
  size_t wrote = 0;
  TEST_ASSERT_TRUE(config_timing_format(cur, factory, limits, false, buf, sizeof(buf), &wrote));
  TEST_ASSERT_TRUE(wrote > 0);
  TEST_ASSERT_NULL(strstr(buf, "\"ok\""));

  ChordTiming parsed{};
  char err[80];
  TEST_ASSERT_TRUE(config_timing_parse(buf, wrote, &parsed, err, sizeof(err)));
  TEST_ASSERT_EQUAL_UINT16(cur.debounce_ms, parsed.debounce_ms);
  TEST_ASSERT_EQUAL_UINT16(cur.release_guard_ms, parsed.release_guard_ms);
  TEST_ASSERT_EQUAL_UINT16(cur.repeat_delay_ms, parsed.repeat_delay_ms);
  TEST_ASSERT_EQUAL_UINT16(cur.repeat_interval_ms, parsed.repeat_interval_ms);

  TEST_ASSERT_TRUE(config_timing_format(cur, factory, limits, true, buf, sizeof(buf), &wrote));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"ok\":true"));
}

extern "C" void test_malformed_json(void) {
  ChordTiming timing{};
  char err[80];
  TEST_ASSERT_FALSE(parse_text("{", &timing, err, sizeof(err)));
  TEST_ASSERT_EQUAL_STRING("JSON 格式不對", err);
  TEST_ASSERT_FALSE(config_timing_parse(nullptr, 0, &timing, err, sizeof(err)));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_missing_field);
  RUN_TEST(test_non_integer);
  RUN_TEST(test_round_trip_and_ok_flag);
  RUN_TEST(test_malformed_json);
  return UNITY_END();
}
