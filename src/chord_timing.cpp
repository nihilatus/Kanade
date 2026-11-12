#include "chord_timing.h"

#include <stdio.h>

static constexpr uint16_t kReleaseGuardMaxMs = 500;
static constexpr uint16_t kRepeatDelayMaxMs = 5000;
static constexpr uint16_t kRepeatIntervalMaxMs = 255;

static_assert(kSwitchDebounceMs > 0, "switch debounce leaves room for a key debounce");
static_assert(kChordDebounceMs < kSwitchDebounceMs, "factory debounce is inside the runtime range");
static_assert(kChordBounceMs >= kChordDebounceMs, "bounce margin is non-negative");
static_assert(kChordReleaseGuardMs >= kChordBounceMs, "factory release covers the bounce window");
static_assert(kChordReleaseGuardMs <= kReleaseGuardMaxMs, "factory release fits the runtime cap");
static_assert(kChordRepeatSettleMs < 65535, "repeat delay has a value above settle");
static_assert(kChordRepeatDelayMs > kChordRepeatSettleMs, "factory delay is after settle");
static_assert(kChordRepeatDelayMs <= kRepeatDelayMaxMs, "factory delay fits the runtime cap");
static_assert(kChordTapGapMs * 2 + 1 <= kRepeatIntervalMaxMs, "repeat interval has a value above one tap");
static_assert(kChordRepeatIntervalMs > kChordTapGapMs * 2, "factory interval outlasts one tap");
static_assert(kChordRepeatIntervalMs <= kRepeatIntervalMaxMs, "factory interval fits the runtime cap");
static_assert(kChordBounceMs - kChordDebounceMs <= 65535, "bounce margin fits one field");

static ChordTiming g_timing = {
    kChordDebounceMs,
    static_cast<uint16_t>(kChordReleaseGuardMs),
    kChordRepeatDelayMs,
    kChordRepeatIntervalMs,
};

static void set_err(char *err, size_t err_len, const char *msg) {
  if (err == nullptr || err_len == 0 || msg == nullptr) return;
  snprintf(err, err_len, "%s", msg);
}

ChordTiming chord_timing_factory() {
  ChordTiming timing;
  timing.debounce_ms = kChordDebounceMs;
  timing.release_guard_ms = static_cast<uint16_t>(kChordReleaseGuardMs);
  timing.repeat_delay_ms = kChordRepeatDelayMs;
  timing.repeat_interval_ms = kChordRepeatIntervalMs;
  return timing;
}

ChordTimingLimits chord_timing_limits() {
  ChordTimingLimits limits;
  limits.debounce_max = static_cast<uint16_t>(kSwitchDebounceMs - 1);
  limits.release_max = kReleaseGuardMaxMs;
  limits.bounce_margin_ms = static_cast<uint16_t>(kChordBounceMs - kChordDebounceMs);
  limits.repeat_delay_min = static_cast<uint16_t>(kChordRepeatSettleMs + 1);
  limits.repeat_delay_max = kRepeatDelayMaxMs;
  limits.repeat_interval_min = static_cast<uint16_t>(kChordTapGapMs * 2 + 1);
  limits.repeat_interval_max = kRepeatIntervalMaxMs;
  return limits;
}

const ChordTiming &chord_timing_current() { return g_timing; }

uint32_t chord_timing_bounce_ms() {
  return static_cast<uint32_t>(g_timing.debounce_ms) +
         (kChordBounceMs - static_cast<uint32_t>(kChordDebounceMs));
}

bool chord_timing_accept(const ChordTiming &timing, char *err, size_t err_len) {
  const ChordTimingLimits limits = chord_timing_limits();
  if (timing.debounce_ms > limits.debounce_max) {
    char msg[64];
    snprintf(msg, sizeof(msg), "去彈跳必須是 0 到 %u", static_cast<unsigned>(limits.debounce_max));
    set_err(err, err_len, msg);
    return false;
  }
  if (timing.release_guard_ms > limits.release_max) {
    char msg[64];
    snprintf(msg, sizeof(msg), "放開保護不能超過 %u", static_cast<unsigned>(limits.release_max));
    set_err(err, err_len, msg);
    return false;
  }
  const uint32_t bounce =
      static_cast<uint32_t>(timing.debounce_ms) + limits.bounce_margin_ms;
  if (timing.release_guard_ms < bounce) {
    char msg[80];
    snprintf(msg, sizeof(msg), "放開保護至少要 %u（去彈跳加 %u）", static_cast<unsigned>(bounce),
             static_cast<unsigned>(limits.bounce_margin_ms));
    set_err(err, err_len, msg);
    return false;
  }
  if (timing.repeat_delay_ms < limits.repeat_delay_min ||
      timing.repeat_delay_ms > limits.repeat_delay_max) {
    char msg[80];
    snprintf(msg, sizeof(msg), "連發延遲必須大於 %u，而且不超過 %u",
             static_cast<unsigned>(limits.repeat_delay_min - 1),
             static_cast<unsigned>(limits.repeat_delay_max));
    set_err(err, err_len, msg);
    return false;
  }
  if (timing.repeat_interval_ms < limits.repeat_interval_min ||
      timing.repeat_interval_ms > limits.repeat_interval_max) {
    char msg[80];
    snprintf(msg, sizeof(msg), "連發間隔必須大於 %u，而且不超過 %u",
             static_cast<unsigned>(limits.repeat_interval_min - 1),
             static_cast<unsigned>(limits.repeat_interval_max));
    set_err(err, err_len, msg);
    return false;
  }
  return true;
}

void chord_timing_apply(const ChordTiming &timing) { g_timing = timing; }

void chord_timing_use_factory() { g_timing = chord_timing_factory(); }
