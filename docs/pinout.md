# Kanade — 14 鍵直連腳位

韌體是 **USB HID 鍵盤**（和弦 → 字元組合／功能鍵／修飾鍵複合）。  
實體輸入是 **14 顆開關各接一隻 GPIO**，沒有矩陣、沒有二極體。

## 板子與 USB

| 項目 | 選擇 |
| --- | --- |
| 板子 | ESP32-S3-DevKitC-1 **N16R8** |
| HID | 標示 **USB** 的 OTG 口 |
| 燒錄 | 標示 **UART** 的口 |
| USB D− / D+ | **GPIO19 / GPIO20** — 不可當按鍵腳 |

## 為什麼不走矩陣

14 鍵用一腳一 GPIO 就夠。一鍵一腳沒有鬼鍵，多鍵同時按下也不靠二極體。舊的 6×6 矩陣是 32 鍵手把時期的接法，不要再照那份清單焊接。

## 電氣

每顆開關：一端接下表的 GPIO，另一端接 **GND**。

- 韌體把腳設成 `INPUT_PULLUP`。沒按是 HIGH，按下被拉到 LOW。
- 不要外加上拉電阻，也不要在開關兩端並電容。電容會把邊緣拖長，消彈跳與和弦判斷都會失真。
- 開關只接 GPIO 與 GND。不要接到 5 V、VIN 或其他電源腳。
- 麵包板杜邦線盡量短於約 30 cm。

14 顆和弦鍵每一腳要連續穩定 `kChordDebounceMs`（`include/settings.h`）才承認按下或放開。何時送出一組和弦見 [`chord-map-machine.md`](chord-map-machine.md)。熱點開關、輸出開關、送出時機開關都在 `include/pin_map.h`，前兩顆和送出時機共用 `kSwitchDebounceMs`，見下方。有幾顆行為開關、開路與導通各代表什麼、撥動後會不會重啟，見 `include/mode_switches.h`。

## 不可使用的腳

| 腳 | 原因 |
| --- | --- |
| GPIO19 / GPIO20 | USB D− / D+ |
| GPIO0 / GPIO3 / GPIO45 / GPIO46 | strapping |
| GPIO43 / GPIO44 | UART0（燒錄） |
| GPIO35 / GPIO36 / GPIO37 | octal PSRAM（N16R8） |

## 按鍵 → GPIO

| 鍵 | GPIO | 鍵 | GPIO |
| --- | ---: | --- | ---: |
| LL | 1 | RL | 9 |
| LU | 2 | RU | 10 |
| LR | 4 | RR | 11 |
| LD | 5 | RD | 12 |
| LT0 | 6 | RT0 | 13 |
| LT1 | 7 | RT1 | 14 |
| LT2 | 8 | RT2 | 21 |

GPIO3 刻意空下（strapping）。順序與和弦表的鍵名順序相同：`LL LU LR LD LT0 LT1 LT2 RL RU RR RD RT0 RT1 RT2`。

程式只經 [`include/pin_map.h`](../include/pin_map.h) 查腳。和弦判斷用功能符號與位元，不寫 GPIO 號碼。改腳或加一顆輸入只改那張表。移動腳加一列，不要放進這 14 個和弦位元。

還能再接哪些腳、現用腳佔走了哪些以後難補的功能，見 [`gpio-spare.md`](gpio-spare.md)。腳位先維持上表。

## WiFi 開關

| 項目 | 值 |
| --- | --- |
| 腳位 | **GPIO15**（不屬於上面 14 個和弦鍵，不會進和弦遮罩） |
| 接法 | 一端接 GPIO15，另一端接 GND。`INPUT_PULLUP`，導通為 LOW |
| 極性 | 導通 = 開熱點；沒接或開路 = 關熱點 |
| 時機 | USB 模式運行中撥動就開關，不用重開機。藍牙模式見下方輸出開關：熱點要開或關都會重啟。穩態要連續 `kSwitchDebounceMs`（`include/settings.h`）才承認，避免彈一下就把無線電開關一次 |

上電時若 GPIO15 是開路，熱點不會自己出現。以前是密碼有效就上電開熱點；現在要這顆開關合上才開。熱點開著時不讀和弦鍵、不送鍵。USB 模式熱點關掉時，掃描與 USB HID 照常，不重啟。開關合上但熱點沒起來（密碼沒設）時，USB 模式鍵盤繼續；藍牙模式仍然不啟動藍牙。

## 輸出開關

| 項目 | 值 |
| --- | --- |
| 腳位 | **GPIO16**（不屬於 14 個和弦鍵，也不進和弦遮罩） |
| 接法 | 一端接 GPIO16，另一端接 GND。`INPUT_PULLUP`，導通為 LOW |
| 極性 | 沒接或開路 = USB 有線鍵盤。導通 = BLE 鍵盤。開路讀到的是 HIGH |
| 時機 | 開機讀一次。之後穩態和開機不同，先放開按鍵再重啟。穩態要連續 `kSwitchDebounceMs`（`include/settings.h`）才承認 |

一次只送一種鍵盤。這塊板是 ESP32-S3，藍牙是 BLE HID，沒有經典藍牙。模式不寫進 NVS，也不寫進 LittleFS。

藍牙配對用 Just Works bonding。金鑰由 NimBLE 寫進它自己的 NVS，不放進命名空間 `jis`。從 USB 切到藍牙再切回來，不清除配對、不強制重新配對。第一次進藍牙模式要在主機的藍牙設定裡配對，名稱是 `Kanade`。還沒連上、或主機還沒訂閱輸入報告時打下去的和弦不補送。

藍牙模式不呼叫 USB 鍵盤的 `begin()`，OTG 不會列舉成鍵盤。既有的 USB CDC 日誌還在。

開機時若 GPIO16 導通、而且 GPIO15 也導通，不啟動藍牙，只開熱點。藍牙已經在跑時 GPIO15 變成導通，這次開機不起熱點，直接重啟；下一輪才只服務熱點。這次是因為藍牙讓位才開的熱點，GPIO15 打開後同樣重啟，下一輪才啟藍牙。USB 模式沒有這個重啟。

建議用維持型撥動開關。輕觸開關要按住才會開著，放開熱點就關。

## 送出時機開關

| 項目 | 值 |
| --- | --- |
| 腳位 | **GPIO17**（不屬於 14 個和弦鍵，也不進和弦遮罩） |
| 接法 | 一端接 GPIO17，另一端接 GND。`INPUT_PULLUP`，導通為 LOW |
| 極性 | 沒接或開路 = 全放再送。導通 = 首鍵放開就送。開路讀到的是 HIGH |
| 時機 | 開機讀一次。之後穩態連續 `kSwitchDebounceMs` 不同就立刻換，不重啟，也不寫進 NVS 或 LittleFS |

全放再送：這一組全部放開，再維持 `kChordReleaseGuardMs`，才送高峰。首鍵放開就送：高峰之後，這一組裡最先放開的那顆起來就送，其餘還按著的先不當下一組。正在等全放的空檔時撥到首鍵放開，等待中的那一組會馬上送出。反方向只影響下一次判斷。

熱點頁只顯示目前是哪一種，不能在網頁上改。序列埠 `fire` 只查詢；`fire release` 和 `fire first` 不再寫入。試打頁的按鈕只影響那個瀏覽器。

建議用維持型撥動開關。沒接這顆開關時內部上拉，就是全放再送。

## 一顆鍵怎麼接

1. 開關的一腳接到上表那隻 GPIO。
2. 開關的另一腳接到 DevKit 的 GND。
3. 14 顆都這樣接。不需要二極體，也不需要共行／共列。

零件：[`docs/bom.md`](bom.md)。和弦規則：[`docs/chord-map-machine.md`](chord-map-machine.md)。

## 手機改和弦

熱點不加入家裡的路由器，而且只在 GPIO15 接到 GND 時才開。手機連上後開瀏覽器改組合；存檔寫進 LittleFS 的 `chords.json`，下一組和弦就用新表。沒有存檔，或存檔壞掉、太大時，用燒錄進去的出廠表。撥開 GPIO15 會關掉熱點，已存的表仍留在板子上。

| 項目 | 值 |
| --- | --- |
| 熱點名稱 | `include/secrets.h` 的 `WIFI_AP_SSID`，範本是 `Kanade` |
| 密碼 | 本機 `include/secrets.h` 的 `WIFI_AP_PASSWORD`，不進 git |
| 開關 | GPIO15 接到 GND 才開；沒接就關 |
| 網頁 | http://192.168.4.1/ |

複製 [`include/secrets.h.example`](../include/secrets.h.example) 成 `include/secrets.h`。密碼至少 8 碼，而且不能沿用範本裡的占位字；否則開關合上熱點也不會開。這是個人原型的設定頁，沒有 HTTPS。`WIFI_API_TOKEN` 留空時，板子在第一次開機產生一組 token、寫進 NVS（命名空間 `jis`、鍵 `apitoken`），並從序列埠印一次 `API token（只印這一次）:`。之後開機沿用同一組、不再印。填了非空字串則用那組，不印、也不改 NVS；把這行改回空字串後，下次開機回到板上已存的那組。兩種情況 `/api/*` 都要帶 `X-Api-Token`。存不進 NVS 時設定 API 關閉。瀏覽器若從別的網站對這個位址送請求會被拒，`Origin` 必須是 `http://192.168.4.1` 或沒有這個標頭。網頁被拒後會問 token。`include/secrets.h` 不可進 git，編譯前會檢查它有沒有被追蹤。序列埠印開關腳位、熱點名稱與 `192.168.4.1`；關掉時印 `AP off`。不印密碼，也不印手寫 token。要換產生的那組，清掉 `apitoken` 再重開。掛載 LittleFS 失敗時會先印一行再格式化一次，避免一開機就無聲清掉 `chords.json`。
