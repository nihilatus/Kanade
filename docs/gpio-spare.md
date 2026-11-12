# GPIO 餘裕

給之後加硬體、或考慮改腳的人。焊接與現行程式以 [`pinout.md`](pinout.md) 為準。腳號只寫在 [`include/pin_map.h`](../include/pin_map.h)。

板子是 ESP32-S3-DevKitC-1 **N16R8**。`platformio.ini` 的 `board_build.arduino.memory_type = qio_opi`：模組內快閃走 Quad SPI，PSRAM 走 Octal。

## 韌體現在佔用的腳

17 隻，腳號在 [`include/pin_map.h`](../include/pin_map.h)。`src/buttons.cpp` 掃描和弦鍵，`config_server.cpp` 讀熱點開關，`output_switch.cpp` 讀輸出開關，`fire_switch.cpp` 讀送出時機。

| 用途 | GPIO | 排針 |
| --- | --- | --- |
| LL LU LR LD LT0 LT1 LT2 | 17, 18, 10, 11, 12, 13, 14 | J1 |
| RL RU RR RD RT0 RT1 RT2 | 42, 41, 40, 39, 38, 48, 47 | J3 |
| 熱點開關（接到 GND 才開，不進和弦遮罩） | 15 | J1 |
| 輸出開關（開路 USB，接到 GND 是藍牙，不進和弦遮罩） | 16 | J1 |
| 送出時機（開路全放再送，接到 GND 是首鍵放開就送，不進和弦遮罩） | 21 | J3 |

左手鍵集中在 J1、右手鍵集中在 J3（USB 朝下時左／右排）。空出 GPIO1、2、4–9，保留 ADC1 與預設 I2C（SDA／SCL）。GPIO38／48 各接一顆右手鍵；韌體不驅動板載 RGB，板子版本不同時其中一隻可能是 LED 資料線。

每顆開關的接法見 [`pinout.md`](pinout.md)：一端 GPIO、一端 GND，`INPUT_PULLUP`，按下為 LOW。不要外加上拉，不要在開關上並電容，不要接到 5 V。這些腳是 3.3 V，不耐 5 V。

## 還能安全再接的腳

跟現有按鍵同一種接法（數位輸入、接到 GND、內部上拉），排針上還沒被韌體佔用的是：

**GPIO1、2、4、5、6、7、8、9**

以上八隻含完整可用 ADC1 與預設 I2C。GPIO8／9 是函式庫常預設的 SDA／SCL。

GPIO39–42 已當右手和弦鍵。這四腳也是外接 JTAG（MTCK、MTDO、MTDI、MTMS）；這份韌體用 USB 除錯，沒佔 JTAG，當按鍵沒問題；接上外部 JTAG 探針才會衝突。GPIO15 配 GPIO16 可接 32 kHz 晶振；兩腳分別是熱點開關和輸出開關，這對腳湊不齊。這塊開發板通常沒裝那顆晶振；鍵盤用內部時鐘就夠。

## 不要用

| 腳 | 原因 |
| --- | --- |
| GPIO19 / GPIO20 | USB HID 的 D− / D+ |
| GPIO0 / GPIO3 / GPIO45 / GPIO46 | 開機 strapping。GPIO0 還是板上 BOOT |
| GPIO43 / GPIO44 | 燒錄用 UART0，已焊到板載 USB 轉序列晶片 |
| GPIO35 / GPIO36 / GPIO37 | Octal PSRAM。排針有印，接到外電路會干擾記憶體 |
| GPIO26–34 | 沒有引出。26–32 給模組內快閃；33–34 在這顆 Octal PSRAM 配置裡同樣屬於記憶體匯流排 |
| GPIO22–25 | 這顆晶片沒有這幾腳 |

## 現用腳當按鍵是否合適

當按鍵是安全的：USB、燒錄、PSRAM、strapping 都避開了。GPIO21 沒有 ADC、也沒有觸控，拿來當送出時機開關合適。GPIO17 是 UART1 預設的送腳，現在是 LL；這份韌體不用 UART1。空著的 GPIO1–2、4–9 可接類比或 I2C。預設 SPI 腳 GPIO10–14 在左手鍵上；ESP32-S3 可把 SPI 改接到別的 GPIO，硬體直連高時脈仍偏好那幾腳。

能力上，空著的腳比已用的更適合以後加電位器、觸控或預設 I2C 裝置：

- **ADC1** 空著 GPIO1、2、4、5、6、7、8、9。Wi-Fi 開著時仍能穩定讀類比。GPIO16 是 ADC2（輸出開關），熱點開著時讀數不可靠。
- **電容觸控**在 GPIO1–14。空著的 1、2、4–9 都有觸控；10–14 已是左手鍵。
- **預設 I2C** GPIO8（SDA）、9（SCL）空著。
- 預設 SPI GPIO10–13 與 GPIO14 在左手鍵上；要外接高速 SPI 時再考慮改腳或改接到空腳。

## 若以後再要求改腳

只有使用者明確要求重接時才動。輸出開關留在 GPIO16，除非連它也要改。熱點開關目前在 GPIO15。送出時機已在 GPIO21。若還要再空出 SPI 預設腳，優先把左手鍵從 GPIO10–14 挪到仍空著的腳，或接受改接 I2C／SPI 到非預設腳。
