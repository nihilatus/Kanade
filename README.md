# ESP32-S3 JIS 和弦鍵盤（14 鍵）

實體離散按鍵 → ESP32 和弦引擎 → USB HID JIS 鍵盤 → PC 日文 IME。

硬體適配器：把 14 顆實體鍵解讀成和弦，在 MCU 上產出羅馬字／鍵位，PC 端只要開日文 IME。與同系列 Python 專案 `joystick writer` 分開；完整和弦引擎尚未移植，目前韌體仍是 stub 證明。

遠端倉庫（private）：https://github.com/nihilatus/esp32-jis-keyboard

---

## 這是什麼

| | |
| --- | --- |
| 角色 | USB HID **鍵盤**（不是手把／不是類比搖桿） |
| 板子 | ESP32-S3-DevKitC-1 **N16R8** |
| 輸入 | **14 顆**離散按鍵（無類比 stick） |
| 輸出 | 羅馬字／快捷鍵配方 → 主機日文 IME |
| 現況 | PlatformIO + TinyUSB HID 骨架；和弦表已有 machine CSV；執行時引擎仍是 demo stub |

---

## 按鍵

14 個實體鍵 ID（左／右各 7）：

| 側 | 鍵 |
| --- | --- |
| 左 | `LL` `LU` `LR` `LD` `LT0` `LT1` `LT2` |
| 右 | `RL` `RU` `RR` `RD` `RT0` `RT1` `RT2` |

沒有類比 stick；舊文件若仍寫「32 鍵／手把」屬過時敘述，請以本 README 的 14 鍵為準。

---

## 輸入邏輯

1. 按住期間持續追蹤按下集合。
2. `max_chord` = 這次按住期間**同時按下數量最大**的那一組（同大小取較晚的快照）。
3. **全部放開**時才觸發一次比對與送出。
4. 比對規則（詳見 [`docs/chord-map-machine.md`](docs/chord-map-machine.md)）：

   ```text
   required ⊆ max_chord ⊆ required ∪ optional
   ```

5. 人表中的 `(RT0)` 表示 RT0 **可有可無**（寫入 machine 表的 `keys_optional`）；空的人表列不匯出。
6. 多筆候選時：優先 `|required|` 較大 → 再 `|optional|` 較小 → 再較小的 `id`。

---

## 設定檔

| 檔案 | 用途 |
| --- | --- |
| [`eps32jischordboard-premapping.csv`](eps32jischordboard-premapping.csv) | **人編**版面／和弦（來源真相） |
| [`data/chord_map.machine.csv`](data/chord_map.machine.csv) | **機器**表（韌體／工具讀取） |
| [`docs/chord-map-machine.md`](docs/chord-map-machine.md) | 欄位、RT0、觸發／比對規則 |
| [`tools/build_chord_map_machine.py`](tools/build_chord_map_machine.py) | 人表 → 機器表重建腳本 |

重建機器表：

```bat
cd /d "E:\cline play\esp32-jis-keyboard"
python tools/build_chord_map_machine.py
```

`data/mapping_table.csv`、`data/parameter_list.csv` 仍是舊 joystick-writer 移植殘件；新路徑以 `chord_map.machine.csv` 為準。  
舊的 [`docs/esp32-jis-chordboard-premapping.csv`](docs/esp32-jis-chordboard-premapping.csv)／[`premapping.csv`](premapping.csv) 若仍寫 32 ID，視為待清理。

---

## 硬體／燒錄

| | |
| --- | --- |
| 板子 | ESP32-S3-DevKitC-1 N16R8 |
| HID | 接標示 **USB** 的 OTG 口 |
| 燒錄／序列 | 接標示 **UART** 的口 |
| 保留 | **GPIO19 / GPIO20**（USB 資料線，勿當按鍵腳） |

**按鍵數量：14。**  
[`docs/pinout.md`](docs/pinout.md)、[`docs/bom.md`](docs/bom.md) 等仍可能殘留早期 **6×6／32 鍵**矩陣敘述——硬體接線文件需要後續專趟更新，本 README 不重寫那些細節。實體配線以 14 鍵為準。

### 建置與燒錄（Windows）

需 [PlatformIO Core](https://platformio.org/install/cli) 或 VS Code／Cursor 的 PlatformIO 擴充。

```bat
cd /d "E:\cline play\esp32-jis-keyboard"
pio run
pio run -t upload
pio device monitor
```

燒錄走 **UART**。燒完把資料線改接 **USB（OTG）**，讓 Windows 列舉 HID 鍵盤；開記事本、啟用日文 IME（羅馬字），再測按鍵。

若 OTG HID 佔住埠導致上傳失敗：按住 **BOOT** → 點 **EN/RST** → 放開 BOOT，再於 UART 重試。

`platformio.ini` 重點：`ARDUINO_USB_MODE=0`（OTG TinyUSB HID）；`ARDUINO_USB_CDC_ON_BOOT=1` 可選（序列日誌；有干擾可改 `0`）。

---

## 現況與待辦

| 已有 | 尚未／stub |
| --- | --- |
| PlatformIO + Arduino + TinyUSB HID 鍵盤 | 完整 14 鍵採樣與接線對應（韌體仍用舊 stub 細胞／命名） |
| 人表 + machine CSV + 重建腳本 | 依 machine 表實作「全放觸發／max_chord」引擎 |
| 板子／雙 USB 口說明 | 更新 pinout／BOM／矩陣文件至 14 鍵 |
| Demo 和弦 → 羅馬字 HID（少數 stub） | VID/PID、產品字串、真正 JIS report 微調 |
| | 外殼／PCB（非本 scaffold 範圍） |

**不要假設完整和弦引擎已上板**——`src/chords.cpp` 仍是少數 combo 的 demo。

### 目錄速覽

```
platformio.ini
src/main.cpp
src/buttons.cpp       掃描／debounce（文件與 stub 命名可能仍偏舊矩陣）
src/hid_keyboard.cpp  USB HID 送鍵
src/chords.cpp        stub 和弦 → 羅馬字
include/*.h
eps32jischordboard-premapping.csv
data/chord_map.machine.csv
tools/build_chord_map_machine.py
docs/chord-map-machine.md
docs/pinout.md        ※ 可能仍寫 32 鍵，待更新
docs/bom.md           ※ 同上
```
