#include "config_route.h"

#include <stdio.h>
#include <string.h>

#include <unity.h>

static ConfigRouteDecision decide_raw(const char *raw, bool token_ok) {
  HttpRequest req;
  const bool parsed = parse_http_request(raw, strlen(raw), &req);
  TEST_ASSERT_TRUE_MESSAGE(parsed, raw);
  return decide_config_route(req, token_ok);
}

static void expect_drain(const char *raw, bool token_ok, int code, const char *message, bool cors,
                         bool drain_body) {
  const ConfigRouteDecision decision = decide_raw(raw, token_ok);
  TEST_ASSERT_TRUE(decision.step == ConfigRouteStep::Drain);
  TEST_ASSERT_EQUAL_INT(code, decision.code);
  if (message == nullptr) TEST_ASSERT_NULL(decision.message);
  else TEST_ASSERT_EQUAL_STRING(message, decision.message);
  TEST_ASSERT_EQUAL(cors, decision.cors);
  TEST_ASSERT_EQUAL(drain_body, decision.drain_body);
}

extern "C" void test_unknown_path_is_404(void) {
  expect_drain("GET /api/nope HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n", true, 404, "not found", false,
               true);
}

extern "C" void test_wrong_origin_is_403(void) {
  expect_drain("GET /api/chords HTTP/1.1\r\nOrigin: http://example.com\r\n"
               "X-Api-Token: secret\r\n\r\n",
               true, 403, "來源不符", false, true);
}

extern "C" void test_bad_token_is_401_and_keeps_cors(void) {
  expect_drain("GET /api/chords HTTP/1.1\r\nOrigin: http://192.168.4.1\r\n"
               "X-Api-Token: no\r\n\r\n",
               false, 401, "請輸入 API token", true, true);
}

extern "C" void test_chunked_is_400(void) {
  expect_drain("POST /api/chords HTTP/1.1\r\nTransfer-Encoding: chunked\r\n"
               "X-Api-Token: secret\r\n\r\n",
               true, 400, "不支援分段上傳", false, true);
}

extern "C" void test_oversized_post_drains_and_reset_does_not(void) {
  expect_drain("POST /api/chords HTTP/1.1\r\nContent-Length: 50000\r\n"
               "X-Api-Token: secret\r\n\r\n",
               true, 413, "內容太大", false, true);
  expect_drain("POST /api/chords/reset HTTP/1.1\r\nContent-Length: 300000\r\n"
               "X-Api-Token: secret\r\n\r\n",
               true, 413, "內容太大", false, false);
}

extern "C" void test_options_on_known_path_is_204(void) {
  const ConfigRouteDecision decision =
      decide_raw("OPTIONS /api/fire HTTP/1.1\r\nOrigin: http://192.168.4.1\r\n\r\n", false);
  TEST_ASSERT_TRUE(decision.step == ConfigRouteStep::Drain);
  TEST_ASSERT_TRUE(decision.drain == ConfigRouteDrain::Status);
  TEST_ASSERT_EQUAL_INT(204, decision.code);
  TEST_ASSERT_TRUE(decision.cors);
  TEST_ASSERT_TRUE(decision.drain_body);
}

extern "C" void test_get_chords_queues_and_fire_timing_stay_on_this_poll(void) {
  const ConfigRouteDecision chords =
      decide_raw("GET /api/chords HTTP/1.1\r\nX-Api-Token: secret\r\n\r\n", true);
  TEST_ASSERT_TRUE(chords.step == ConfigRouteStep::QueueGet);

  const ConfigRouteDecision fire =
      decide_raw("GET /api/fire HTTP/1.1\r\nX-Api-Token: secret\r\n\r\n", true);
  TEST_ASSERT_TRUE(fire.step == ConfigRouteStep::SendFire);

  const ConfigRouteDecision timing =
      decide_raw("GET /api/timing HTTP/1.1\r\nX-Api-Token: secret\r\n\r\n", true);
  TEST_ASSERT_TRUE(timing.step == ConfigRouteStep::SendTiming);
}

extern "C" void test_wrong_method_is_404(void) {
  expect_drain("POST / HTTP/1.1\r\n\r\n", true, 404, "not found", false, true);
  expect_drain("GET /api/chords/reset HTTP/1.1\r\nX-Api-Token: secret\r\n\r\n", true, 404, "not found",
               false, true);
  expect_drain("PUT /api/timing HTTP/1.1\r\nX-Api-Token: secret\r\n\r\n", true, 404, "not found", false,
               true);
}

extern "C" void test_posts_read_a_body(void) {
  const ConfigRouteDecision chords =
      decide_raw("POST /api/chords HTTP/1.1\r\nContent-Length: 2\r\nX-Api-Token: secret\r\n\r\n", true);
  TEST_ASSERT_TRUE(chords.step == ConfigRouteStep::ReadChords);
  const ConfigRouteDecision fire =
      decide_raw("POST /api/fire HTTP/1.1\r\nContent-Length: 2\r\nX-Api-Token: secret\r\n\r\n", true);
  TEST_ASSERT_TRUE(fire.step == ConfigRouteStep::ReadFire);
  const ConfigRouteDecision timing =
      decide_raw("POST /api/timing HTTP/1.1\r\nContent-Length: 2\r\nX-Api-Token: secret\r\n\r\n", true);
  TEST_ASSERT_TRUE(timing.step == ConfigRouteStep::ReadTiming);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_unknown_path_is_404);
  RUN_TEST(test_wrong_origin_is_403);
  RUN_TEST(test_bad_token_is_401_and_keeps_cors);
  RUN_TEST(test_chunked_is_400);
  RUN_TEST(test_oversized_post_drains_and_reset_does_not);
  RUN_TEST(test_options_on_known_path_is_204);
  RUN_TEST(test_get_chords_queues_and_fire_timing_stay_on_this_poll);
  RUN_TEST(test_wrong_method_is_404);
  RUN_TEST(test_posts_read_a_body);
  return UNITY_END();
}
