# ESP32-S3 JIS 和弦鍵盤（14 鍵）

實體離散按鍵 → ESP32 和弦引擎 → USB HID JIS 鍵盤 → PC 日文 IME。

硬體適配器：把 14 顆實體鍵解讀成和弦，在 MCU 上產出羅馬字／鍵位，PC 端只要開日文 IME。與同系列 Python 專案 `joystick writer` 分開。韌體依 14 鍵和弦表送出羅馬字與快捷鍵配方。

遠端倉庫（private）：https://github.com/nihilatus/esp32-jis-keyboard

---

## 這是什麼

| | |
| --- | --- |
| 角色 | USB HID **鍵盤**（不是手把／不是類比搖桿） |
| 板子 | ESP32-S3-DevKitC-1 **N16R8** |
| 輸入 | **14 顆**離散按鍵（無類比 stick） |
| 輸出 | 羅馬字／快捷鍵配方 → 主機日文 IME |
| 現況 | 14 鍵直連採樣、8 ms 消彈跳；預設全放 10 ms 後送出，可改成首鍵放開就送 |

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
3. 送出時機可切換，開機後沿用上次選擇（預設是全放）：
   - **全放再送**：全部放開，並再維持 10 ms 空檔，才比對與送出一次。下一組若在 mask 回到 0 之前就按下，會被併進同一組。
   - **首鍵放開就送**：高峰後第一顆屬於高峰的鍵放開就送出，然後進入收尾，等全部放開才開始新的和弦。手感前提是同一組先一起按下、再放開。
   手機熱點頁的兩個按鈕，或序列埠的 `fire release` / `fire first`，都會寫進 NVS。試打頁左上角同一個開關只影響瀏覽器。
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

`pio run` 與 `pio run -t upload` 會在編譯前自動重建機器表與韌體用的 [`include/chord_map.inc`](include/chord_map.inc)。人表有未知鍵名，或同一組鍵對到不同輸出時，建置會失敗。只想更新 CSV、還不編譯時：

```bat
cd /d "E:\cline play\esp32-jis-keyboard"
python tools/build_chord_map_machine.py
```

手機改表：複製 [`include/secrets.h.example`](include/secrets.h.example) 成 `include/secrets.h`，把熱點密碼改成至少 8 碼。`include/secrets.h` 在 `.gitignore` 裡，不要提交、也不要寫進文件。把 **GPIO15** 接到 GND（維持型開關合上）熱點才會出現；沒接或撥開就關，不用重開機，和弦與 USB 鍵盤照常。合上後手機瀏覽 `http://192.168.4.1/`。存檔寫進板子的 LittleFS，蓋過編譯進去的出廠表；網頁上的「還原出廠表」會刪掉那份存檔。

`data/mapping_table.csv`、`data/parameter_list.csv` 仍是舊 joystick-writer 移植殘件；新路徑以 `chord_map.machine.csv` 為準。  
舊的 [`docs/esp32-jis-chordboard-premapping.csv`](docs/esp32-jis-chordboard-premapping.csv)／[`premapping.csv`](premapping.csv) 若仍寫 32 ID，視為待清理。

---

## 用一般鍵盤試打

[`web/index.html`](web/index.html) 用電腦鍵盤模擬 14 顆和弦鍵，不必接開發板，也不送 USB HID。判讀規則與韌體相同（最大同時集合、全放 10 ms 或首鍵放開就送、`required`／`optional`、較具體的列優先）。電腦鍵盤的按下／放開已經是穩定邊緣，所以不做韌體那 8 ms GPIO 消彈跳。左上角「全放再送／首鍵放開就送」只存在這個瀏覽器，用來對照手感。

頁面用相對路徑讀 [`data/chord_map.machine.csv`](data/chord_map.machine.csv)，要從專案根目錄開一個本機伺服器，直接用檔案開啟讀不到表：

```bat
cd /d "E:\cline play\esp32-jis-keyboard"
python -m http.server 8765
```

瀏覽器開 [http://127.0.0.1:8765/web/](http://127.0.0.1:8765/web/)。改完人表並重建機器表之後，重新整理就會用新表。

| | |
| --- | --- |
| 預設對應 | 左 `A W D S Q E Z`＝`LL LU LR LD LT0 LT1 LT2`；右 `J I L K U O`＝`RL RU RR RD RT0 RT1`；空白鍵＝`RT2` |
| 改綁 | 點某個和弦鍵再按實體鍵。指定中再點一次會清掉。同一顆實體鍵不能綁兩個和弦鍵 |
| 語音 | 有假名就用日文念；方向鍵、Enter 這類沒有假名的列用中文念功能名。右上角可關 |
| 逐項驗證 | 依機器表一列一列要求打出該輸出。打對才算通過，可跳過或重來一輪 |
| 換表 | 「載入 CSV」暫時試另一份同格式的機器表；「改回機器表」回到專案裡那份 |
| 記住 | 按鍵對應與語音開關存在這個瀏覽器。也可下載／載入 `chord-keymap.json` |

---

## 硬體／燒錄

| | |
| --- | --- |
| 板子 | ESP32-S3-DevKitC-1 N16R8 |
| HID | 接標示 **USB** 的 OTG 口 |
| 燒錄／序列 | 接標示 **UART** 的口 |
| 保留 | **GPIO19 / GPIO20**（USB 資料線，勿當按鍵腳） |

**按鍵數量：14。** 每顆開關一腳接 GPIO、一腳接 GND（內部上拉，按下為 LOW）。另外一顆維持型開關接 **GPIO15** 與 GND，合上才開 Wi-Fi 設定熱點，撥開就關。腳位與零件見 [`docs/pinout.md`](docs/pinout.md)、[`docs/bom.md`](docs/bom.md)。不要再照舊的 6×6 矩陣或二極體清單接線。[`docs/config-plan.md`](docs/config-plan.md) 開頭已標成過時的 32 鍵文件。

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

| 已有 | 尚未 |
| --- | --- |
| PlatformIO + Arduino + TinyUSB HID 鍵盤 | VID/PID 仍是原型值 |
| 14 鍵直連、每鍵 8 ms 穩定消彈跳 | 自訂 JIS HID report／國碼 |
| 全放 10 ms 或首鍵放開就送，可切換並存進 NVS；手機熱點可改表並存進 LittleFS | 連發 |
| 配方送出（羅馬字、`Shift+1`、`Backspace`、`Ctrl+i` 等）；一般鍵盤試打頁 | 外殼／PCB |

出廠表仍由 `include/chord_map.inc` 編譯進去。改人表後，下一次 `pio run` 會重建機器表，燒錄才會更新出廠內容；手機上儲存的表會蓋過它，直到在網頁還原出廠。

### 目錄速覽

```
platformio.ini
src/main.cpp
src/buttons.cpp       14 鍵直連／每鍵 8 ms 消彈跳
src/hid_keyboard.cpp  配方 → USB HID
src/chords.cpp        max_chord；全放 10 ms 或首鍵放開就送；可改用 LittleFS 存檔
src/config_server.cpp Wi-Fi 熱點與手機設定頁（GPIO15 合上才開）
include/chord_map.inc 出廠表，由機器表產生，勿手改
include/secrets.h     本機熱點密碼，不進 git
include/*.h
eps32jischordboard-premapping.csv
data/chord_map.machine.csv
tools/build_chord_map_machine.py
tools/prebuild_chord_map.py  編譯前自動重建機器表
web/index.html                一般鍵盤試打，讀機器表
docs/chord-map-machine.md
docs/pinout.md
docs/bom.md
```
