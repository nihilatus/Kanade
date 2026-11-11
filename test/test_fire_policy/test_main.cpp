#include "fire_switch.h"

#include <unity.h>

extern "C" void test_open_is_release_and_closed_is_first(void) {
  TEST_ASSERT_TRUE(fire_mode_from_closed(false) == ChordFireMode::ReleaseAll);
  TEST_ASSERT_TRUE(fire_mode_from_closed(true) == ChordFireMode::FirstUp);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_open_is_release_and_closed_is_first);
  return UNITY_END();
}
