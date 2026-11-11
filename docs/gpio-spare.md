# GPIO 餘裕

給之後加硬體、或考慮改腳的人。焊接與現行程式仍以 [`pinout.md`](pinout.md) 為準。使用者已決定**先不更動**目前腳位：不要為了這份說明去改 `src/buttons.cpp` 的腳號，或 `src/config_server.cpp` 的 `kWifiEnablePin`。

板子是 ESP32-S3-DevKitC-1 **N16R8**。`platformio.ini` 的 `board_build.arduino.memory_type = qio_opi`：模組內快閃走 Quad SPI，PSRAM 走 Octal。

## 韌體現在佔用的腳

15 隻。和弦鍵在 `src/buttons.cpp`，熱點開關在 `config_server.cpp`。

| 用途 | GPIO |
| --- | --- |
| LL LU LR LD LT0 LT1 LT2 | 1, 2, 4, 5, 6, 7, 8 |
| RL RU RR RD RT0 RT1 RT2 | 9, 10, 11, 12, 13, 14, 21 |
| 熱點開關（接到 GND 才開，不進和弦遮罩） | 15 |

順序跟和弦表鍵名相同，中間跳過 GPIO3（strapping）和 GPIO15（熱點）。這樣對表方便。選腳時沒有把 ADC、觸控留到最後。

每顆開關的接法見 [`pinout.md`](pinout.md)：一端 GPIO、一端 GND，`INPUT_PULLUP`，按下為 LOW。不要外加上拉，不要在開關上並電容，不要接到 5 V。這些腳是 3.3 V，不耐 5 V。

## 還能安全再接的腳

跟現有按鍵同一種接法（數位輸入、接到 GND、內部上拉），排針上還沒被韌體佔用的是：

**GPIO16、17、18、39、40、41、42、47**

第 9 隻要看這塊板的 RGB LED 焊在哪。Espressif 初版在 **GPIO48**，那時 **GPIO38** 可當一般腳；v1.1 改到 **GPIO38**，那時 **GPIO48** 可當一般腳。很多市售 N16R8 仿板的燈在 GPIO48。這份說明沒有對過手上那塊板的絲印。不確定就 38 和 48 都留著，只用上面 8 隻。

GPIO39–42 是外接 JTAG（MTCK、MTDO、MTDI、MTMS）。這份韌體用 USB 除錯，沒佔這四腳，當按鍵或一般輸出入沒問題；接上外部 JTAG 探針才會衝突。GPIO16 是 32 kHz 晶振的另一腳，這塊開發板沒裝那顆晶振。晶振要 GPIO15 配 GPIO16，而 GPIO15 已經是熱點開關，所以這對腳現在湊不齊。

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

當按鍵是安全的：USB、燒錄、PSRAM、strapping 都避開了。GPIO21 沒有 ADC、也沒有觸控，拿來當 RT2 合適。空著的 GPIO17、18 仍是 UART1 預設腳（17 送、18 收），GPIO39–42 仍可外接 JTAG。

能力上，已用的腳比空著的那 8 隻更難得。單純開關只需要內部上拉；那種最普通的腳，正是現在空著的 GPIO47、39、40、41、42，以及 38／48 裡不是燈的那一隻。

空腳補不回來的有兩項：

- **ADC1** 幾乎用完。GPIO1、2、4、5、6、7、8、9、10 全是和弦鍵。Wi-Fi 開著時仍能穩定讀類比的，排針上只剩 strapping 的 GPIO3。空著的 16、17、18 是 ADC2，熱點開著時讀數不可靠。以後加電位器或電池分壓，沒有同等的腳。
- **電容觸控**也用完。觸控只有 GPIO1–14。除了不能用的 GPIO3，其餘都在 14 個和弦鍵上。空腳沒有觸控。

還能改接到空腳、只是預設腳已被佔的：

- GPIO8、9 是預設 I2C（SDA、SCL），GPIO10–13 是預設 SPI。ESP32-S3 可以把 I2C、SPI、UART 改接到別的 GPIO，螢幕或感測器仍接得上，不能直接用函式庫的預設腳。SPI 要跑很高時脈時，硬體直連仍是 GPIO10–14，這幾腳現在都是按鍵。
- GPIO15 當熱點開關之後，32 kHz 晶振（GPIO15 配 GPIO16）就沒了。這份鍵盤用內部時鐘就夠，這個代價小。

線已經照 [`pinout.md`](pinout.md) 接好的話，維持現況。鍵盤本身用不到 ADC 和觸控。

## 若以後要求改腳

只有使用者明確要求重接時才動。若目的是把 ADC1 和觸控留出來，15 個輸入（14 鍵加熱點開關）按這個順序拿：

1. 先用 GPIO16、17、18、21、39、40、41、42、47，以及 38／48 裡不是板載 RGB 的那一隻（10 隻）。
2. 不夠再動 GPIO11、12、13、14。
3. 最後才動 GPIO10。

這樣留下的 ADC1 是 GPIO1、2、4、5、6、7、8、9。GPIO3 仍是 strapping，不要拿來補數。
