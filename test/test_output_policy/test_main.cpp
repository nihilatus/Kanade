#include "output_policy.h"

#include <unity.h>

extern "C" void test_open_is_usb_and_closed_is_ble(void) {
  TEST_ASSERT_TRUE(output_mode_from_closed(false) == OutputMode::Usb);
  TEST_ASSERT_TRUE(output_mode_from_closed(true) == OutputMode::Ble);
}

extern "C" void test_usb_starts_whether_or_not_wifi_is_closed(void) {
  TEST_ASSERT_TRUE(output_starts_usb(OutputMode::Usb));
  TEST_ASSERT_FALSE(output_starts_ble(OutputMode::Usb, false));
  TEST_ASSERT_FALSE(output_starts_ble(OutputMode::Usb, true));
}

extern "C" void test_ble_starts_only_when_the_wifi_switch_is_open(void) {
  TEST_ASSERT_FALSE(output_starts_usb(OutputMode::Ble));
  TEST_ASSERT_TRUE(output_starts_ble(OutputMode::Ble, false));
  TEST_ASSERT_FALSE(output_starts_ble(OutputMode::Ble, true));
}

extern "C" void test_output_switch_restarts_only_after_the_boot_level_changes(void) {
  TEST_ASSERT_FALSE(output_switch_requests_restart(false, false));
  TEST_ASSERT_FALSE(output_switch_requests_restart(true, true));
  TEST_ASSERT_TRUE(output_switch_requests_restart(false, true));
  TEST_ASSERT_TRUE(output_switch_requests_restart(true, false));
}

extern "C" void test_wifi_edge_restarts_only_in_ble_mode(void) {
  TEST_ASSERT_FALSE(wifi_edge_requests_restart(OutputMode::Usb, false, true));
  TEST_ASSERT_FALSE(wifi_edge_requests_restart(OutputMode::Usb, true, false));
  TEST_ASSERT_FALSE(wifi_edge_requests_restart(OutputMode::Usb, true, true));
  TEST_ASSERT_FALSE(wifi_edge_requests_restart(OutputMode::Ble, false, false));
  TEST_ASSERT_FALSE(wifi_edge_requests_restart(OutputMode::Ble, true, true));
  TEST_ASSERT_TRUE(wifi_edge_requests_restart(OutputMode::Ble, false, true));
  TEST_ASSERT_TRUE(wifi_edge_requests_restart(OutputMode::Ble, true, false));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_open_is_usb_and_closed_is_ble);
  RUN_TEST(test_usb_starts_whether_or_not_wifi_is_closed);
  RUN_TEST(test_ble_starts_only_when_the_wifi_switch_is_open);
  RUN_TEST(test_output_switch_restarts_only_after_the_boot_level_changes);
  RUN_TEST(test_wifi_edge_restarts_only_in_ble_mode);
  return UNITY_END();
}
