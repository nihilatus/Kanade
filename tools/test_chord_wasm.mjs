// Smoke test for web/chord_core.wasm. Behavior lives in the native Unity
// suite; this only checks that the browser entry is wired to it.
//
//   node tools/test_chord_wasm.mjs

import { readFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const wasmPath = join(root, "web", "chord_core.wasm");

const PHASE_IDLE = 0;
const PHASE_HOLDING = 1;
const PHASE_GUARD = 2;

function fail(message) {
  console.error(message);
  process.exit(1);
}

function pick(exports, name) {
  if (typeof exports[name] === "function") return exports[name];
  if (typeof exports[`_${name}`] === "function") return exports[`_${name}`];
  fail(`missing export ${name}`);
}

function stubImports(module) {
  const imports = {};
  for (const entry of WebAssembly.Module.imports(module)) {
    if (!imports[entry.module]) imports[entry.module] = {};
    imports[entry.module][entry.name] = () => {
      throw new Error(`unexpected import ${entry.module}.${entry.name}`);
    };
  }
  return imports;
}

function readSend(core) {
  const view = new DataView(core.memory.buffer);
  const off = core.lastPtr;
  return {
    count: view.getUint8(off),
    phase: view.getUint8(off + 1),
    mask0: view.getUint16(off + 2, true),
    mask1: view.getUint16(off + 4, true),
    peak: view.getUint16(off + 6, true),
    wakeMs: view.getUint32(off + 8, true),
  };
}

function writeRule(core, index, rule) {
  const view = new DataView(core.memory.buffer);
  const off = core.rulesPtr + index * 8;
  view.setUint16(off, rule.id, true);
  view.setUint16(off + 2, rule.required, true);
  view.setUint16(off + 4, rule.optional, true);
  view.setUint8(off + 6, rule.repeat ? 1 : 0);
  view.setUint8(off + 7, 0);
}

function step(core, mask, now) {
  core.step(mask, now);
  return readSend(core);
}

function expect(cond, message) {
  if (!cond) fail(message);
}

const bytes = await readFile(wasmPath);
const module = await WebAssembly.compile(bytes);
const instance = await WebAssembly.instantiate(module, stubImports(module));
const exp = instance.exports;
if (typeof exp._initialize === "function") exp._initialize();

const core = {
  memory: exp.memory,
  reset: pick(exp, "chord_wasm_reset"),
  setRules: pick(exp, "chord_wasm_set_rules"),
  step: pick(exp, "chord_wasm_step"),
  setMode: pick(exp, "chord_wasm_set_mode"),
  find: pick(exp, "chord_wasm_find"),
  releaseMs: pick(exp, "chord_wasm_release_ms"),
  bounceMs: pick(exp, "chord_wasm_bounce_ms"),
  ruleCap: pick(exp, "chord_wasm_rule_cap"),
  rulesPtr: pick(exp, "chord_wasm_rules_ptr")(),
  lastPtr: pick(exp, "chord_wasm_last_ptr")(),
};

expect(core.releaseMs() === 10, `release ms ${core.releaseMs()}`);
expect(core.bounceMs() === 10, `bounce ms ${core.bounceMs()}`);
expect(core.ruleCap() === 512, `rule cap ${core.ruleCap()}`);

core.reset(0);
writeRule(core, 0, { id: 5, required: 0x0001, optional: 0x0002, repeat: 0 });
writeRule(core, 1, { id: 9, required: 0x0003, optional: 0, repeat: 0 });
expect(core.setRules(2) === 0, "set_rules");
expect(core.setRules(513) === -1, "over cap should fail");
expect(core.find(0x0003) === 1, `find more-specific row, got ${core.find(0x0003)}`);
expect(core.find(0x0001) === 0, `find optional row, got ${core.find(0x0001)}`);

// Release waits 10 ms, then sends once.
core.reset(0);
let send = step(core, 0x0001, 1000);
expect(send.count === 0 && send.phase === PHASE_HOLDING && send.peak === 0x0001, "hold");
send = step(core, 0, 1500);
expect(send.count === 0 && send.phase === PHASE_GUARD && send.peak === 0x0001, "guard");
expect(send.wakeMs === 1510, `wake ${send.wakeMs}`);
send = step(core, 0, 1509);
expect(send.count === 0 && send.phase === PHASE_GUARD, "still guarding at 9 ms");
send = step(core, 0, 1510);
expect(
  send.count === 1 && send.mask0 === 0x0001 && send.phase === PHASE_IDLE && send.wakeMs === 0,
  "fires at 10 ms",
);

// A return 9 ms after release stays the same chord. At 10 ms a nested mask splits.
const ka = 0x0802;
const a = 0x0800;
const up = 100;
core.reset(0);
step(core, ka, 0);
step(core, 0, up);
send = step(core, a, up + 9);
expect(send.count === 0 && send.phase === PHASE_HOLDING && send.peak === ka, "bounce within 9 ms");

core.reset(0);
step(core, ka, 0);
step(core, 0, up);
send = step(core, a, up + 10);
expect(
  send.count === 1 && send.mask0 === ka && send.phase === PHASE_HOLDING && send.peak === a,
  "nested mask after the bounce window sends the previous chord",
);

console.log("chord wasm smoke ok");
