#pragma once

#include <stdint.h>

#include "config_http.h"

// What to do with one parsed config request. No sockets.
// token_ok is computed by the device. An empty stored token is not ok.

enum class ConfigRouteStep : uint8_t {
  Drain = 0,
  SendPage,
  SendFire,
  SendTiming,
  QueueGet,
  ReadChords,
  ReadFire,
  ReadTiming,
};

enum class ConfigRouteDrain : uint8_t { Error = 0, Status, Reset, TimingReset };

enum class ConfigRouteId : uint8_t {
  None = 0,
  Page,
  Chords,
  Reset,
  Fire,
  Timing,
  TimingReset,
};

struct ConfigRouteDecision {
  ConfigRouteStep step;
  ConfigRouteDrain drain;
  int code;
  const char *message;
  bool cors;
  // True: discard the declared body before answering. False: answer now.
  bool drain_body;
};

ConfigRouteId config_route_id(const char *path);

ConfigRouteDecision decide_config_route(const HttpRequest &req, bool token_ok);
