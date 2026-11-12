# Kanade

日文輸入用的 14 鍵和弦鍵盤，名字取自日文「奏」。

個人作品：人出主意，實作交給 AI。這份 README 優先讓後續 AI 快速對齊專案，而不是當產品手冊。

```text
實體 14 鍵 → ESP32 和弦引擎 → USB HID 或 BLE HID → 主機日文 IME（羅馬字）
```

| | |
| --- | --- |
| 板子 | ESP32-S3-DevKitC-1 **N16R8** |
| 角色 | HID **鍵盤**（不是手把、沒有類比 stick） |
| 輸出 | 開機二選一：GPIO16 開路 = USB，接地 = BLE |
| 送出時機 | GPIO21 開路 = 全放再送，接地 = 首鍵放開就送（運行中可切） |
| 主機 | 鍵盤類型設成 Kanade 的配列（目前只實作 **JIS**），再啟用日文 IME |
| 遠端 | https://github.com/nihilatus/Kanade |

已決定、不要順手改的取捨 → [`docs/design-decisions.md`](docs/design-decisions.md)  
想法還沒定案、不要照著開工 → [`docs/動嘴容易.md`](docs/動嘴容易.md)

---

## 架構（給 AI）

採樣、判斷、送鍵、改表分開。`chord_core` 是純邏輯：不碰 GPIO、檔案、HID，宿主測試與試打頁共用同一份。

```text
人編 CSV
  └─ 編譯前 → data/chord_map.machine.csv
              include/chord_map.inc   （出廠表，編進韌體）

開機
  LittleFS /chords.json 有效 → 活表；否則出廠表
  GPIO16 → USB 或 BLE（執行中切換會重啟）

loop（熱點關）
  14 GPIO → buttons（遮罩）→ chords / chord_core → hid_keyboard（HostLayout）→ hid_link
loop（熱點開）
  不掃描、不送鍵；只服務設定頁
```

遮罩 bit0..13 固定為 `LL LU LR LD LT0 LT1 LT2 RL RU RR RD RT0 RT1 RT2`。韌體依配方送出字元組合／功能鍵／修飾鍵複合，不做假名轉換。

| 該改什麼 | 看哪裡 |
| --- | --- |
| 和弦表（唯一可手改來源） | `esp32-jis-chordboard-premapping.csv` → `tools/build_chord_map_machine.py` |
| 判斷／高峰／送出時機／配方 | `src/chord_core.cpp`；常數在 `include/settings.h` |
| 活表、比對後送鍵 | `src/chords.cpp` |
| 主機配列（目前 JIS） | `src/host_layout.cpp` |
| 腳位與行為開關 | `include/pin_map.h`、`include/mode_switches.h` |
| USB／BLE 輸出 | `src/hid_link.cpp`、`src/hid_ble.cpp`、`src/output_switch.cpp` |
| 熱點與改表 API | `src/config_server.cpp` |
| 串起來 | `src/main.cpp` |

為什麼是「左手行、右手段、拇指修飾」→ [`docs/chorded-kana.md`](docs/chorded-kana.md)  
表欄位、RT0、比對規則 → [`docs/chord-map-machine.md`](docs/chord-map-machine.md)

---

## 建置

需 PlatformIO。改人表後，`pio run` 會經 `tools/prebuild_chord_map.py` 重建機器表與 `chord_map.inc`。

```bat
pio run
pio run -t upload
pio device monitor
```

燒錄走 **UART**；HID 用 **USB（OTG）**。上傳被 OTG 擋住時：按住 BOOT → 按 EN/RST → 放開 BOOT，再從 UART 重試。

只更新 CSV、不編譯：

```bat
python tools/build_chord_map_machine.py
```

宿主測試（不燒板）：`pio test -e native`  
實機大改後照 [`docs/device-acceptance.md`](docs/device-acceptance.md) 跑一輪。

一般鍵盤試打（不接板、不送 HID）：根目錄 `python -m http.server 8765`，開 <http://127.0.0.1:8765/web/>。判讀來自 `web/chord_core.wasm`（`src/chord_core.cpp`）；改判斷後重編：`python tools/build_chord_wasm.py`。

硬體腳位／零件／空腳 → [`docs/pinout.md`](docs/pinout.md)、[`docs/bom.md`](docs/bom.md)、[`docs/gpio-spare.md`](docs/gpio-spare.md)  
手機熱點改表 → 同見 pinout；`include/secrets.h` 不進 git。

---

## 文件索引

| 文件 | 用途 |
| --- | --- |
| [`docs/design-decisions.md`](docs/design-decisions.md) | 已決定、目前維持（改韌體前先讀） |
| [`docs/動嘴容易.md`](docs/動嘴容易.md) | 未定案想法；有空 `答：` 的不要開工 |
| [`docs/chord-map-machine.md`](docs/chord-map-machine.md) | 人和機器表、比對、送出 |
| [`docs/chorded-kana.md`](docs/chorded-kana.md) | 假名和弦設計骨架 |
| [`docs/playing.md`](docs/playing.md) | 指法與打字習慣 |
| [`docs/pinout.md`](docs/pinout.md) | 腳位、熱點、開關 |
| [`docs/device-acceptance.md`](docs/device-acceptance.md) | 實機驗收 |
| [`docs/history.md`](docs/history.md) | 演進紀錄 |

尚未：外殼／PCB。試作 VID/PID，有別人要用再申請（見設計取捨）。
