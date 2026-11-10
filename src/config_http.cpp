#include "config_http.h"

#include <limits.h>
#include <string.h>

size_t http_header_end(const char *data, size_t len) {
  if (data == nullptr || len < 4) return 0;
  for (size_t i = 0; i + 3 < len; ++i) {
    if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n') {
      return i + 4;
    }
  }
  return 0;
}

static bool name_eq(const char *text, size_t len, const char *lower) {
  size_t i = 0;
  for (; lower[i] != '\0'; ++i) {
    if (i >= len) return false;
    char c = text[i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != lower[i]) return false;
  }
  return i == len;
}

static bool take_token(const char *&cursor, const char *end, char *dst, size_t dst_len) {
  while (cursor < end && *cursor == ' ') ++cursor;
  const char *start = cursor;
  while (cursor < end && *cursor != ' ' && *cursor != '\r') ++cursor;
  const size_t n = static_cast<size_t>(cursor - start);
  if (n == 0 || n >= dst_len) return false;
  memcpy(dst, start, n);
  dst[n] = '\0';
  return true;
}

static bool parse_content_length(const char *value, size_t len, long *out) {
  if (len == 0) return false;
  long number = 0;
  for (size_t i = 0; i < len; ++i) {
    if (value[i] < '0' || value[i] > '9') return false;
    const int digit = value[i] - '0';
    if (number > (LONG_MAX - digit) / 10) {
      *out = LONG_MAX;
      return true;
    }
    number = number * 10 + digit;
  }
  *out = number;
  return true;
}

static void copy_capped(char *dst, size_t dst_len, const char *src, size_t src_len, bool *overflow) {
  if (src_len >= dst_len) {
    *overflow = true;
    dst[0] = '\0';
    return;
  }
  memcpy(dst, src, src_len);
  dst[src_len] = '\0';
}

bool parse_http_request(const char *data, size_t len, HttpRequest *out) {
  if (data == nullptr || out == nullptr || len < 4) return false;
  if (memchr(data, '\0', len) != nullptr) return false;
  const size_t header_end = http_header_end(data, len);
  if (header_end == 0 || header_end > len) return false;

  memset(out, 0, sizeof(*out));
  out->content_length = -1;

  const char *cursor = data;
  const char *block_end = data + header_end;
  const char *line_end = static_cast<const char *>(memchr(cursor, '\n', static_cast<size_t>(block_end - cursor)));
  if (line_end == nullptr || line_end == cursor || line_end[-1] != '\r') return false;

  if (!take_token(cursor, line_end, out->method, sizeof(out->method))) return false;
  if (!take_token(cursor, line_end, out->path, sizeof(out->path))) return false;
  char version[16];
  if (!take_token(cursor, line_end, version, sizeof(version))) return false;
  if (strncmp(version, "HTTP/", 5) != 0) return false;
  while (cursor < line_end && *cursor == ' ') ++cursor;
  if (cursor != line_end - 1) return false;

  char *query = strchr(out->path, '?');
  if (query != nullptr) *query = '\0';
  if (out->path[0] != '/') return false;

  cursor = line_end + 1;
  bool saw_length = false;
  while (cursor < block_end) {
    if (cursor + 1 < block_end && cursor[0] == '\r' && cursor[1] == '\n') break;
    line_end = static_cast<const char *>(memchr(cursor, '\n', static_cast<size_t>(block_end - cursor)));
    if (line_end == nullptr || line_end == cursor || line_end[-1] != '\r') return false;
    const char *colon = static_cast<const char *>(memchr(cursor, ':', static_cast<size_t>(line_end - cursor)));
    if (colon == nullptr || colon == cursor) return false;
    const char *name = cursor;
    const size_t name_len = static_cast<size_t>(colon - cursor);
    const char *value = colon + 1;
    size_t value_len = static_cast<size_t>((line_end - 1) - value);
    while (value_len > 0 && (*value == ' ' || *value == '\t')) {
      ++value;
      --value_len;
    }
    while (value_len > 0 && (value[value_len - 1] == ' ' || value[value_len - 1] == '\t')) --value_len;

    if (name_eq(name, name_len, "content-length")) {
      if (saw_length) return false;
      saw_length = true;
      if (!parse_content_length(value, value_len, &out->content_length)) return false;
    } else if (name_eq(name, name_len, "transfer-encoding")) {
      out->chunked = true;
    } else if (name_eq(name, name_len, "x-api-token")) {
      out->has_token = true;
      copy_capped(out->token, sizeof(out->token), value, value_len, &out->token_overflow);
    } else if (name_eq(name, name_len, "origin")) {
      out->has_origin = true;
      copy_capped(out->origin, sizeof(out->origin), value, value_len, &out->origin_overflow);
    }
    cursor = line_end + 1;
  }
  return true;
}

bool ap_credentials_ok(const char *ssid, const char *password, const char *placeholder) {
  if (ssid == nullptr || password == nullptr || placeholder == nullptr) return false;
  const size_t ssid_len = strlen(ssid);
  const size_t pass_len = strlen(password);
  if (ssid_len == 0 || ssid_len > 32) return false;
  if (pass_len < 8 || pass_len > 63) return false;
  if (strcmp(password, placeholder) == 0) return false;
  return true;
}

bool token_matches(const char *expected, const char *got) {
  if (expected == nullptr || expected[0] == '\0') return true;
  if (got == nullptr) got = "";
  const size_t expected_len = strlen(expected);
  const size_t got_len = strlen(got);
  unsigned diff = expected_len == got_len ? 0u : 1u;
  const size_t shared = expected_len < got_len ? expected_len : got_len;
  for (size_t i = 0; i < shared; ++i) {
    diff |= static_cast<unsigned char>(expected[i]) ^ static_cast<unsigned char>(got[i]);
  }
  for (size_t i = shared; i < expected_len; ++i) diff |= static_cast<unsigned char>(expected[i]);
  for (size_t i = shared; i < got_len; ++i) diff |= static_cast<unsigned char>(got[i]);
  return diff == 0;
}

bool origin_allowed(const char *origin, bool has_origin) {
  if (!has_origin) return true;
  if (origin == nullptr || origin[0] == '\0') return false;
  return strcmp(origin, kConfigOrigin) == 0;
}
