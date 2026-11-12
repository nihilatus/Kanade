#include "chord_core.h"
#include "pin_map.h"

#include <unity.h>

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

// Wiring locked here. pin_map.h is the only GPIO list; this test fails if a
// chord key, the wifi switch, the output switch, or the fire switch moves
// without an update.
extern "C" void test_pin_map_matches_wiring(void) {
  static const char *kNames[kChordKeyCount] = {
      "LL", "LU", "LR", "LD", "LT0", "LT1", "LT2", "RL", "RU", "RR", "RD", "RT0", "RT1", "RT2"};
  static const int kChordGpio[kChordKeyCount] = {17, 18, 10, 11, 12, 13, 14, 42, 41, 40, 39, 38, 48, 47};

  TEST_ASSERT_EQUAL_INT(kChordKeyCount, static_cast<int>(PinFn::RT2) + 1);
  for (int i = 0; i < kChordKeyCount; ++i) {
    const PinFn fn = static_cast<PinFn>(i);
    TEST_ASSERT_TRUE(pin_fn_is_chord(fn));
    TEST_ASSERT_EQUAL_INT(kChordGpio[i], pin_gpio(fn));
    TEST_ASSERT_EQUAL_STRING(kNames[i], chord_key_name(i));
    for (int j = i + 1; j < kChordKeyCount; ++j) {
      TEST_ASSERT_NOT_EQUAL(kChordGpio[i], kChordGpio[j]);
    }
  }

  TEST_ASSERT_FALSE(pin_fn_is_chord(PinFn::WifiEnable));
  TEST_ASSERT_FALSE(pin_fn_is_chord(PinFn::OutputMode));
  TEST_ASSERT_FALSE(pin_fn_is_chord(PinFn::FireTiming));
  TEST_ASSERT_EQUAL_INT(15, pin_gpio(PinFn::WifiEnable));
  TEST_ASSERT_EQUAL_INT(16, pin_gpio(PinFn::OutputMode));
  TEST_ASSERT_EQUAL_INT(21, pin_gpio(PinFn::FireTiming));
  TEST_ASSERT_EQUAL_INT(-1, pin_gpio(PinFn::Count));

  const int extra[] = {pin_gpio(PinFn::WifiEnable), pin_gpio(PinFn::OutputMode),
                       pin_gpio(PinFn::FireTiming)};
  for (int i = 0; i < kChordKeyCount; ++i) {
    for (int gpio : extra) {
      TEST_ASSERT_NOT_EQUAL(kChordGpio[i], gpio);
    }
  }
  TEST_ASSERT_NOT_EQUAL(extra[0], extra[1]);
  TEST_ASSERT_NOT_EQUAL(extra[0], extra[2]);
  TEST_ASSERT_NOT_EQUAL(extra[1], extra[2]);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_pin_map_matches_wiring);
  return UNITY_END();
}
