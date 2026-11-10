# Machine chord map

Source of truth (human / editable): [`../eps32jischordboard-premapping.csv`](../eps32jischordboard-premapping.csv)

Generated machine table: [`../data/chord_map.machine.csv`](../data/chord_map.machine.csv)

`pio run` rebuilds this table before compile. To regenerate the CSV without compiling: `python tools/build_chord_map_machine.py`

That command rewrites the machine CSV and [`../include/chord_map.inc`](../include/chord_map.inc). Unknown key tokens, or the same key set mapped to different outputs, make the script exit non-zero and fail the PlatformIO build. Firmware uses the generated include as the factory table. A chord table saved from the phone is stored in LittleFS as `chords.json` and overrides the factory table until the page’s reset action deletes it.

The browser practice page [`../web/index.html`](../web/index.html) fetches this same CSV (serve the repo root; see the README). “載入 CSV” can try another file with these columns; “改回機器表” loads this one again.

## Columns

| Column | Meaning |
| --- | --- |
| `id` | Stable 1-based row id in generation order |
| `kana` | Display kana / symbol (may be empty for pure key ops) |
| `output` | Romaji / key recipe sent on match (`a`, `nn`, `Shift+1`, `Backspace`, `Ctrl+i`, …) |
| `keys_required` | Sorted unique **required** keys, `+`-joined. Does **not** include optional RT0 |
| `keys_optional` | Sorted unique optional keys (`RT0` or empty) |
| `category` | Short tag from human 摘要 (section label); may be empty |

Empty 假名 **and** empty 鍵位模擬 rows in the human CSV are spacing / reserved slots — they are **omitted** here (no no-op rows).

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
3. Fire **once**. Each key becomes stable only after its raw level holds for **8 ms**. Two timings, switchable at runtime (phone page or `fire release` / `fire first` on serial; stored in NVS). Default is `release`.

   - **`release`:** fire on **full release**. Firmware confirms only after the debounced mask stays empty for **10 ms**, so a bounce that returns after the stable release does not split the chord or send it twice. A new chord pressed before that empty window is merged into the same `max_chord`.
   - **`first`:** after the peak, fire when the **first key that belonged to that peak goes up**. Remaining keys enter a drain: presses are ignored until the mask is empty again, then a new chord may start. This does not wait out the 10 ms guard. It assumes the keys of one chord go down together and then come up; a key that lifts while the chord is still landing sends early.
4. Skip if `max_chord` is empty or the matched row’s `output` is empty (empty outputs are not present in this CSV). A mask that matches no row is not sent.

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
