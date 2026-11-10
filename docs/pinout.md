# ESP32-S3 JIS keyboard — diode key matrix pin map

Firmware role is **USB HID keyboard** (chord → romaji), not a gamepad.  
Physical input is a **6×6 diode matrix** (36 cells, 32 keys + 4 spare), not 1-GPIO-per-key.

## Board & USB

| Item | Choice |
| --- | --- |
| Board | ESP32-S3-DevKitC-1 **N16R8** |
| HID port | Connector labeled **USB** (native USB OTG) |
| Not for HID | **UART** port (USB-to-UART bridge) — flash / serial only |
| USB D− / D+ | **GPIO19 / GPIO20** — never assign as matrix lines |

## Why a matrix

32 logical buttons would exhaust safe GPIOs on N16R8 if wired 1:1 (USB, PSRAM, strapping, UART, RGB).  
A **6×6** matrix uses **12 GPIOs** and leaves spare cells for future keys.

## Safe GPIO set used

Prefer clean lines: **1–18, 21, 39–42, 47**.  
Avoided: **19/20** (USB), **35–37** (octal PSRAM), **0/45/46** (strapping), **38/48** (RGB), **43/44** (UART0).

| Role | GPIOs |
| --- | --- |
| **Rows** (driven) | **1, 2, 3, 4, 5, 6** |
| **Cols** (read) | **7, 8, 9, 10, 11, 12** |

## Scan method

1. Columns are `INPUT_PULLUP` (idle HIGH).
2. Each row is driven **LOW** in turn (`OUTPUT`); other rows stay Hi-Z (`INPUT`).
3. A closed switch pulls its column LOW through the diode → pressed.
4. Firmware debounce ≈ **8 ms**. Ghosting is prevented by per-key diodes (assumed installed).

## Diode orientation (required)

Per switch, series diode for anti-ghosting:

```
  COL (pull-up) ── switch ──|>|── ROW (driven LOW when scanned)
                              ↑
                         anode → cathode toward ROW
```

- **Anode** toward the **switch / column** side.  
- **Cathode** toward the **row** (driven) line.  
- Equivalent wording: *switch → diode anode, cathode toward row*.

If polarity is reversed, that key will never register (or the whole matrix misbehaves). Use **1N4148** (or SOD-123 equivalent).

## Matrix map (row, col) → logical button

Rows = GPIO index order above (R0=GPIO1 … R5=GPIO6).  
Cols = C0=GPIO7 … C5=GPIO12.

|  | C0 (7) | C1 (8) | C2 (9) | C3 (10) | C4 (11) | C5 (12) |
| --- | --- | --- | --- | --- | --- | --- |
| **R0 (1)** | Left N | Left NE | Left E | Left SE | Left S | Left SW |
| **R1 (2)** | Left W | Left NW | **Left Center** | Right N | Right NE | **Right E** |
| **R2 (3)** | Right SE | Right S | Right SW | Right W | Right NW | Right Center |
| **R3 (4)** | D-pad Up | D-pad Down | D-pad Left | D-pad Right | **A** | B |
| **R4 (5)** | X | Y | L1 | L2 | R1 | R2 |
| **R5 (6)** | **Start** | Select | *(spare)* | *(spare)* | *(spare)* | *(spare)* |

Bold cells are the **stub** keys enabled in firmware for first HID proof.

### Stub cell cheat-sheet

| Logical | Cell | Types (stub) |
| --- | --- | --- |
| Left Center | R1 C2 | `a` |
| Left N | R0 C0 | with Right E → `ka` |
| Right E | R1 C5 | chord partner |
| A | R3 C4 | `shi` |
| Start | R5 C0 | space |

## Wiring checklist (one key)

1. One side of momentary switch → **column** GPIO (via shared col bus).  
2. Other side of switch → **diode anode**.  
3. Diode **cathode** → **row** GPIO (via shared row bus).  
4. Repeat for all 32 keys; share row/col buses on protoboard / DuPont harness.

## Stub vs full matrix

`src/buttons.cpp` currently **samples only** Left Center, Left N, Right E, A, Start (`stub_enabled`).  
All 32 cells are mapped; clear / expand the stub mask as you solder more keys. Spare cells stay empty until needed.

## Later

Full chord engine will port logic / `mapping_table.csv` from sibling repo `E:\cline play\joystick writer`. This firmware types **romaji** HID keycodes; the PC only needs a Japanese IME.

Parts list: [`docs/bom.md`](bom.md).
