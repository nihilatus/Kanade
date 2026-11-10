#include "config_http.h"

#include <limits.h>
#include <string.h>

#include <unity.h>

static void expect_parse(const char *raw, bool ok) {
  HttpRequest req;
  const bool got = parse_http_request(raw, strlen(raw), &req);
  TEST_ASSERT_EQUAL_MESSAGE(ok, got, raw);
}

extern "C" void test_header_end_waits_for_blank_line(void) {
  const char *partial = "GET / HTTP/1.1\r\nHost: 192.168.4.1\r\n";
  TEST_ASSERT_EQUAL_UINT(0, http_header_end(partial, strlen(partial)));
  const char *full = "GET / HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n";
  TEST_ASSERT_EQUAL_UINT(strlen(full), http_header_end(full, strlen(full)));
}

extern "C" void test_parse_get_and_strips_query(void) {
  const char *raw = "GET /api/fire?x=1 HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n";
  HttpRequest req;
  TEST_ASSERT_TRUE(parse_http_request(raw, strlen(raw), &req));
  TEST_ASSERT_EQUAL_STRING("GET", req.method);
  TEST_ASSERT_EQUAL_STRING("/api/fire", req.path);
  TEST_ASSERT_EQUAL_INT(-1, req.content_length);
  TEST_ASSERT_FALSE(req.chunked);
  TEST_ASSERT_FALSE(req.has_token);
}

extern "C" void test_parse_post_headers(void) {
  const char *raw =
      "POST /api/chords HTTP/1.1\r\n"
      "Content-Length: 12\r\n"
      "X-Api-Token: secret\r\n"
      "Origin: http://192.168.4.1\r\n"
      "\r\n";
  HttpRequest req;
  TEST_ASSERT_TRUE(parse_http_request(raw, strlen(raw), &req));
  TEST_ASSERT_EQUAL_STRING("POST", req.method);
  TEST_ASSERT_EQUAL_STRING("/api/chords", req.path);
  TEST_ASSERT_EQUAL_INT(12, req.content_length);
  TEST_ASSERT_TRUE(req.has_token);
  TEST_ASSERT_EQUAL_STRING("secret", req.token);
  TEST_ASSERT_TRUE(req.has_origin);
  TEST_ASSERT_EQUAL_STRING(kConfigOrigin, req.origin);
}

extern "C" void test_parse_rejects_duplicate_length_and_nul(void) {
  const char *dup =
      "POST / HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n";
  expect_parse(dup, false);
  char raw[64];
  memcpy(raw, "GET / HTTP/1.1\r\n\r\n", 18);
  raw[4] = '\0';
  HttpRequest req;
  TEST_ASSERT_FALSE(parse_http_request(raw, 18, &req));
}

extern "C" void test_parse_chunked_and_huge_length(void) {
  const char *chunked =
      "POST /api/chords HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n";
  HttpRequest req;
  TEST_ASSERT_TRUE(parse_http_request(chunked, strlen(chunked), &req));
  TEST_ASSERT_TRUE(req.chunked);

  const char *huge =
      "POST /api/chords HTTP/1.1\r\nContent-Length: 99999999999999999999\r\n\r\n";
  TEST_ASSERT_TRUE(parse_http_request(huge, strlen(huge), &req));
  TEST_ASSERT_EQUAL_INT(LONG_MAX, req.content_length);
}

extern "C" void test_token_and_origin(void) {
  TEST_ASSERT_TRUE(token_matches("", "anything"));
  TEST_ASSERT_TRUE(token_matches(nullptr, "x"));
  TEST_ASSERT_TRUE(token_matches("secret", "secret"));
  TEST_ASSERT_FALSE(token_matches("secret", "secreT"));
  TEST_ASSERT_FALSE(token_matches("secret", "secret "));
  TEST_ASSERT_FALSE(token_matches("secret", nullptr));

  TEST_ASSERT_TRUE(origin_allowed(nullptr, false));
  TEST_ASSERT_TRUE(origin_allowed(kConfigOrigin, true));
  TEST_ASSERT_FALSE(origin_allowed("http://192.168.4.1/", true));
  TEST_ASSERT_FALSE(origin_allowed("https://evil.example", true));
  TEST_ASSERT_FALSE(origin_allowed("", true));
}

extern "C" void test_ap_credentials(void) {
  const char *placeholder = "replace-with-8-or-more-chars";
  TEST_ASSERT_FALSE(ap_credentials_ok("JIS-Chord", placeholder, placeholder));
  TEST_ASSERT_FALSE(ap_credentials_ok("JIS-Chord", "short", placeholder));
  TEST_ASSERT_FALSE(ap_credentials_ok("", "12345678", placeholder));
  char long_ssid[34];
  memset(long_ssid, 'a', 33);
  long_ssid[33] = '\0';
  TEST_ASSERT_FALSE(ap_credentials_ok(long_ssid, "12345678", placeholder));
  char long_pass[65];
  memset(long_pass, 'b', 64);
  long_pass[64] = '\0';
  TEST_ASSERT_FALSE(ap_credentials_ok("JIS-Chord", long_pass, placeholder));
  TEST_ASSERT_TRUE(ap_credentials_ok("JIS-Chord", "12345678", placeholder));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_header_end_waits_for_blank_line);
  RUN_TEST(test_parse_get_and_strips_query);
  RUN_TEST(test_parse_post_headers);
  RUN_TEST(test_parse_rejects_duplicate_length_and_nul);
  RUN_TEST(test_parse_chunked_and_huge_length);
  RUN_TEST(test_token_and_origin);
  RUN_TEST(test_ap_credentials);
  return UNITY_END();
}
