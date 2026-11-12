# Machine chord map

Source of truth (human / editable): [`../esp32-jis-chordboard-premapping.csv`](../esp32-jis-chordboard-premapping.csv)

Generated machine table: [`../data/chord_map.machine.csv`](../data/chord_map.machine.csv)

`pio run` rebuilds this table before compile. To regenerate the CSV without compiling: `python tools/build_chord_map_machine.py`

That command rewrites the machine CSV and [`../include/chord_map.inc`](../include/chord_map.inc). Unknown key tokens, the same key set mapped to different outputs, or an output the firmware would skip make the script exit non-zero and fail the PlatformIO build. A skipped token is a bad combo (`Shift+Nope`), a byte outside printable ASCII, or a token longer than `kChordTokenMax` (`include/settings.h`). On the device that token is only logged; the rest of the recipe can still be sent. When more than `kChordSkipCap` tokens are skipped, later ones are counted without their text, and the valid tokens are still sent. An output that does not fit in the event buffer is not sent at all. The factory table is not allowed to rely on that. A bare word that is not a key name, such as `Backspce`, is typed as letters. It is not a skip. `Shift+Backspce` is a skip, and the build rejects it. Firmware uses the generated include as the factory table. A chord table saved from the phone is stored in LittleFS as `chords.json` and overrides the factory table until the page’s reset action deletes it.

The browser practice page [`../web/index.html`](../web/index.html) loads [`../web/chord_core.wasm`](../web/chord_core.wasm), built from [`../src/chord_core.cpp`](../src/chord_core.cpp), and fetches this same CSV (serve the repo root; see the README). “載入 CSV” can try another file with these columns; “改回機器表” loads this one again. Matching, the peak, both send timings, and repeat run in that module. The page does not keep a second copy.

## 人編 CSV

手改的是 [`../esp32-jis-chordboard-premapping.csv`](../esp32-jis-chordboard-premapping.csv)。檔首要有 UTF-8 BOM，Excel 才會把中文和假名當 UTF-8 開啟。第一行固定是：

```text
section,label,output,left_fingers,right_fingers,left_thumb,right_thumb,repeat
```

| 欄 | 意義 |
| --- | --- |
| `section` | 區段名，例如 `單母音`、`傳統 た 行`、`符號`。空白表示沿用上一列。新區段寫在該區段第一列 |
| `label` | 給人看的字：假名（`あ`）、符號（`「`、`★ ☆`），或功能名（`退格`、`空白（變換）`）。方向鍵、Enter 可以空白。腳本抄進機器表的 `kana` |
| `output` | 實際送出的配方，例如 `a`、`[`、`Backspace`、`Shift+1` |
| `left_fingers` / `right_fingers` | 手指鍵。左手 `LL` `LU` `LR` `LD`，右手 `RL` `RU` `RR` `RD`。鍵名用 `+` 連接，例如 `LL+LU`。建議打法見 [打法](playing.md) |
| `left_thumb` / `right_thumb` | 拇指。`(RT0)` 表示這顆可按可不按；裸 `RT0` 是必按 |
| `repeat` | `allow` 或 `no`。`allow` 在高峰仍按著時可以連送 |

`label` 和 `output` 都空的列是空位，留在人編表裡，機器表略過，也不檢查 `repeat`。有顯示字或配方的列，`repeat` 必須是 `allow` 或 `no`。

儲存格裡不要換行。鍵名大小寫必須是 `LL` 這組。列的順序就是機器表 `id` 的順序，不要整表排序。`、` 的輸出是逗號，存檔時那一格要保持引號。

Excel 請另存「CSV UTF-8 (逗號分隔)」。傳統「CSV (逗號分隔)」在繁中 Windows 會寫成 CP950，假名會壞。「Unicode 文字」是 UTF-16，建置會停。表頭少一欄、多一欄，或結尾多一個逗號，也會停。

## Columns

| Column | Meaning |
| --- | --- |
| `id` | Stable 1-based row id in generation order |
| `kana` | Display kana / symbol (may be empty for pure key ops) |
| `output` | Romaji / key recipe sent on match (`a`, `nn`, `Shift+1`, `Backspace`, `Ctrl+i`, …) |
| `keys_required` | Sorted unique **required** keys, `+`-joined. Does **not** include optional RT0 |
| `keys_optional` | Sorted unique optional keys (`RT0` or empty) |
| `category` | Short tag copied from the human `section` cell; blank there keeps the previous tag. May be empty |
| `repeat` | `allow` or `no`, copied from the human `repeat` cell. `allow` may tap while that peak is still held (step 5) |

Empty `label` **and** empty `output` rows in the human CSV are spacing / reserved slots — they are **omitted** here (no no-op rows).

## Physical key ids (14)

All discrete buttons (no analog sticks):

`LL` `LU` `LR` `LD` `LT0` `LT1` `LT2` `RL` `RU` `RR` `RD` `RT0` `RT1` `RT2`

Canonical sort order in CSV fields is the order above.

## RT0 optional rule

In the human table, `(RT0)` means RT0 **may or may not** be pressed; output is the same.

Machine encoding: one row with `keys_optional=RT0` (RT0 is **not** listed in `keys_required`).

Bare `RT0` (no parentheses) is a normal required key (e.g. あ → `keys_required=RT0`).

## When to fire / what set to match

1. Track keys while the chord is held.
2. Let `max_chord` be the **maximum simultaneous** down-set observed during the hold (largest `|pressed|` snapshot; if tied, use the latest such snapshot).
3. Fire **once**, except a matched row with `repeat=allow` (step 5). Each key becomes stable only after its raw level holds for the running debounce. The factory value is `kChordDebounceMs` in `include/settings.h`. The hotspot page stores another debounce, release guard, repeat delay, and repeat interval in NVS from 時間（進階）, and clearing that store returns these constants. The bounce window on the device is the running debounce plus `kChordBounceMs - kChordDebounceMs`. The practice page keeps the compiled factory timings. Two timings. GPIO17 chooses, live, and nothing is stored: open is `release`, closed to GND is `first`. The phone page and serial `fire` only report the level. The practice page does not move this pin.

   - **`release`:** fire on **full release**. Firmware confirms only after the debounced mask stays empty for the running release guard (factory `kChordReleaseGuardMs`), so a bounce that returns after the stable release does not split the chord or send it twice. A mask that returns before the running bounce window (factory `kChordBounceMs`) and is a subset or superset of the peak stays the same `max_chord`. Any later mask, including a nested one (あ `RT0` after か `LU+(RT0)`), sends the waiting chord on that poll and starts the next hold. A mask that is neither a subset nor a superset does that immediately.
   - **`first`:** after the peak, fire when the **first key that belonged to that peak goes up**. Remaining keys enter a drain: presses are ignored until the mask is empty again, then a new chord may start. This does not wait out the release guard. It assumes the keys of one chord go down together and then come up; a key that lifts while the chord is still landing sends early.
4. Skip if `max_chord` is empty or the matched row’s `output` is empty (empty outputs are not present in this CSV). A mask that matches no row is not sent.
5. If the matched row’s `repeat` is `allow`, the firmware can also tap while that peak is still held. The clock starts when the peak mask last changed, so a larger set (for example adding keys around a repeatable chord) starts over and does not repeat the smaller chord. After `kChordRepeatSettleMs` it sends the first tap. If that same peak is still held at the running repeat delay, it sends again and then every running repeat interval. Settle stays `kChordRepeatSettleMs` in `include/settings.h`. Delay and interval follow the hotspot override from step 3, whose factory values are `kChordRepeatDelayMs` and `kChordRepeatIntervalMs`. One poll sends at most one tap; a late poll does not catch up. Letting go after that first tap does not also send the usual end-of-chord copy. A hold shorter than the settle time never starts this and still fires once through `release` or `first`. A row with `repeat=no` uses only that send timing. A bounce that returns before the running bounce window and is a subset or superset of the peak keeps the same clock and does not send the first tap again. Switching to `first` while that guard is waiting still suppresses the extra copy when a tap already went out.

## Matching rule

Candidate rows satisfy:

```text
required ⊆ max_chord ⊆ required ∪ optional
```

where `required` / `optional` are the key sets from `keys_required` / `keys_optional`.

If several candidates match:

1. Prefer larger `|required|` (more specific).
2. Then prefer smaller `|optional|` (tighter envelope).
3. Then prefer smaller `id` (stable).

With the current table there are **no** duplicate `(keys_required, keys_optional)` pairs with different outputs, so step 1–2 already uniquely identify a row for any matching `max_chord`.

Equivalent view: among rows whose allowed envelope `required ∪ optional` covers `max_chord` and whose `required` keys were all down in that snapshot, pick the most specific required set.

## Host tests

Rule matching, the peak snapshot, both send timings, and recipe parsing live in [`../src/chord_core.cpp`](../src/chord_core.cpp). That file does not use Arduino. From the repo root, before flashing a change to the guard time, first-up behavior, or match order:

```text
pio test -e native
```

The chord test reads every row of [`../data/chord_map.machine.csv`](../data/chord_map.machine.csv) and rebuilds the expected match from that row. Recipe tokens are locked by [`../test/recipe_vectors.tsv`](../test/recipe_vectors.tsv): that test and `python tools/test_recipe_vectors.py` both read that file. `pio test -e native` runs the Python check first, then the chord tests and the config HTTP and job tests. The native build compiles `chord_core.cpp`, `chord_timing.cpp`, `config_http.cpp`, `config_job.cpp`, and `config_files.cpp`.

After a change to the guard, bounce, first-up, repeat, or match order, rebuild the practice module with the emsdk version in [`../wasm/emsdk-version.txt`](../wasm/emsdk-version.txt):

```text
python tools/build_chord_wasm.py
```

CI installs that same emsdk, rebuilds the wasm, and checks it matches [`../web/chord_core.wasm`](../web/chord_core.wasm). `node tools/test_chord_wasm.mjs` only checks the export wiring. Recipe parsing stays in `tools/recipe.py`, still locked by the shared vectors; the practice page does not send HID recipes.
