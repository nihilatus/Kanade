# ESP32-S3 JIS 和弦鍵盤（14 鍵）

實體離散按鍵 → ESP32 和弦引擎 → USB HID JIS 鍵盤 → PC 日文 IME。

硬體適配器：把 14 顆實體鍵解讀成和弦，在 MCU 上產出羅馬字／鍵位。主機必須把鍵盤類型設成日文 106/109，再啟用日文 IME（羅馬字）。與同系列 Python 專案 `joystick writer` 分開。韌體依 14 鍵和弦表送出羅馬字與快捷鍵配方。

遠端倉庫（private）：https://github.com/nihilatus/esp32-jis-keyboard

---

## 這是什麼

| | |
| --- | --- |
| 角色 | USB HID **鍵盤**（不是手把／不是類比搖桿） |
| 板子 | ESP32-S3-DevKitC-1 **N16R8** |
| 輸入 | **14 顆**離散按鍵（無類比 stick） |
| 輸出 | 羅馬字／快捷鍵配方。主機鍵盤類型必須是日文 106/109，再交日文 IME |
| 現況 | 14 鍵直連採樣、`kChordDebounceMs` 消彈跳；[送出時機](#輸入邏輯) |

---

## 架構

採樣、和弦判斷、送鍵、改表分開。判斷本身在 `chord_core`，不碰 GPIO、檔案或 USB，所以宿主測試不用燒板。

```text
人編 CSV
  └─ 編譯前 → data/chord_map.machine.csv
              include/chord_map.inc   出廠表，編進韌體

開機 chords_begin
  LittleFS /chords.json 有效 → 用這份活表
  否則用出廠表

每一圈 loop
  14 顆 GPIO
    → buttons        去彈跳，得到 14-bit 遮罩
    → chords         交給 chord_core：高峰、送出時機、比對活表
    → hid_keyboard   配方排隊，到點才送一個 USB HID report
  GPIO15
    → config_server  合上才開熱點；只在熱點開著時處理網頁與 JSON
```

遮罩的 bit0..13 固定是 `LL LU LR LD LT0 LT1 LT2 RL RU RR RD RT0 RT1 RT2`。PC 收到的是一般鍵盤的羅馬字與少數特殊鍵，日文轉換交給 IME，韌體不做假名轉換。主機鍵盤類型必須設成日文 106/109。字元走 JIS 鍵位：`a`–`z` 與數字的用途碼和美式相同，出廠表裡不同的是 `[`（0x30）和 `]`（0x31）。`Shift+8`／`Shift+9` 在這個版面上才是（）。`Hankaku`、`Muhenkan`、`Henkan`、`Kana` 也要這個版面，Windows 才會當成半角/全角、無変換、変換、ひらがな。USB 國碼不設定，Windows 不會靠它切換版面。

| 模組 | 做什麼 |
| --- | --- |
| `src/buttons.cpp` | 一鍵一 GPIO，按下為 LOW。去彈跳後的遮罩給和弦引擎；raw 邊緣與穩定遮罩同時寫進 512 筆 RAM 環形緩衝 |
| `src/chord_core.cpp` | 鍵名、高峰快照、全放／首鍵放開、規則比對、配方解析 |
| `src/chords.cpp` | 出廠表與 LittleFS 活表、NVS 裡的送出模式。比對成功才呼叫送鍵；比對不到時序列埠印「無符合規則」與遮罩，不送鍵 |
| `src/hid_keyboard.cpp` | 把配方排進同一條時間軸。`loop` 不會為了打完一串字而停住 |
| `src/config_server.cpp` | GPIO15、SoftAP、和弦 JSON API 與手機頁。熱點關掉時鍵盤照常送鍵 |
| `src/main.cpp` | `setup`／`loop` 把上面串起來。序列埠指令是 `fire`（送出時機）、`keys`（印目前按鍵）、`drops`（開機後丟掉的配方、略過的 token、主機未就緒次數；UART0 也印）與 `trace`（傾印開機後最近 512 筆 raw 邊緣與穩定遮罩；傾印期間 `loop` 暫停） |
| `web/index.html` | 用電腦鍵盤試打。讀機器表，不燒錄、不送 USB HID |
| `test/test_chord_core` | `pio test -e native`。只編譯 `chord_core.cpp` |

手機存檔會蓋過出廠表，直到網頁上還原出廠。送出時機寫在 NVS，與和弦表分開。試打頁的開關只存在那個瀏覽器。

---

## 按鍵

14 個實體鍵 ID（左／右各 7）：

| 側 | 鍵 |
| --- | --- |
| 左 | `LL` `LU` `LR` `LD` `LT0` `LT1` `LT2` |
| 右 | `RL` `RU` `RR` `RD` `RT0` `RT1` `RT2` |

沒有類比 stick，也不要照早期 32 鍵矩陣接線。

---

## 輸入邏輯

按住期間取同時按下數量最大的一組，送出一次。時機是全放再送，或首鍵放開就送。`(RT0)`、比對與候選優先序見 [`docs/chord-map-machine.md`](docs/chord-map-machine.md)。

沒有 NVS 紀錄時，開機用 `CHORD_FIRE_DEFAULT_FIRST`（沒設就是全放）。手機熱點頁或序列埠 `fire release` / `fire first` 會寫進 NVS，並蓋過編譯旗標。試打頁的開關只影響瀏覽器。每鍵要先穩定 `kChordDebounceMs` 才算按下或放開。

機器表 `repeat=allow` 的列（目前是方向鍵和 `Backspace`）由韌體自己定時送出按下再放開，間隔 `kChordRepeatIntervalMs`，不跟 Windows 的鍵盤重複速度走。

---

## 設定檔

| 檔案 | 用途 |
| --- | --- |
| [`esp32-jis-chordboard-premapping.csv`](esp32-jis-chordboard-premapping.csv) | **人編**和弦表，唯一可手改的來源 |
| [`data/chord_map.machine.csv`](data/chord_map.machine.csv) | **機器**表，由下列腳本產生；韌體與試打頁讀這份 |
| [`docs/chord-map-machine.md`](docs/chord-map-machine.md) | 欄位、RT0、觸發與比對 |
| [`tools/build_chord_map_machine.py`](tools/build_chord_map_machine.py) | 人表 → 機器表與 [`include/chord_map.inc`](include/chord_map.inc) |
| [`tools/prebuild_chord_map.py`](tools/prebuild_chord_map.py) | PlatformIO 編譯前掛鉤，只呼叫上一支 |

`build_chord_map_machine.py` 做轉換；`prebuild_chord_map.py` 不做轉換，只在 `pio run` 編譯前轉呼叫它。`pio run` 與 `pio run -t upload` 會因此重建機器表與韌體用的 include。人表有未知鍵名、同一組鍵對到不同輸出，或輸出裡有裝置會略過的配方 token 時，建置會失敗。只想更新 CSV、還不編譯時，在專案根目錄執行：

```bat
python tools/build_chord_map_machine.py
```

手機改表、熱點密碼與 GPIO15 開關見 [`docs/pinout.md`](docs/pinout.md)。`include/secrets.h` 不進 git。

---

## 用一般鍵盤試打

[`web/index.html`](web/index.html) 用電腦鍵盤模擬 14 顆和弦鍵，不必接開發板，也不送 USB HID。判讀規則與韌體相同（最大同時集合、全放 `kChordReleaseGuardMs` 或首鍵放開就送、`required`／`optional`、較具體的列優先）。電腦鍵盤的按下／放開已經是穩定邊緣，所以不做韌體的 `kChordDebounceMs` GPIO 消彈跳。頁首「全放再送／首鍵放開就送」只存在這個瀏覽器，用來對照手感。

頁面用相對路徑讀 [`data/chord_map.machine.csv`](data/chord_map.machine.csv)。在專案根目錄開一個本機伺服器，直接用檔案開啟讀不到表：

```bat
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

**按鍵數量：14。** 每顆開關一腳接 GPIO、一腳接 GND（內部上拉，按下為 LOW）。另外一顆維持型開關接 **GPIO15** 與 GND，合上才開 Wi-Fi 設定熱點。腳位與零件見 [`docs/pinout.md`](docs/pinout.md)、[`docs/bom.md`](docs/bom.md)。

### 建置與燒錄（Windows）

需 [PlatformIO Core](https://platformio.org/install/cli) 或 VS Code／Cursor 的 PlatformIO 擴充。在專案根目錄執行：

```bat
pio run
pio run -t upload
pio device monitor
```

燒錄走 **UART**。燒完把資料線改接 **USB（OTG）**，讓 Windows 列舉 HID 鍵盤。主機必須把鍵盤類型設成日文 106/109，再開記事本、啟用日文 IME（羅馬字），然後測按鍵。鍵盤類型仍是美式時，`[`、`]` 和 `Shift+8`／`Shift+9` 會對到美式符號，`Hankaku`、`Muhenkan`、`Henkan`、`Kana` 也不會被當成半角/全角、無変換、変換、ひらがな。沒反應時先看這項設定。`Eisuu`（0x91，LANG2）是 macOS 的英数，Windows 常常沒有反應。

若 OTG HID 佔住埠導致上傳失敗：按住 **BOOT** → 點 **EN/RST** → 放開 BOOT，再於 UART 重試。

`platformio.ini` 重點：`ARDUINO_USB_MODE=0`（OTG TinyUSB HID）；`ARDUINO_USB_CDC_ON_BOOT=1` 可選（序列日誌；有干擾可改 `0`）。

---

## 現況與待辦

| 已有 | 尚未 |
| --- | --- |
| PlatformIO + Arduino + TinyUSB HID 鍵盤。描述符用途 0–255，已含 変換（0x8A）、無変換（0x8B）、カタカナひらがな（0x88）；配方裡的 `Henkan` 等走 `pressRaw` | VID/PID 仍是原型值 |
| 14 鍵直連、每鍵 `kChordDebounceMs` 穩定消彈跳。機器表 `repeat=allow` 的列在高峰穩定 50ms 後送第一下，同一組按住到 400ms 後每 33ms 連發 | — |
| [送出時機](#輸入邏輯)；手機熱點可改表並存進 LittleFS | 外殼／PCB |
| 配方送出（羅馬字、`Shift+1`、`Backspace`、`Ctrl+i` 等），ASCII 走 JIS 配列（`KeyboardLayout_ja_JP`）。USB 國碼不設定；一般鍵盤試打頁 | — |

出廠表仍由 `include/chord_map.inc` 編譯進去。改人表後，下一次 `pio run` 會重建機器表，燒錄才會更新出廠內容；手機上儲存的表會蓋過它，直到在網頁還原出廠。

### 目錄速覽

```
platformio.ini
src/main.cpp              把採樣、和弦、HID、熱點串成 loop
src/buttons.cpp           14 鍵直連與消彈跳
src/chord_core.cpp        純邏輯，宿主測試也編譯這份
src/chords.cpp            活表、送出模式、比對後送鍵
src/hid_keyboard.cpp      配方 → USB HID
src/config_server.cpp     GPIO15 熱點與手機設定頁
include/chord_map.inc     出廠表，由機器表產生，勿手改
include/secrets.h         本機熱點密碼，不進 git
esp32-jis-chordboard-premapping.csv
data/chord_map.machine.csv
tools/build_chord_map_machine.py
tools/recipe.py           配方解析，與 chord_core 同一套規則
tools/prebuild_chord_map.py
tools/test_recipe_vectors.py
test/recipe_vectors.tsv   C++ 與 Python 共用的配方向量
test/test_chord_core      pio test -e native
web/index.html            一般鍵盤試打，讀機器表
docs/chord-map-machine.md
docs/pinout.md
docs/bom.md
```
