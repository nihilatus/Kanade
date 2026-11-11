// Browser entry for the practice page. PlatformIO never compiles this file:
// it lives outside src/, and the firmware keeps calling chord_core directly.
//
// One static session. No heap. The page writes rules into chord_wasm_rules_ptr()
// and then calls chord_wasm_set_rules. Sends follow src/chords.cpp
// dispatch_chord_step: engine step, then repeat, and a repeat that already
// tapped swallows the release copy.
//
// ChordWasmRule at chord_wasm_rules_ptr(), little-endian:
//   0 u16 id, 2 u16 required, 4 u16 optional, 6 u8 repeat, 7 u8 pad
// ChordWasmSend at chord_wasm_last_ptr(), little-endian:
//   0 u8 count, 1 u8 phase, 2 u16 mask0, 4 u16 mask1, 6 u16 peak, 8 u32 wake_ms
// phase is ChordPhase: Idle 0, Holding 1, ReleaseGuard 2, Draining 3.
// wake_ms is an absolute time on the caller's uint32 clock, or 0 if the page
// should not schedule another step. count is 0, 1, or 2.

#include "chord_core.h"

#include <stddef.h>
#include <stdint.h>

static constexpr uint32_t kChordWasmRuleCap = 512;

struct ChordWasmRule {
  uint16_t id;
  uint16_t required;
  uint16_t optional;
  uint8_t repeat;
  uint8_t pad;
};

struct ChordWasmSend {
  uint8_t count;
  uint8_t phase;
  uint16_t mask0;
  uint16_t mask1;
  uint16_t peak;
  uint32_t wake_ms;
};

static_assert(sizeof(ChordWasmRule) == 8, "rule stride");
static_assert(offsetof(ChordWasmRule, repeat) == 6, "rule repeat");
static_assert(sizeof(ChordWasmSend) == 12, "send size");
static_assert(offsetof(ChordWasmSend, mask0) == 2, "send mask0");
static_assert(offsetof(ChordWasmSend, wake_ms) == 8, "send wake");

static ChordWasmRule g_rules_in[kChordWasmRuleCap];
static ChordRule g_rules[kChordWasmRuleCap];
static uint32_t g_rule_count = 0;
static ChordEngine g_engine;
static ChordRepeat g_repeat;
static ChordWasmSend g_last;
static const char kEmpty[] = "";

static bool peak_auto_repeats(uint16_t mask) {
  if (mask == 0 || g_rule_count == 0) return false;
  const int index = chord_find_rule(g_rules, g_rule_count, mask);
  if (index < 0) return false;
  return g_rules[index].repeat;
}

// Soonest strictly-future deadline. A time that is already due is 0 so the
// page does not spin: the step that observes it performs the send.
static uint32_t future_at(uint32_t now_ms, uint32_t at) {
  if (static_cast<int32_t>(at - now_ms) <= 0) return 0;
  return at;
}

static uint32_t sooner(uint32_t now_ms, uint32_t wake, uint32_t at) {
  const uint32_t next = future_at(now_ms, at);
  if (next == 0) return wake;
  if (wake == 0 || static_cast<int32_t>(next - wake) < 0) return next;
  return wake;
}

static uint32_t wake_at(uint32_t now_ms) {
  uint32_t wake = 0;
  if (g_engine.phase == ChordPhase::ReleaseGuard) {
    wake = sooner(now_ms, wake, g_engine.guard_since + kChordReleaseGuardMs);
  }
  if (g_engine.phase != ChordPhase::Holding || g_engine.peak.mask == 0) return wake;
  if (g_repeat.mask != g_engine.peak.mask || !peak_auto_repeats(g_engine.peak.mask)) return wake;
  if (g_repeat.phase == ChordRepeatPhase::Watching) {
    wake = sooner(now_ms, wake, g_repeat.since_ms + kChordRepeatSettleMs);
  } else if (g_repeat.phase == ChordRepeatPhase::Sent) {
    wake = sooner(now_ms, wake, g_repeat.since_ms + kChordRepeatDelayMs);
  } else if (g_repeat.phase == ChordRepeatPhase::Repeating) {
    wake = sooner(now_ms, wake, g_repeat.next_ms);
  }
  return wake;
}

static void push_mask(uint16_t mask) {
  if (mask == 0 || g_last.count >= 2) return;
  if (g_last.count == 0) g_last.mask0 = mask;
  else g_last.mask1 = mask;
  g_last.count = static_cast<uint8_t>(g_last.count + 1);
}

static void publish(const ChordStep &step, uint32_t now_ms) {
  const bool holding = g_engine.phase == ChordPhase::Holding;
  const uint16_t peak = holding ? g_engine.peak.mask : 0;
  const ChordRepeatStep rep =
      chord_repeat_step(&g_repeat, g_engine.phase, peak, holding && peak_auto_repeats(peak),
                        step.fire, step.fire_mask, now_ms);
  g_last.count = 0;
  g_last.mask0 = 0;
  g_last.mask1 = 0;
  if (rep.send) push_mask(rep.mask);
  if (step.fire && !rep.swallow_fire) push_mask(step.fire_mask);
  g_last.phase = static_cast<uint8_t>(g_engine.phase);
  g_last.peak = g_engine.peak.mask;
  g_last.wake_ms = wake_at(now_ms);
}

extern "C" {

void chord_wasm_reset(uint32_t mode) {
  chord_engine_reset(&g_engine);
  chord_repeat_reset(&g_repeat);
  g_engine.mode = mode == 1 ? ChordFireMode::FirstUp : ChordFireMode::ReleaseAll;
  g_last.count = 0;
  g_last.mask0 = 0;
  g_last.mask1 = 0;
  g_last.phase = static_cast<uint8_t>(ChordPhase::Idle);
  g_last.peak = 0;
  g_last.wake_ms = 0;
}

// Copies the packed rules the page just wrote. -1 leaves the previous table
// in place. kana and output stay empty: matching and repeat only need the bits.
int chord_wasm_set_rules(uint32_t count) {
  if (count > kChordWasmRuleCap) return -1;
  g_rule_count = count;
  for (uint32_t i = 0; i < count; ++i) {
    ChordRule &row = g_rules[i];
    row.id = g_rules_in[i].id;
    row.required = g_rules_in[i].required;
    row.optional = g_rules_in[i].optional;
    row.kana = kEmpty;
    row.output = kEmpty;
    row.repeat = g_rules_in[i].repeat != 0;
  }
  return 0;
}

void chord_wasm_step(uint32_t mask, uint32_t now_ms) {
  const ChordStep step = chord_engine_step(&g_engine, static_cast<uint16_t>(mask), now_ms);
  publish(step, now_ms);
}

void chord_wasm_set_mode(uint32_t mode, uint32_t now_ms) {
  const ChordFireMode next = mode == 1 ? ChordFireMode::FirstUp : ChordFireMode::ReleaseAll;
  const ChordStep step = chord_engine_set_mode(&g_engine, next);
  publish(step, now_ms);
}

int chord_wasm_find(uint32_t mask) {
  return chord_find_rule(g_rules, g_rule_count, static_cast<uint16_t>(mask));
}

uint32_t chord_wasm_release_ms(void) { return kChordReleaseGuardMs; }

uint32_t chord_wasm_bounce_ms(void) { return kChordBounceMs; }

uint32_t chord_wasm_rule_cap(void) { return kChordWasmRuleCap; }

uint32_t chord_wasm_rules_ptr(void) {
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_rules_in));
}

uint32_t chord_wasm_last_ptr(void) {
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_last));
}

}
