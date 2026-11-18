#include "config_route.h"

#include <string.h>

#include "settings.h"

static bool api_path(const char *path) { return path != nullptr && strncmp(path, "/api/", 5) == 0; }

static bool known_path(const char *path) {
  return strcmp(path, "/") == 0 || strcmp(path, "/api/chords") == 0 ||
         strcmp(path, "/api/chords/reset") == 0 || strcmp(path, "/api/fire") == 0 ||
         strcmp(path, "/api/timing") == 0 || strcmp(path, "/api/timing/reset") == 0;
}

ConfigRouteId config_route_id(const char *path) {
  if (path == nullptr) return ConfigRouteId::None;
  if (strcmp(path, "/") == 0) return ConfigRouteId::Page;
  if (strcmp(path, "/api/chords") == 0) return ConfigRouteId::Chords;
  if (strcmp(path, "/api/chords/reset") == 0) return ConfigRouteId::Reset;
  if (strcmp(path, "/api/fire") == 0) return ConfigRouteId::Fire;
  if (strcmp(path, "/api/timing") == 0) return ConfigRouteId::Timing;
  if (strcmp(path, "/api/timing/reset") == 0) return ConfigRouteId::TimingReset;
  return ConfigRouteId::None;
}

static ConfigRouteDecision drain(ConfigRouteDrain kind, int code, const char *message, bool cors,
                                 bool drain_body) {
  ConfigRouteDecision decision{};
  decision.step = ConfigRouteStep::Drain;
  decision.drain = kind;
  decision.code = code;
  decision.message = message;
  decision.cors = cors;
  decision.drain_body = drain_body;
  return decision;
}

static ConfigRouteDecision go(ConfigRouteStep step, bool cors) {
  ConfigRouteDecision decision{};
  decision.step = step;
  decision.cors = cors;
  return decision;
}

ConfigRouteDecision decide_config_route(const HttpRequest &req, bool token_ok) {
  const bool api = api_path(req.path);
  const bool cors = api && req.has_origin && !req.origin_overflow && origin_allowed(req.origin, true);
  const ConfigRouteId route = config_route_id(req.path);

  if (strcmp(req.method, "OPTIONS") == 0) {
    if (!known_path(req.path)) return drain(ConfigRouteDrain::Error, 404, "not found", cors, true);
    return drain(ConfigRouteDrain::Status, 204, nullptr, cors, true);
  }
  if (route == ConfigRouteId::None) return drain(ConfigRouteDrain::Error, 404, "not found", false, true);
  if (api && (req.origin_overflow || !origin_allowed(req.origin, req.has_origin))) {
    return drain(ConfigRouteDrain::Error, 403, "來源不符", false, true);
  }
  if (api && !token_ok) return drain(ConfigRouteDrain::Error, 401, "請輸入 API token", cors, true);
  if (req.chunked) return drain(ConfigRouteDrain::Error, 400, "不支援分段上傳", cors, true);

  if (route == ConfigRouteId::Page) {
    if (strcmp(req.method, "GET") != 0) {
      return drain(ConfigRouteDrain::Error, 404, "not found", false, true);
    }
    return go(ConfigRouteStep::SendPage, false);
  }

  long length = req.content_length;
  if (length < 0) length = 0;

  if (route == ConfigRouteId::Reset) {
    if (strcmp(req.method, "POST") != 0) {
      return drain(ConfigRouteDrain::Error, 404, "not found", cors, true);
    }
    if (length > kDrainMax) return drain(ConfigRouteDrain::Error, 413, "內容太大", cors, false);
    return drain(ConfigRouteDrain::Reset, 0, nullptr, cors, true);
  }

  if (route == ConfigRouteId::TimingReset) {
    if (strcmp(req.method, "POST") != 0) {
      return drain(ConfigRouteDrain::Error, 404, "not found", cors, true);
    }
    if (length > kDrainMax) return drain(ConfigRouteDrain::Error, 413, "內容太大", cors, false);
    return drain(ConfigRouteDrain::TimingReset, 0, nullptr, cors, true);
  }

  if (route == ConfigRouteId::Fire && strcmp(req.method, "GET") == 0) {
    if (length > 0) return drain(ConfigRouteDrain::Error, 400, "請求格式不對", cors, true);
    return go(ConfigRouteStep::SendFire, cors);
  }

  if (route == ConfigRouteId::Timing && strcmp(req.method, "GET") == 0) {
    if (length > 0) return drain(ConfigRouteDrain::Error, 400, "請求格式不對", cors, true);
    return go(ConfigRouteStep::SendTiming, cors);
  }

  const bool post_chords = route == ConfigRouteId::Chords && strcmp(req.method, "POST") == 0;
  const bool post_fire = route == ConfigRouteId::Fire && strcmp(req.method, "POST") == 0;
  const bool post_timing = route == ConfigRouteId::Timing && strcmp(req.method, "POST") == 0;
  const bool get_chords = route == ConfigRouteId::Chords && strcmp(req.method, "GET") == 0;
  if (get_chords) {
    if (length > 0) return drain(ConfigRouteDrain::Error, 400, "請求格式不對", cors, true);
    return go(ConfigRouteStep::QueueGet, cors);
  }
  if (!post_chords && !post_fire && !post_timing) {
    return drain(ConfigRouteDrain::Error, 404, "not found", cors, true);
  }
  if (length == 0) return drain(ConfigRouteDrain::Error, 400, "沒有內容", cors, true);
  const long cap = post_fire    ? static_cast<long>(kFireBodyMax)
                   : post_timing ? static_cast<long>(kTimingBodyMax)
                                 : static_cast<long>(kChordsJsonMax);
  if (length > cap) return drain(ConfigRouteDrain::Error, 413, "內容太大", cors, true);
  if (post_fire) return go(ConfigRouteStep::ReadFire, cors);
  if (post_timing) return go(ConfigRouteStep::ReadTiming, cors);
  return go(ConfigRouteStep::ReadChords, cors);
}
