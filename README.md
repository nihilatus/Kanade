# ESP32-S3 JIS Chord Keyboard / ESP32-S3 JIS 和弦鍵盤

Minimal **native USB HID keyboard** scaffold for ESP32-S3-DevKitC-1 **N16R8**.  
Device looks like a USB keyboard (not a gamepad). Chord → romaji runs on the MCU; the PC only needs a Japanese IME.

最小原生 USB HID **鍵盤**骨架（非手把）。和弦→羅馬字在 ESP32 上完成；PC 端只要日文 IME。

> Separate from the sibling Python app `joystick writer`. Full chord engine will later port timing + `mapping_table.csv` from that repo — this tree does **not** copy the whole app.

---

## Board / USB

| | |
| --- | --- |
| Board | ESP32-S3-DevKitC-1 N16R8 |
| HID cable | Plug PC into the port labeled **USB** (OTG) |
| Flash / serial | Port labeled **UART** (optional CDC also on OTG if enabled) |
| Reserved | **GPIO19 / GPIO20** — USB data; do not wire buttons |

Pin table: [`docs/pinout.md`](docs/pinout.md)

---

## What’s in this scaffold / 目前有什麼

| Done | Stub / later |
| --- | --- |
| PlatformIO + Arduino + TinyUSB HID keyboard | Full 32-key sampling (only 5 GPIOs enabled) |
| Debounced GPIO read for stub keys | Full chord engine + SOCD timing |
| Demo chords → romaji HID (`a` / `ka` / `shi` / space) | Port `mapping_table.csv` from joystick-writer |
| Pin map docs | JIS HID country/layout descriptor polish |

### Stub chord proof

| Combo | Types |
| --- | --- |
| Left Center (GPIO13) release | `a` |
| Left N (17) held + Right E (38) release | `ka` |
| A (40) release | `shi` |
| Start (15) release | space |

Wire those switches to GND (active-low). Expand `stub_enabled()` in `src/buttons.cpp` as you solder more keys.

---

## Build & flash (Windows / MYROOM)

Needs [PlatformIO Core](https://platformio.org/install/cli) or the VS Code / Cursor PlatformIO extension.

```bat
cd /d "E:\cline play\esp32-jis-keyboard"
pio run
```

Flash via the **UART** port (or esp32s3 USB-Serial/JTAG if your cable is on that path during upload):

```bat
pio run -t upload
pio device monitor
```

After reset, move the data cable to the **USB (OTG)** port so Windows enumerates the HID keyboard. Open Notepad, enable Japanese IME (romaji input), press stub keys.

If upload fails while OTG HID holds the port, hold **BOOT**, tap **EN/RST**, release BOOT, then retry upload on UART.

### `platformio.ini` notes

- `ARDUINO_USB_MODE=0` — USB OTG TinyUSB stack (required for HID on the USB connector).
- `ARDUINO_USB_CDC_ON_BOOT=1` — optional serial over OTG for logs; set to `0` if it confuses the host.
- N16R8 flash/PSRAM flags are set; GPIO35 as Right Center still needs a bench check.

---

## Layout

```
platformio.ini
src/main.cpp          entry
src/buttons.cpp       GPIO map + stub mask + debounce
src/hid_keyboard.cpp  USB HID keyboard sender
src/chords.cpp        tiny chord → romaji table
include/*.h
docs/pinout.md
```

---

## Remaining gaps / 之後要做

1. Enable all 32 inputs (or matrix fallback if GPIO35 fails).
2. Port chord engine + CSV from `E:\cline play\joystick writer`.
3. Decide final USB VID/PID / product strings; optional true JIS layout report.
4. CDC vs UART logging strategy once 43/44 are buttons.
5. Hardware harness / PCB — out of scope for this scaffold.

---

## Git

Local repo only unless you add a GitHub remote yourself. No remote is created by this scaffold.
