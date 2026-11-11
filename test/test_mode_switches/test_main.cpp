#include "mode_switches.h"
#include "output_policy.h"
#include "pin_map.h"

#include <unity.h>

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

// Wiring of the behavior switches. mode_switches.h is the only count; this
// test fails if a switch is added or its pin, labels, or restart rule move
// without an update.
extern "C" void test_catalog_lists_the_three_mode_switches(void) {
  TEST_ASSERT_EQUAL_INT(3, static_cast<int>(ModeSwitch::Count));
  TEST_ASSERT_EQUAL_INT(3, kModeSwitchCount);

  TEST_ASSERT_TRUE(kModeSwitches[0].id == ModeSwitch::Wifi);
  TEST_ASSERT_TRUE(kModeSwitches[0].pin == PinFn::WifiEnable);
  TEST_ASSERT_TRUE(kModeSwitches[0].change == SwitchChange::RestartWhenBle);
  TEST_ASSERT_EQUAL_STRING("wifi", kModeSwitches[0].key);
  TEST_ASSERT_EQUAL_STRING("off", kModeSwitches[0].open_label);
  TEST_ASSERT_EQUAL_STRING("on", kModeSwitches[0].closed_label);

  TEST_ASSERT_TRUE(kModeSwitches[1].id == ModeSwitch::Output);
  TEST_ASSERT_TRUE(kModeSwitches[1].pin == PinFn::OutputMode);
  TEST_ASSERT_TRUE(kModeSwitches[1].change == SwitchChange::Restart);
  TEST_ASSERT_EQUAL_STRING("output", kModeSwitches[1].key);
  TEST_ASSERT_EQUAL_STRING("USB", kModeSwitches[1].open_label);
  TEST_ASSERT_EQUAL_STRING("BLE", kModeSwitches[1].closed_label);

  TEST_ASSERT_TRUE(kModeSwitches[2].id == ModeSwitch::Fire);
  TEST_ASSERT_TRUE(kModeSwitches[2].pin == PinFn::FireTiming);
  TEST_ASSERT_TRUE(kModeSwitches[2].change == SwitchChange::Live);
  TEST_ASSERT_EQUAL_STRING("fire", kModeSwitches[2].key);
  TEST_ASSERT_EQUAL_STRING("release", kModeSwitches[2].open_label);
  TEST_ASSERT_EQUAL_STRING("first", kModeSwitches[2].closed_label);
}

extern "C" void test_restart_display_matches_output_policy(void) {
  const OutputMode modes[] = {OutputMode::Usb, OutputMode::Ble};
  for (OutputMode mode : modes) {
    TEST_ASSERT_FALSE(mode_switch_restarts(SwitchChange::Live, mode));
    TEST_ASSERT_TRUE(mode_switch_restarts(SwitchChange::Restart, mode));
    const bool when_ble = mode == OutputMode::Ble;
    TEST_ASSERT_TRUE(mode_switch_restarts(SwitchChange::RestartWhenBle, mode) == when_ble);

    TEST_ASSERT_TRUE(wifi_edge_requests_restart(mode, false, true) == when_ble);
    TEST_ASSERT_TRUE(wifi_edge_requests_restart(mode, true, false) == when_ble);
    TEST_ASSERT_FALSE(wifi_edge_requests_restart(mode, false, false));
    TEST_ASSERT_FALSE(wifi_edge_requests_restart(mode, true, true));
  }

  TEST_ASSERT_FALSE(output_switch_requests_restart(false, false));
  TEST_ASSERT_FALSE(output_switch_requests_restart(true, true));
  TEST_ASSERT_TRUE(output_switch_requests_restart(false, true));
  TEST_ASSERT_TRUE(output_switch_requests_restart(true, false));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_catalog_lists_the_three_mode_switches);
  RUN_TEST(test_restart_display_matches_output_policy);
  return UNITY_END();
}
