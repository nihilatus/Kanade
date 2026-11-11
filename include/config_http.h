#pragma once

#include <stddef.h>
#include <stdint.h>

// Pure request checks for the config AP. No sockets, so host tests can run them.

static constexpr char kConfigOrigin[] = "http://192.168.4.1";
static constexpr size_t kHttpHeaderMax = 1536;

struct HttpRequest {
  char method[8];
  char path[40];
  long content_length;  // -1 when the header is absent. Too big to fit becomes LONG_MAX.
  bool chunked;
  bool has_token;
  bool token_overflow;
  bool has_origin;
  bool origin_overflow;
  char token[128];
  char origin[80];
};

// Index just past the header block's blank line, or 0 when it is not complete yet.
size_t http_header_end(const char *data, size_t len);

// data/len cover one header block, including the final CRLF CRLF. False is malformed.
bool parse_http_request(const char *data, size_t len, HttpRequest *out);

// SSID 1..32 bytes, passphrase 8..63, and not the published placeholder.
bool ap_credentials_ok(const char *ssid, const char *password, const char *placeholder);

// An empty expected token allows every caller. A mismatch is false.
// The config server must not use that empty case: an unresolved token denies /api/*.
bool token_matches(const char *expected, const char *got);

// Compile-time token wins. A stored value is used only when compile-time is empty.
// Both empty means the board should generate one. nullptr is empty.
enum class ApiTokenAction : uint8_t { UseCompile, UseStored, Generate };

ApiTokenAction api_token_action(const char *compile_time, const char *stored);

// Lowercase hex, two characters per byte, plus a NUL. False when the buffer
// cannot hold that, or when raw is null and raw_len is not 0.
bool format_token_hex(const uint8_t *raw, size_t raw_len, char *out, size_t out_len);

// A missing Origin is a non-browser client on the AP. A browser sends Origin,
// and only the config page itself is accepted.
bool origin_allowed(const char *origin, bool has_origin);
