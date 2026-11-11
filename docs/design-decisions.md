# 設計取捨

給之後改這份韌體的人，尤其是 AI。這裡只記已經決定、**目前維持**的事。架構與操作看 [README](../README.md)、[腳位](pinout.md)、[和弦表](chord-map-machine.md)。使用者沒有改口之前，不要把下面任何一條做成待辦，也不要順手「修好」。

## 1. 打到沒有對應的和弦，不給使用者回饋

維持。沒有符合的遮罩不送 USB HID，也不閃燈、不蜂鳴、不在主機打出錯誤字。使用者會自己記得哪些和弦有效。

序列埠印的「無符合規則」與遮罩是給接上 UART 的人看的診斷，不是給打字的人的回饋。留著這行，不要把它改成 HID 輸出，也不要為了「沒有回饋」把這行刪掉。`drops`、`trace` 同樣只在序列埠。

## 2. 多按一鍵，整組和弦不送出

維持。比對是嚴格的：`required ⊆ 高峰遮罩 ⊆ required ∪ optional`。多出來的鍵不在那一列的 optional 裡，這組就沒有符合列，於是整組丟掉。不要做模糊比對、最近列、或「多按的鍵自動忽略」。

人編表裡的 `(RT0)` 是目前唯一的 optional。那是表上的規則，不是誤觸容錯。

硬體預定用鍵盤開關，甚至格鬥搖桿的按鈕，同時多按一顆的機會低。手感靠使用者練習，不靠韌體猜。

高峰仍是按住期間同時按下數量最多的那一組；同樣多顆時留較晚的那一組。全放再送，以及首鍵放開就送，兩種時機都保留。連射列（`repeat=allow`）的 50 ms／400 ms／33 ms 由韌體自己計時，不跟 Windows 的鍵盤重複速度。

## 3. 設定頁自己寫 HTTP，不用 Arduino-ESP32 的 WebServer

維持。`src/config_server.cpp` 自己讀寫 socket，是為了不要卡住鍵盤掃描。不要為了程式較短改回 `WebServer` 或同類會在請求裡等到收完、解析完的伺服器。

要守住的行為：

- `config_server_poll()` 每一圈最多搬一小段（`kHttpSlice`）。慢請求或過大的 body 不能停住掃描。
- 和弦 JSON 的解析與 LittleFS 讀寫在另一個核心。那個工作開不起來時，GET／POST／還原回答 503，不要退回在掃描迴圈裡做。
- 活表只在這次 poll 換上。先 stage，成功才 commit。
- 熱點關掉時，掃描與 USB HID 照常。

## 4. 暫不申請正式的 VID／PID

維持。現在是試作。程式裡是 Espressif VID `0x303A`、本地原型 PID `0x4004`（`src/hid_keyboard.cpp`）。在有別人要使用之前，不要為了看起來像產品而改這兩個數字，也不要向 USB-IF 申請自己的 VID。

之後若要給別人用，再向 Espressif 申請他們 VID 底下的 PID。

## 5. 其他已經定下來的取向

這些沒有放在上面四條，但同樣不要順手改方向。

| 取向 | 維持什麼 |
| --- | --- |
| 日文轉換 | 韌體送羅馬字與少數特殊鍵。假名轉換是主機日文 IME 的事。主機鍵盤類型要設成 Kanade 設定的配列。(目前只實作JIS)。USB 國碼不設定，Windows 不會靠它切換版面。 |
| 改配列 | 熱點頁不能改。不加指撥開關。 |
| 裝置種類 | USB HID 鍵盤，不是手把，也不是類比搖桿。格鬥搖桿的按鈕只是開關，韌體仍以鍵盤列舉。 |
| 按鍵接法 | 14 鍵各接一隻 GPIO 到 GND，內部上拉，按下為 LOW。沒有矩陣、沒有二極體、沒有外加上拉、沒有並在開關上的電容。 |
| 掃描與送鍵 | `loop` 不為了打完一串字而停住。配方排成佇列，到點最多送一個 HID report。重複字母（例如 `nn`）不用 `Keyboard.write()`：那會在主機一次 poll 之前把按下與放開都送出，兩下會併成一下。佇列最後一格留給 `release-all`，避免修飾鍵漏到下一組。 |
| 純邏輯 | `chord_core` 不碰 Arduino、GPIO、檔案、USB。宿主測試 `pio test -e native` 編譯的就是這份。判斷規則要加在這裡，不要只寫進 `chords.cpp`。 |
| 表的來源 | 唯一可手改的是 `esp32-jis-chordboard-premapping.csv`。機器表與 `include/chord_map.inc` 由腳本產生。出廠表編進韌體，不必上傳 LittleFS 映像。手機存檔 `chords.json` 蓋過出廠表，直到網頁還原出廠。 |
| 配方裡認不得的 token | 裝置上略過那個 token，其餘仍送。略過紀錄溢位時仍送出其餘內容，只少記 token 文字。事件緩衝放不下，整份配方不送。出廠表不允許靠略過：建置時認不得的 token 會讓 `pio run` 失敗。 |
| 熱點 | 只在 GPIO15 接到 GND 時開 SoftAP，不加入家裡的路由器，沒有 HTTPS。密碼是信任邊界。`WIFI_API_TOKEN` 非空時用那組，不印到序列埠。留空時第一次開機產生 token、寫進 NVS，並只印一次；之後 `/api/*` 都要 `X-Api-Token`。瀏覽器仍要 `Origin` 是 `http://192.168.4.1` 或沒有這個標頭。密碼不印到序列埠，`include/secrets.h` 不進 git。 |
| 記憶體 | 活表與 JSON DOM 放 PSRAM，把內部 RAM 留給 Wi-Fi。 |
| 試打頁 | `web/index.html` 用電腦鍵盤試規則，不送 USB HID。作業系統給的按下／放開已經是穩定邊緣，所以不做韌體的 `kChordDebounceMs`。送出時機、比對與連發呼叫由 `chord_core` 編成的 `web/chord_core.wasm`。 |
