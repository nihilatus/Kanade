# ESP32-S3 JIS 和弦鍵盤 — 14 鍵直連腳位

韌體是 **USB HID 鍵盤**（和弦 → 羅馬字／快捷鍵配方）。  
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

14 顆和弦鍵每一腳要連續穩定 **8 ms** 才承認按下或放開。全部放開後再等 **10 ms** 仍全放，才送出這一組和弦。GPIO15 的 WiFi 開關另外用約 50 ms，見下方。

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

## WiFi 開關

| 項目 | 值 |
| --- | --- |
| 腳位 | **GPIO15**（不屬於上面 14 個和弦鍵，不會進和弦遮罩） |
| 接法 | 一端接 GPIO15，另一端接 GND。`INPUT_PULLUP`，導通為 LOW |
| 極性 | 導通 = 開熱點；沒接或開路 = 關熱點 |
| 時機 | 運行中撥動就開關，不用重開機。穩態要連續約 **50 ms** 才承認，避免彈一下就把無線電開關一次 |

上電時若 GPIO15 是開路，熱點不會自己出現。以前是密碼有效就上電開熱點；現在要這顆開關合上才開。和弦與 USB HID 在熱點關掉時照常運作。

建議用維持型撥動開關。輕觸開關要按住才會開著，放開熱點就關。

## 一顆鍵怎麼接

1. 開關的一腳接到上表那隻 GPIO。
2. 開關的另一腳接到 DevKit 的 GND。
3. 14 顆都這樣接。不需要二極體，也不需要共行／共列。

零件：[`docs/bom.md`](bom.md)。和弦規則：[`docs/chord-map-machine.md`](chord-map-machine.md)。

## 手機改和弦

熱點不加入家裡的路由器，而且只在 GPIO15 接到 GND 時才開。手機連上後開瀏覽器改組合；存檔寫進 LittleFS 的 `chords.json`，下一組和弦就用新表。沒有存檔，或存檔壞掉、太大時，用燒錄進去的出廠表。撥開 GPIO15 會關掉熱點，已存的表仍留在板子上。

| 項目 | 值 |
| --- | --- |
| 熱點名稱 | `include/secrets.h` 的 `WIFI_AP_SSID`，範本是 `JIS-Chord` |
| 密碼 | 本機 `include/secrets.h` 的 `WIFI_AP_PASSWORD`，不進 git |
| 開關 | GPIO15 接到 GND 才開；沒接就關 |
| 網頁 | http://192.168.4.1/ |

複製 [`include/secrets.h.example`](../include/secrets.h.example) 成 `include/secrets.h`。密碼至少 8 碼，而且不能沿用範本裡的占位字；否則開關合上熱點也不會開。這是個人原型的設定頁，沒有 HTTPS。序列埠印開關腳位、熱點名稱與 `192.168.4.1`；關掉時印 `AP off`。不印密碼。
