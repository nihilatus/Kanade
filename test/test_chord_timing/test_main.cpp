#include "chord_core.h"
#include "chord_timing.h"
#include "chord_timing_nv.h"

#include <string.h>

#include <unity.h>

static ChordTiming with_release(uint16_t release_ms) {
  ChordTiming timing = chord_timing_factory();
  timing.release_guard_ms = release_ms;
  return timing;
}

extern "C" void test_factory_timing_is_accepted(void) {
  char err[96];
  err[0] = '\0';
  const ChordTiming timing = chord_timing_factory();
  TEST_ASSERT_TRUE(chord_timing_accept(timing, err, sizeof(err)));
  TEST_ASSERT_EQUAL_UINT16(kChordDebounceMs, chord_timing_current().debounce_ms);
  TEST_ASSERT_EQUAL_UINT16(kChordReleaseGuardMs, chord_timing_current().release_guard_ms);
  TEST_ASSERT_EQUAL_UINT16(kChordRepeatDelayMs, chord_timing_current().repeat_delay_ms);
  TEST_ASSERT_EQUAL_UINT16(kChordRepeatIntervalMs, chord_timing_current().repeat_interval_ms);
  TEST_ASSERT_EQUAL_UINT32(kChordBounceMs, chord_timing_bounce_ms());
  const ChordTimingLimits limits = chord_timing_limits();
  TEST_ASSERT_EQUAL_UINT16(kSwitchDebounceMs - 1, limits.debounce_max);
  TEST_ASSERT_EQUAL_UINT16(kChordBounceMs - kChordDebounceMs, limits.bounce_margin_ms);
  TEST_ASSERT_EQUAL_UINT16(kChordRepeatSettleMs + 1, limits.repeat_delay_min);
  TEST_ASSERT_EQUAL_UINT16(kChordTapGapMs * 2 + 1, limits.repeat_interval_min);
}

extern "C" void test_debounce_at_switch_window_is_rejected(void) {
  char err[96];
  err[0] = '\0';
  ChordTiming timing = chord_timing_factory();
  timing.debounce_ms = kSwitchDebounceMs;
  TEST_ASSERT_FALSE(chord_timing_accept(timing, err, sizeof(err)));
  TEST_ASSERT_TRUE(err[0] != '\0');
}

extern "C" void test_release_shorter_than_bounce_is_rejected(void) {
  char err[96];
  err[0] = '\0';
  const uint16_t bounce =
      static_cast<uint16_t>(kChordDebounceMs + (kChordBounceMs - kChordDebounceMs));
  ChordTiming timing = with_release(static_cast<uint16_t>(bounce - 1));
  TEST_ASSERT_FALSE(chord_timing_accept(timing, err, sizeof(err)));
  TEST_ASSERT_TRUE(err[0] != '\0');
}

extern "C" void test_repeat_delay_at_settle_is_rejected(void) {
  char err[96];
  err[0] = '\0';
  ChordTiming timing = chord_timing_factory();
  timing.repeat_delay_ms = kChordRepeatSettleMs;
  TEST_ASSERT_FALSE(chord_timing_accept(timing, err, sizeof(err)));
  TEST_ASSERT_TRUE(err[0] != '\0');
}

extern "C" void test_repeat_interval_at_two_taps_is_rejected(void) {
  char err[96];
  err[0] = '\0';
  ChordTiming timing = chord_timing_factory();
  timing.repeat_interval_ms = static_cast<uint16_t>(kChordTapGapMs * 2);
  TEST_ASSERT_FALSE(chord_timing_accept(timing, err, sizeof(err)));
  TEST_ASSERT_TRUE(err[0] != '\0');
}

extern "C" void test_longer_release_guard_holds_the_engine(void) {
  char err[96];
  err[0] = '\0';
  const ChordTiming timing = with_release(40);
  TEST_ASSERT_TRUE(chord_timing_accept(timing, err, sizeof(err)));
  chord_timing_apply(timing);

  ChordEngine engine;
  chord_engine_reset(&engine);
  ChordStep step = chord_engine_step(&engine, 0x0001, 0);
  TEST_ASSERT_FALSE(step.fire);
  step = chord_engine_step(&engine, 0, 1000);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::ReleaseGuard), static_cast<int>(engine.phase));
  step = chord_engine_step(&engine, 0, 1039);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::ReleaseGuard), static_cast<int>(engine.phase));
  step = chord_engine_step(&engine, 0, 1040);
  TEST_ASSERT_TRUE(step.fire);
  TEST_ASSERT_EQUAL_UINT16(0x0001, step.fire_mask);

  chord_timing_use_factory();
  TEST_ASSERT_EQUAL_UINT16(kChordReleaseGuardMs, chord_timing_current().release_guard_ms);
  TEST_ASSERT_EQUAL_UINT32(kChordBounceMs, chord_timing_bounce_ms());
}

extern "C" void test_timing_nv_round_trip(void) {
  const ChordTiming timing = chord_timing_factory();
  ChordTimingNv nv;
  chord_timing_nv_pack(timing, &nv);
  TEST_ASSERT_EQUAL_UINT8(kChordTimingNvVersion, nv.version);
  TEST_ASSERT_EQUAL_UINT8(0, nv.reserved);
  ChordTiming out;
  memset(&out, 0xff, sizeof(out));
  TEST_ASSERT_TRUE(chord_timing_nv_unpack(&nv, sizeof(nv), &out));
  TEST_ASSERT_EQUAL_UINT16(timing.debounce_ms, out.debounce_ms);
  TEST_ASSERT_EQUAL_UINT16(timing.release_guard_ms, out.release_guard_ms);
  TEST_ASSERT_EQUAL_UINT16(timing.repeat_delay_ms, out.repeat_delay_ms);
  TEST_ASSERT_EQUAL_UINT16(timing.repeat_interval_ms, out.repeat_interval_ms);
}

extern "C" void test_timing_nv_rejects_wrong_version(void) {
  ChordTimingNv nv;
  chord_timing_nv_pack(chord_timing_factory(), &nv);
  nv.version = static_cast<uint8_t>(kChordTimingNvVersion + 1);
  ChordTiming out;
  TEST_ASSERT_FALSE(chord_timing_nv_unpack(&nv, sizeof(nv), &out));
}

extern "C" void test_timing_nv_rejects_short_blob(void) {
  ChordTimingNv nv;
  chord_timing_nv_pack(chord_timing_factory(), &nv);
  ChordTiming out;
  TEST_ASSERT_FALSE(chord_timing_nv_unpack(&nv, sizeof(nv) - 1, &out));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_factory_timing_is_accepted);
  RUN_TEST(test_debounce_at_switch_window_is_rejected);
  RUN_TEST(test_release_shorter_than_bounce_is_rejected);
  RUN_TEST(test_repeat_delay_at_settle_is_rejected);
  RUN_TEST(test_repeat_interval_at_two_taps_is_rejected);
  RUN_TEST(test_longer_release_guard_holds_the_engine);
  RUN_TEST(test_timing_nv_round_trip);
  RUN_TEST(test_timing_nv_rejects_wrong_version);
  RUN_TEST(test_timing_nv_rejects_short_blob);
  return UNITY_END();
}
