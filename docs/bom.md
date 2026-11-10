# BOM — diode key matrix prototype

Materials Augustine must buy or prepare for the ESP32-S3 JIS chord keyboard matrix.  
Quantities include a small spare margin.

## Already on hand

| Item | Qty | Notes |
| --- | ---: | --- |
| ESP32-S3-DevKitC-1 **N16R8** | 1 | Already have — do not buy another for this build |
| USB cable for **USB (OTG)** port | 1 | Data-capable; HID enumerates on the OTG connector, not UART |

## Must buy / prepare

| Item | Qty | Suggest | Notes |
| --- | ---: | --- | --- |
| Momentary switches | **32** (+4 spare ≈ **36**) | **6×6 mm tactile** through-hole (e.g. TS-1187 / common soft-touch) | One per logical button; 6×6 mm fits breadboard / proto well |
| Signal diodes | **32** (+4 spare ≈ **36**) | **1N4148** (DO-35) or SOD-123 1N4148W | Anti-ghosting; cathode toward **row** (see pinout) |
| Hook-up wire | 1 spool or mixed | 22–28 AWG solid for proto, or DuPont jumpers | Row/col buses + switch legs |
| DuPont / jumper wires | ~40–60 | M-M and/or M-F | DevKit header → protoboard |
| Protoboard **or** breadboard | 1–2 | Solder proto (preferred once layout stable) **or** half/full breadboard for dry-run | 6×6 matrix needs space for 32 switches + diodes |

## Optional but useful

| Item | Qty | Notes |
| --- | ---: | --- |
| Male/female header pins | 1–2 strips | Break out rows/cols; strain relief to DevKit |
| USB cable for **UART** port | 1 | Flash / serial monitor while OTG is used for HID (or one cable swapped between ports) |
| Extra 1N4148 / tactiles | ~10 | Replacements while soldering |
| Multimeter | 1 | Continuity + diode-orientation checks |
| Hot glue / cable ties | — | Keep matrix harness from yanking GPIO headers |

## Not required for matrix bring-up

- Extra ESP32 boards  
- PSRAM / flash modules  
- Level shifters (matrix is 3.3 V logic, internal pull-ups)  
- I²C GPIO expander (matrix frees enough pins)

## Approximate buy list (shopping shorthand)

```
36×  6x6mm tactile momentary switches
36×  1N4148 signal diodes
1×   solder protoboard (or breadboard for first dry-run)
1×   DuPont jumper pack (M-M / M-F)
1×   hook-up wire (if not using only DuPont)
—    headers / glue / spare USB-UART cable as needed
```

Board + OTG USB cable: **already owned**.

Pin / cell map: [`docs/pinout.md`](pinout.md).
