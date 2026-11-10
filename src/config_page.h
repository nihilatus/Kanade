#pragma once

// Phone page for the chord editor. Served from flash; no password is embedded.

static const char kConfigPage[] PROGMEM = R"html(<!DOCTYPE html>
<html lang="zh-Hant">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>和弦設定</title>
<style>
  body { margin: 0; font: 16px/1.4 sans-serif; background: #111; color: #eee; }
  header { position: sticky; top: 0; background: #1b1b1b; padding: 12px; display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
  button, input { font: inherit; }
  button { background: #2d6a4f; color: #fff; border: 0; border-radius: 8px; padding: 10px 12px; }
  button.ghost { background: #333; }
  button.warn { background: #9b2226; }
  input[type="search"], input[type="text"] { background: #222; color: #eee; border: 1px solid #444; border-radius: 8px; padding: 8px; }
  #q { flex: 1; min-width: 12rem; }
  #status { padding: 0 12px 8px; color: #adb5bd; }
  article { margin: 8px 12px; padding: 10px; background: #1c1c1c; border-radius: 10px; }
  .row { display: flex; gap: 8px; flex-wrap: wrap; align-items: center; }
  .kana { width: 5rem; }
  .output { flex: 1; min-width: 8rem; }
  .keys { display: flex; flex-wrap: wrap; gap: 6px; margin-top: 8px; }
  .chip { border: 1px solid #555; background: #222; color: #ccc; border-radius: 999px; padding: 6px 10px; }
  .chip.req { background: #40916c; color: #fff; border-color: #40916c; }
  .chip.opt { background: transparent; color: #d8f3dc; border-color: #95d5b2; }
  .meta { color: #888; font-size: 13px; }
</style>
</head>
<body>
<header>
  <input id="q" type="search" placeholder="搜尋假名、送出字或按鍵">
  <button id="save" type="button">儲存</button>
  <button id="add" class="ghost" type="button">新增一列</button>
  <button id="reset" class="warn" type="button">還原出廠表</button>
</header>
<p id="status">載入中…</p>
<main id="list"></main>
<script>
const KEYS = ["LL","LU","LR","LD","LT0","LT1","LT2","RL","RU","RR","RD","RT0","RT1","RT2"];
let rows = [];
let source = "factory";

function el(tag, cls, text) {
  const node = document.createElement(tag);
  if (cls) node.className = cls;
  if (text != null) node.textContent = text;
  return node;
}

function nextId() {
  let id = 1;
  for (const row of rows) id = Math.max(id, Number(row.id) + 1);
  return id > 65535 ? 1 : id;
}

function visible(row) {
  const q = document.getElementById("q").value.trim().toLowerCase();
  if (!q) return true;
  const blob = [row.kana, row.output, row.required.join(" "), row.optional.join(" ")].join(" ").toLowerCase();
  return blob.indexOf(q) >= 0;
}

function render() {
  const list = document.getElementById("list");
  list.replaceChildren();
  let shown = 0;
  rows.forEach((row, index) => {
    if (!visible(row)) return;
    shown += 1;
    const card = el("article");
    card.dataset.i = String(index);
    const top = el("div", "row");
    const kana = el("input");
    kana.type = "text";
    kana.className = "kana";
    kana.dataset.field = "kana";
    kana.value = row.kana || "";
    kana.setAttribute("aria-label", "假名");
    const output = el("input");
    output.type = "text";
    output.className = "output";
    output.dataset.field = "output";
    output.value = row.output || "";
    output.setAttribute("aria-label", "送出");
    const del = el("button", "ghost", "刪除");
    del.type = "button";
    del.dataset.action = "delete";
    top.append(kana, output, del);
    const keys = el("div", "keys");
    for (const key of KEYS) {
      const onReq = row.required.indexOf(key) >= 0;
      const onOpt = row.optional.indexOf(key) >= 0;
      const chip = el("button", "chip" + (onReq ? " req" : onOpt ? " opt" : ""), key);
      chip.type = "button";
      chip.dataset.key = key;
      keys.append(chip);
    }
    card.append(top, keys, el("div", "meta", "點按鍵：必按 → 可選 → 關閉"));
    list.append(card);
  });
  document.getElementById("status").textContent =
    (source === "saved" ? "使用已儲存的表" : "使用出廠表") +
    " · " + rows.length + " 列" + (shown === rows.length ? "" : " · 顯示 " + shown);
}

async function load() {
  const response = await fetch("/api/chords");
  const data = await response.json();
  rows = data.chords || [];
  source = data.source || "factory";
  render();
}

document.getElementById("q").addEventListener("input", render);
document.getElementById("add").addEventListener("click", () => {
  rows.push({id: nextId(), kana: "", output: "", required: ["LL"], optional: []});
  render();
});
document.getElementById("list").addEventListener("input", (event) => {
  const card = event.target.closest("[data-i]");
  if (!card) return;
  const row = rows[Number(card.dataset.i)];
  if (event.target.dataset.field === "kana") row.kana = event.target.value;
  if (event.target.dataset.field === "output") row.output = event.target.value;
});
document.getElementById("list").addEventListener("click", (event) => {
  const card = event.target.closest("[data-i]");
  if (!card) return;
  const index = Number(card.dataset.i);
  const row = rows[index];
  if (event.target.dataset.action === "delete") {
    rows.splice(index, 1);
    render();
    return;
  }
  const key = event.target.dataset.key;
  if (!key) return;
  const req = row.required.filter((item) => item !== key);
  const opt = row.optional.filter((item) => item !== key);
  if (row.required.indexOf(key) >= 0) opt.push(key);
  else if (row.optional.indexOf(key) < 0) req.push(key);
  row.required = KEYS.filter((item) => req.indexOf(item) >= 0);
  row.optional = KEYS.filter((item) => opt.indexOf(item) >= 0);
  render();
});
document.getElementById("save").addEventListener("click", async () => {
  document.getElementById("status").textContent = "儲存中…";
  const response = await fetch("/api/chords", {
    method: "POST",
    headers: {"Content-Type": "application/json"},
    body: JSON.stringify({chords: rows})
  });
  const data = await response.json().catch(() => ({}));
  if (!response.ok) {
    document.getElementById("status").textContent = data.error || "儲存失敗";
    return;
  }
  await load();
});
document.getElementById("reset").addEventListener("click", async () => {
  if (!confirm("還原成燒錄時的和弦表？手機上改過的表會刪除。")) return;
  const response = await fetch("/api/chords/reset", {method: "POST"});
  const data = await response.json().catch(() => ({}));
  if (!response.ok) {
    document.getElementById("status").textContent = data.error || "還原失敗";
    return;
  }
  await load();
});
load().catch(() => {
  document.getElementById("status").textContent = "無法載入和弦表";
});
</script>
</body>
</html>)html";
