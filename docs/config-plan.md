# ESP32 設定檔配置計畫（v1）

本文件說明前置輸入代號、矩陣座標與 `mapping_table.csv` 的關係，以及哪些內容應編譯進韌體、哪些可放可寫檔案系統。

## 編號方案（前置輸入代號）

- **格式**：十進位、**兩位零填充**字串（`00`–`49`）。韌體內可 `atoi` 成 `uint8_t`。
- **原則**：32 顆實體鍵 → **32 個互不重複**代號。和弦邏輯仍用 `mapping_table.csv` 的中文手勢名，不直接寫死這些數字。
- **Bank（沿用舊表、只修正碰撞）**：

| Bank | 代號 | 用途 |
| --- | --- | --- |
| `0x` | `00` | Select |
| `0x` | `01`–`09` | 左蘑菇頭 8 向 + 置中 |
| `1x` | `10` | Start |
| `1x` | `11`–`19` | 右蘑菇頭 8 向 + 置中 |
| `3x` | `32`/`34`/`36`/`38` | 十字鍵 下/左/右/上 |
| `3x` | `31` / `39` | **L1**（新編號）/ L2 |
| `4x` | `42`/`44`/`46`/`48` | A / X / B / Y |
| `4x` | `41` / `49` | **R1**（新編號）/ R2 |

### 相對舊 premapping 的變更

| 鍵 | 舊代號 | 新代號 | 原因 |
| --- | --- | --- | --- |
| 左蘑菇頭 置中 | `09` | `09`（不變） | 保留 |
| **L1** | `09`（與置中撞名） | **`31`** | 必須唯一 |
| 右蘑菇頭 置中 | `19` | `19`（不變） | 保留 |
| **R1** | `19`（與置中撞名） | **`41`** | 必須唯一 |

其餘 28 鍵代號維持舊值，方便對照既有筆記。權威表：[`esp32-jis-chordboard-premapping.csv`](esp32-jis-chordboard-premapping.csv)（根目錄 [`../premapping.csv`](../premapping.csv) 為同內容副本）。

## 三層對應關係

```
實體矩陣 (row,col)  ──scan──►  ButtonId / 前置輸入代號
                                      │
手把原先操作字串 ◄── premapping.csv ──┘
        │
        ▼
mapping_table.csv 欄位：左手大拇指 / 右手大拇指 / L1 / R1 / L2 / R2 / 觸發時機
        │
        ▼
和弦引擎 → 羅馬字鍵序 → USB HID
```

1. **矩陣 → 邏輯鍵**：`docs/pinout.md` 與 `src/buttons.cpp` 的 `kMatrixMap`。premapping 的 `matrix_row` / `matrix_col` 必須與此一致。
2. **邏輯鍵 → 前置代號**：`premapping.csv`（本專案）。除錯／序列輸出可用代號；引擎匹配仍用中文手勢名。
3. **手勢名 → 輸出**：[`data/mapping_table.csv`](../data/mapping_table.csv)（自 sibling `joystick writer` 複製）。欄位語意與 Python `config_loader.MappingRule` 相同：左／右拇指方向條件、L1–R2 布林條件、觸發時機（放開時／立即＋連發等）。

**重點**：L1／R1 在 mapping 裡是**修飾條件**（「L1 按下」欄非空），與左／右置中（拇指「置中 按下」）是不同實體鍵；premapping 已分開編號，韌體不可再把 Center 與 L1／R1 合成同一 bit。

## 建議檔名與職責

| 路徑 | 職責 | v1 建議存放 |
| --- | --- | --- |
| `docs/esp32-jis-chordboard-premapping.csv` / `premapping.csv` | 32 鍵唯一 ID + 矩陣座標 + 與 mapping 對齊的手勢名 | **編譯期**：轉成 `include/premapping.h`（或 PROGMEM 表）。量產前很少改接線。 |
| `data/mapping_table.csv` | 和弦／符號／操作輸出表（與 Python 同 schema） | **v1：編譯期嵌入**（生成 `mapping_table.inc` / 靜態陣列）。之後可改 LittleFS 以便現場改鍵。 |
| `data/parameter_list.csv` | 門檻、組合鍵視窗、連發、送鍵間隔等 | **v1：compile-time 預設**（`include/parameters.h` 常數），與 CSV 預設值對齊；可選 LittleFS 覆寫。 |
| （未來）`data/user_overrides.json` | 使用者微調參數／少數重映射 | LittleFS；v1 可不做 |

### Flash / SPIFFS / LittleFS vs 標頭

| 內容 | v1（建議） | 之後 |
| --- | --- | --- |
| GPIO／矩陣座標 | `pinout` + `buttons.cpp` 編譯期 | 不變（硬體固定） |
| premapping ID | 編譯期 header（由 CSV 生成或手維護平行表） | 仍編譯期為主 |
| mapping_table | **嵌入韌體**（N16R8 閃存夠用） | LittleFS 可熱更新 CSV |
| parameters | **嵌入預設**；serial／建置旗標可改 | LittleFS 覆寫 + 設定 UI |
| 完整和弦引擎 | 尚未移植；stub 仍用 `chords.cpp` | 讀上述表驅動 |

**v1 不做**完整 LittleFS 載入，除非之後明確要「不重燒即可改 mapping」。先把 CSV 放進 repo 的 `data/`，用腳本或手動同步到 C 陣列即可。

## 從 Python 引擎欄位遷移

| Python（`joystick writer`） | ESP32 對應 |
| --- | --- |
| `joystick_reader` 類比搖桿門檻／八向 | 矩陣離散鍵；`parameter_list` 的搖桿門檻在純按鈕板上可忽略或保留給未來類比 |
| `MappingRule.left_stick` / `right_stick` | 字串比對同名「左／右蘑菇頭 …」；內部先換成 `ButtonId` 或 premapping ID |
| `l1`/`r1`/`l2`/`r2` bool | `button_pressed(BTN_L1)` 等；**勿**對應到 Center |
| `trigger_timing`（放開時、立即＋連發、按下後等待＋連發…） | 狀態機 + `parameter_list` 的組合鍵視窗／連發／改判時間 |
| `keys` 羅馬字／鍵位模擬字串 | 既有 `hid_keyboard` 送鍵；JIS／US 參數決定符號鍵碼 |
| `config_loader.load_*` | 開機時解析嵌入字串或 LittleFS 檔；schema 保持 10 欄 mapping、參數名對照表 |

建議遷移順序：premapping／ButtonId 對齊 → 讀入 mapping 列成規則陣列 → 移植觸發時機狀態機 → 再接參數覆寫。本任務**不**移植引擎本體。

## 目錄現況

```
data/mapping_table.csv      ← 自 joystick writer 複製
data/parameter_list.csv     ← 同上（時機常數來源）
premapping.csv              ← 32 唯一代號（根目錄便利副本）
docs/esp32-jis-chordboard-premapping.csv  ← 文件用權威副本
docs/config-plan.md         ← 本計畫
docs/pinout.md              ← 矩陣 GPIO 與 cell
```
