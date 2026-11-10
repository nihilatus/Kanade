# ESP32-S3 JIS keyboard — pin map

Adapted from Project store `docs/esp32-hid-pinout.md` (gamepad design → keyboard chord surface). Same GPIOs; firmware role is **USB HID keyboard**, not gamepad.

## Board & USB

| Item | Choice |
| --- | --- |
| Board | ESP32-S3-DevKitC-1 **N16R8** |
| HID port | Connector labeled **USB** (native USB OTG) |
| Not for HID | **UART** port (USB-to-UART bridge) — flash / serial only |
| USB D− / D+ | **GPIO19 / GPIO20** — never assign as buttons |

## Caveats (same as store design)

- **GPIO35 / 36 / 37** — octal PSRAM on N16R8. Map uses **GPIO35 only for Right Center**; leave 36/37 alone. Bench-test R3; fall back to a matrix/expander if flaky.
- **Strapping:** GPIO0 (BOOT), GPIO45, GPIO46 — don’t hold at reset; enable pull-up on 45/46 **after** boot.
- **RGB LED:** v1.1 ≈ GPIO38; v1.0 ≈ GPIO48 — pressing that key glitches the LED.
- **UART0:** GPIO43/44 — wiring them as buttons drops the UART console.

## GPIO → logical button (32× direct)

| # | Logical | GPIO | Notes |
| ---: | --- | ---: | --- |
| 1 | D-pad Up | 1 | |
| 2 | D-pad Down | 2 | |
| 3 | D-pad Left | 42 | |
| 4 | D-pad Right | 41 | |
| 5 | A | 40 | stub: → `shi` |
| 6 | B | 39 | |
| 7 | X | 47 | |
| 8 | Y | 21 | |
| 9 | L1 | 4 | |
| 10 | R1 | 5 | |
| 11 | L2 | 6 | |
| 12 | R2 | 7 | |
| 13 | Start | 15 | stub: → space |
| 14 | Select | 16 | |
| 15 | Left N | 17 | stub chord with Right E → `ka` |
| 16 | Left NE | 18 | |
| 17 | Left E | 8 | |
| 18 | Left SE | 3 | |
| 19 | Left S | 9 | |
| 20 | Left SW | 10 | |
| 21 | Left W | 11 | |
| 22 | Left NW | 12 | |
| 23 | Left Center | 13 | stub: → `a` |
| 24 | Right N | 14 | |
| 25 | Right NE | 48 | RGB on v1.0 |
| 26 | Right E | 38 | RGB on v1.1; stub chord |
| 27 | Right SE | 45 | strapping |
| 28 | Right S | 0 | BOOT |
| 29 | Right SW | 46 | strapping |
| 30 | Right W | 43 | UART0 TX |
| 31 | Right NW | 44 | UART0 RX |
| 32 | Right Center | **35** | PSRAM line — caveat |

**Do not use:** 19, 20 (USB); 36, 37 (PSRAM).

Electrical: switch to GND, internal pull-up, pressed = LOW, debounce ~8 ms (firmware).

## Stub vs full wiring

`src/buttons.cpp` currently **samples only** Left Center, Left N, Right E, A, Start. All other rows keep their GPIO numbers in the table but are skipped until you clear the stub mask in `stub_enabled()`.

## Later

Full chord engine will port logic / `mapping_table.csv` from sibling repo `E:\cline play\joystick writer`. This firmware types **romaji** HID keycodes; the PC only needs a Japanese IME.
