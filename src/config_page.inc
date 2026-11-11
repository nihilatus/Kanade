R"HTML(
<!DOCTYPE html>
<html lang="zh-Hant">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Kanade</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body {
    margin: 0;
    font-family: sans-serif;
    background: #111;
    color: #eee;
  }
  header {
    position: sticky;
    top: 0;
    z-index: 2;
    background: #1b1b1b;
    border-bottom: 1px solid #333;
    padding: 12px;
  }
  .wrap { max-width: 760px; margin: 0 auto; }
  h1 { font-size: 18px; margin: 0 0 8px; }
  .hint { color: #aaa; font-size: 13px; margin: 0 0 10px; line-height: 1.4; }
  #q, .output {
    width: 100%;
    font-size: 16px;
    padding: 10px;
    border-radius: 8px;
    border: 1px solid #444;
    background: #0e0e0e;
    color: #eee;
  }
  .actions { display: flex; gap: 8px; margin-top: 10px; }
  button {
    font-size: 16px;
    padding: 10px 14px;
    border-radius: 8px;
    border: 1px solid #555;
    background: #2a2a2a;
    color: #eee;
  }
  #save { background: #1d6b3a; border-color: #1d6b3a; }
  #reset { background: transparent; color: #ffb4b4; border-color: #8a4040; }
  button:disabled { opacity: 0.5; }
  button.mode:disabled { opacity: 1; cursor: default; }
  button.mode:disabled:not(.on) { opacity: 0.55; }
  button.on { background: #1d4e86; border-color: #6cb6ff; }
  #status { display: block; min-height: 1.3em; margin-top: 8px; font-size: 14px; }
  #status.bad { color: #ff8d8d; }
  #status.ok { color: #8ee0a8; }
  .row { padding: 12px; border-bottom: 1px solid #2a2a2a; }
  .row.hidden, .hidden { display: none; }
  .kana { font-size: 22px; margin-right: 8px; }
  .meta { color: #888; font-size: 12px; }
  .hands { display: flex; flex-wrap: wrap; gap: 8px; margin: 8px 0; }
  .hand { display: flex; flex-wrap: wrap; gap: 6px; }
  .key {
    min-width: 58px;
    padding: 4px 6px 6px;
    border: 1px solid #444;
    border-radius: 8px;
    background: #1c1c1c;
  }
  .key.on-req { border-color: #6cb6ff; background: #16324d; }
  .key.on-opt { border-color: #e0b84a; background: #3a3214; }
  .kname { font-size: 13px; font-weight: 700; }
  .key label { display: flex; align-items: center; gap: 4px; font-size: 13px; margin-top: 2px; }
  .key input { width: 18px; height: 18px; }
  .empty-hint { color: #888; font-size: 12px; margin-top: 4px; }
  #none { padding: 16px; color: #aaa; }
</style>
</head>
<body>
<div class="wrap">
  <header>
    <h1>Kanade</h1>
    <p class="hint">假名列填羅馬字。符號與操作列從清單選鍵。必＝一定要按。可＝可有可無。同一顆不能兩個都勾。假名只供辨識。output 空白表示這列不送鍵。</p>
    <div class="actions">
      <button id="fire-release" class="mode" type="button" disabled>全放再送</button>
      <button id="fire-first" class="mode" type="button" disabled>首鍵放開就送</button>
    </div>
    <p class="hint">送出時機由 GPIO17 開關決定，這頁只顯示。開路是全放再送，接到 GND 是首鍵放開就送。全放再送：這組全部放開並再確認空檔才送出。下一組若在前一組還沒放完就按下，兩組會併成一個對不到的集合。首鍵放開就送：高峰後第一顆放開就送出，其餘鍵要全部放開才接下一個。請同時按下、再放開。</p>
    <input id="q" type="search" placeholder="搜尋假名或 output" autocomplete="off">
    <div class="actions">
      <button id="save" type="button">儲存</button>
      <button id="reset" type="button">還原出廠</button>
    </div>
    <input id="api-token" class="hidden" type="password" placeholder="開機序列埠印過的 API token" autocomplete="off">
    <span id="status"></span>
  </header>
  <p id="none" class="hidden">沒有符合的列</p>
  <div id="list"></div>
</div>
<script>
const KEYS = ["LL","LU","LR","LD","LT0","LT1","LT2","RL","RU","RR","RD","RT0","RT1","RT2"];
const KEY_OPTIONS = [
  ["Hankaku", "半角/全角"],
  ["Henkan", "変換"],
  ["Muhenkan", "無変換"],
  ["Kana", "カタカナひらがな"],
  ["Eisuu", "英数（macOS）"],
  ["Yen", "¥"],
  ["Ro", "ろ"],
  ["Backspace", "退格"],
  ["Delete", "Delete"],
  ["Space", "空白"],
  ["Enter", "Enter"],
  ["Esc", "Esc"],
  ["Tab", "Tab"],
  ["Up", "上"],
  ["Down", "下"],
  ["Left", "左"],
  ["Right", "右"],
  ["Home", "Home"],
  ["End", "End"],
  ["PageUp", "PageUp"],
  ["PageDown", "PageDown"],
  ["Insert", "Insert"],
  ["CapsLock", "Caps Lock"],
  ["F1", "F1"], ["F2", "F2"], ["F3", "F3"], ["F4", "F4"],
  ["F5", "F5"], ["F6", "F6"], ["F7", "F7"], ["F8", "F8"],
  ["F9", "F9"], ["F10", "F10"], ["F11", "F11"], ["F12", "F12"]
];
const qInput = document.getElementById("q");
const list = document.getElementById("list");
const none = document.getElementById("none");
const statusEl = document.getElementById("status");
const tokenInput = document.getElementById("api-token");
let apiToken = "";
try { apiToken = sessionStorage.getItem("jisApiToken") || ""; } catch (e) { apiToken = ""; }
tokenInput.value = apiToken;
const saveBtn = document.getElementById("save");
const resetBtn = document.getElementById("reset");
const fireReleaseBtn = document.getElementById("fire-release");
const fireFirstBtn = document.getElementById("fire-first");
let fireMode = "release";
let rows = [];
let source = "factory";
let busy = false;

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text != null) node.textContent = text;
  return node;
}

function setStatus(text, kind) {
  statusEl.textContent = text;
  statusEl.className = kind || "";
}

function sourceLabel() {
  const name = source === "saved" ? "已存檔" : "出廠表";
  return name + " · " + rows.length + " 列";
}

function norm(row) {
  const required = Array.isArray(row.required) ? row.required : [];
  const optional = Array.isArray(row.optional) ? row.optional : [];
  return {
    id: row.id,
    kana: row.kana || "",
    output: row.output || "",
    repeat: row.repeat === true,
    required: KEYS.filter((name) => required.indexOf(name) !== -1),
    optional: KEYS.filter((name) => optional.indexOf(name) !== -1 && required.indexOf(name) === -1)
  };
}

function paintKey(box, row, name) {
  box.classList.toggle("on-req", row.required.indexOf(name) !== -1);
  box.classList.toggle("on-opt", row.optional.indexOf(name) !== -1);
}

function renderRow(row, index) {
  const root = el("section", "row");
  root.dataset.index = String(index);
  const title = el("div");
  const kana = el("span", "kana", row.kana || "（無假名）");
  const meta = el("span", "meta", "#" + row.id);
  title.appendChild(kana);
  title.appendChild(meta);
  root.appendChild(title);

  const hands = el("div", "hands");
  [["左", 0, 7], ["右", 7, 14]].forEach((part) => {
    const hand = el("div", "hand");
    for (let i = part[1]; i < part[2]; i++) {
      const name = KEYS[i];
      const box = el("div", "key");
      box.dataset.key = name;
      box.appendChild(el("div", "kname", name));
      ["required", "optional"].forEach((role) => {
        const label = el("label");
        const input = document.createElement("input");
        input.type = "checkbox";
        input.dataset.role = role;
        input.checked = row[role].indexOf(name) !== -1;
        label.appendChild(input);
        label.appendChild(document.createTextNode(role === "required" ? "必" : "可"));
        box.appendChild(label);
      });
      paintKey(box, row, name);
      hand.appendChild(box);
    }
    hands.appendChild(hand);
  });
  root.appendChild(hands);

  const id = Number(row.id);
  const symbolRow = id >= 4 && id <= 31;
  let output;
  if (symbolRow) {
    output = document.createElement("select");
    output.className = "output";
    output.dataset.role = "output";
    const known = KEY_OPTIONS.some((item) => item[0] === row.output);
    if (row.output !== "" && !known) {
      const keep = document.createElement("option");
      keep.value = row.output;
      keep.textContent = "目前配方：" + row.output;
      output.appendChild(keep);
    }
    const blank = document.createElement("option");
    blank.value = "";
    blank.textContent = "不送鍵";
    output.appendChild(blank);
    KEY_OPTIONS.forEach((item) => {
      const opt = document.createElement("option");
      opt.value = item[0];
      opt.textContent = item[1];
      output.appendChild(opt);
    });
    output.value = row.output;
  } else {
    output = document.createElement("input");
    output.className = "output";
    output.type = "text";
    output.dataset.role = "output";
    output.value = row.output;
    output.maxLength = 63;
    output.placeholder = "output，例如 nn、Shift+1、Backspace";
    output.autocomplete = "off";
    output.autocapitalize = "off";
    output.spellcheck = false;
  }
  root.appendChild(output);
  const hint = el("div", "empty-hint", "這列不送鍵");
  hint.hidden = row.output !== "";
  root.appendChild(hint);
  return root;
}

function render() {
  const frag = document.createDocumentFragment();
  rows.forEach((row, index) => frag.appendChild(renderRow(row, index)));
  list.replaceChildren(frag);
  applyFilter();
}

function applyFilter() {
  const q = qInput.value.trim().toLowerCase();
  let shown = 0;
  Array.from(list.children).forEach((node) => {
    const row = rows[Number(node.dataset.index)];
    const hay = ((row.kana || "") + "\n" + (row.output || "")).toLowerCase();
    const ok = q === "" || hay.indexOf(q) !== -1;
    node.classList.toggle("hidden", !ok);
    if (ok) shown++;
  });
  none.classList.toggle("hidden", shown !== 0);
  if (!statusEl.classList.contains("bad") && !statusEl.classList.contains("ok")) {
    setStatus(sourceLabel() + " · 顯示 " + shown);
  }
}

function payload() {
  return {
    chords: rows.map((row) => ({
      id: row.id,
      required: KEYS.filter((name) => row.required.indexOf(name) !== -1),
      optional: KEYS.filter((name) => row.optional.indexOf(name) !== -1),
      kana: row.kana,
      output: row.output,
      repeat: row.repeat === true
    }))
  };
}

function clientError() {
  for (let i = 0; i < rows.length; i++) {
    const row = rows[i];
    for (let k = 0; k < KEYS.length; k++) {
      const name = KEYS[k];
      if (row.required.indexOf(name) !== -1 && row.optional.indexOf(name) !== -1) {
        return "第 " + (i + 1) + " 列的同一顆鍵不能又必按又可選";
      }
    }
    if (row.output.length > 63) return "第 " + (i + 1) + " 列的 output 太長";
    for (let c = 0; c < row.output.length; c++) {
      const code = row.output.charCodeAt(c);
      if (code < 32 || code > 126) return "第 " + (i + 1) + " 列的 output 只能用英數與符號";
    }
  }
  return "";
}

function apiFetch(url, opts) {
  const next = opts ? Object.assign({}, opts) : {};
  const headers = Object.assign({}, next.headers || {});
  if (apiToken) headers["X-Api-Token"] = apiToken;
  next.headers = headers;
  return fetch(url, next);
}

async function readBody(res) {
  const text = await res.text();
  let data = {};
  try { data = JSON.parse(text); } catch (e) { data = {}; }
  if (res.status === 401) {
    tokenInput.classList.remove("hidden");
    tokenInput.focus();
    throw new Error(data.error || "請輸入 API token");
  }
  if (!res.ok || data.ok === false) throw new Error(data.error || "要求失敗");
  return data;
}

function paintFire() {
  fireReleaseBtn.classList.toggle("on", fireMode === "release");
  fireFirstBtn.classList.toggle("on", fireMode === "first");
}

async function loadFire() {
  const data = await readBody(await apiFetch("/api/fire"));
  fireMode = data.mode === "first" ? "first" : "release";
  paintFire();
}

async function load() {
  const data = await readBody(await apiFetch("/api/chords"));
  rows = (data.chords || []).map(norm);
  source = data.source === "saved" ? "saved" : "factory";
  render();
  setStatus(sourceLabel());
}

async function withBusy(fn) {
  if (busy) return;
  busy = true;
  saveBtn.disabled = true;
  resetBtn.disabled = true;
  try {
    await fn();
  } catch (err) {
    setStatus(err && err.message ? err.message : "失敗", "bad");
  } finally {
    busy = false;
    saveBtn.disabled = false;
    resetBtn.disabled = false;
  }
}

function applyOutput(input) {
  if (!input || input.dataset.role !== "output") return;
  const rowEl = input.closest(".row");
  const row = rows[Number(rowEl.dataset.index)];
  row.output = input.value;
  rowEl.querySelector(".empty-hint").hidden = input.value !== "";
}

list.addEventListener("change", (ev) => {
  const input = ev.target;
  if (input && input.dataset.role === "output") {
    applyOutput(input);
    return;
  }
  if (!input || input.type !== "checkbox") return;
  const rowEl = input.closest(".row");
  const box = input.closest(".key");
  const row = rows[Number(rowEl.dataset.index)];
  const name = box.dataset.key;
  const role = input.dataset.role;
  const other = role === "required" ? "optional" : "required";
  row.required = row.required.filter((key) => key !== name);
  row.optional = row.optional.filter((key) => key !== name);
  if (input.checked) {
    const otherInput = box.querySelector('[data-role="' + other + '"]');
    otherInput.checked = false;
    row[role].push(name);
  }
  paintKey(box, row, name);
});

list.addEventListener("input", (ev) => {
  applyOutput(ev.target);
});

qInput.addEventListener("input", () => {
  statusEl.className = "";
  applyFilter();
});

saveBtn.addEventListener("click", () => withBusy(async () => {
  const problem = clientError();
  if (problem) throw new Error(problem);
  const data = await readBody(await apiFetch("/api/chords", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload())
  }));
  source = "saved";
  setStatus("已儲存，下一組和弦就用新表（" + (data.count != null ? data.count : rows.length) + " 列）", "ok");
}));

resetBtn.addEventListener("click", () => withBusy(async () => {
  if (!window.confirm("還原成燒錄時的出廠表？手機上存過的組合會刪掉。")) return;
  const data = await readBody(await apiFetch("/api/chords/reset", { method: "POST" }));
  rows = (data.chords || []).map(norm);
  source = "factory";
  render();
  setStatus("已還原成出廠表", "ok");
}));

tokenInput.addEventListener("change", () => {
  apiToken = tokenInput.value;
  try { sessionStorage.setItem("jisApiToken", apiToken); } catch (e) {}
  withBusy(async () => {
    await load();
    await loadFire();
  });
});

paintFire();
load().catch((err) => setStatus(err && err.message ? err.message : "讀不到和弦表", "bad"));
loadFire().catch((err) => setStatus(err && err.message ? err.message : "讀不到送出模式", "bad"));
</script>
</body>
</html>
)HTML"
