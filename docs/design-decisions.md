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

## 3. 設定頁自己寫 HTTP，不用套件

維持，而且這條是刻意留下的，不是還沒換成套件。`src/config_server.cpp` 自己讀寫 socket。不要為了程式較短改成 Arduino-ESP32 的 `WebServer`，也不要換成 ESPAsyncWebServer 或任何會在一筆請求裡等到收完、解析完的伺服器，以及非同步回呼裡改活表的伺服器。

熱點開著時本來就不掃描、不送 USB HID，藍牙輸出也還沒有。設定頁因此不必跟鍵盤或藍牙搶同一圈。這不夠成為換套件的理由，不要用「設定頁已經獨立」回頭改這一條。

要守的是主迴圈還趕得上處理熱點開關與序列埠。`config_server_poll()` 每一圈先看 GPIO15，再決定關熱點或（藍牙模式下）設重啟旗標；HTTP 最多搬一小段（`kHttpSlice`）。關熱點時 `stop_ap()` 會 `softAPdisconnect` 強制斷線，不必等請求傳完——但若某一圈卡在阻塞的 HTTP 讀寫裡，就永遠走不到 `poll_switch()`，也呼叫不到切斷或重啟。分片是為了趕得上那一步，不是為了優雅收完 body。同一圈還要收序列埠的 `switches`、`fire`、`drops`、`keys`、`trace`，堵住 HTTP 也會堵住這些指令。

路由只有設定頁、`/api/chords`、還原、`/api/fire`，一次只收一個連線。標頭檢查、token、Origin 在 `include/config_http.h`，工作協定在 `include/config_job.h`；宿主測試覆蓋這兩層純邏輯，不碰 Arduino，也**測不到** socket 狀態機。手刻不是為了「比較好測」：套件吃掉的只是 socket 狀態機，下面這些仍要自己寫，所以不換：

- `config_server_poll()` 每一圈最多搬 `kHttpSlice`。慢請求或過大的 body 不能拖住開關處理與序列埠。
- 和弦 JSON 的解析與 LittleFS 讀寫在另一個核心。那個工作開不起來時，GET／POST／還原回答 503，不要退回在這圈 loop 裡做。
- 活表只在這次 poll 換上。先 stage，成功才 commit。上一筆還沒結束不接下一筆。斷線不能留下半截表。
- 熱點開著時不掃描、不送鍵。USB 模式熱點關掉時，掃描與 USB HID 照常，不重啟。藍牙模式見下面的輸出開關：熱點開關再變就重啟，不在同一次開機裡改走藍牙。

路由變多、要從檔案系統送靜態檔、WebSocket、上傳檔案或 HTTPS 時，再重看非阻塞套件。在那之前維持自寫。

## 4. 暫不申請正式的 VID／PID

維持。現在是試作。程式裡是 Espressif VID `0x303A`、本地原型 PID `0x4004`（`src/hid_keyboard.cpp`）。在有別人要使用之前，不要為了看起來像產品而改這兩個數字，也不要向 USB-IF 申請自己的 VID。

之後若要給別人用，再向 Espressif 申請他們 VID 底下的 PID。

## 5. 日文轉換

維持。假名轉換是主機日文 IME 的事。主機鍵盤類型要設成 Kanade 設定的配列。(目前只實作JIS)。USB 國碼不設定，Windows 不會靠它切換版面。韌體只依配方送按鍵，一個 token 走下面其中一條。

字元組合。空白只切開 token，不送出；空白鍵是功能鍵 `Space`。不是功能鍵名、也不是修飾鍵+… 時，每個可印 ASCII（0x21–0x7E）各按一次。`a`、`nn`、`ltu`、`ya`、`kome` 都是逐字打出，假名交給電腦上的輸入法。`z [` 是 `z` 再 `[`，`z . z .` 是 `z`、`.`、`z`、`.`。

功能鍵。整字對上名稱才送該鍵，不拆成字母。出廠表裡有 Up、Down、Left、Right、`[ESC]`（Esc、ESC 同義）、Enter、Backspace（退格）、Space。Delete 解析器認得（HID 0x4C，向前刪除），這份出廠表沒有用。

修飾鍵複合。token 含 `+`，而且不是單獨一個 `+`。左邊是 Shift、Ctrl、Alt 或 Gui（最多四個），右邊是一個可印字元或一個功能鍵名。修飾鍵先按住，再按主鍵，然後全部放開。`Shift+1`、`Shift+/`、`Shift+8`、`Shift+9` 在 JIS 主機上是 ！、？、（、）。`Ctrl+i`、`Ctrl+u` 同一條路。

## 6. 其他已經定下來的取向

這些沒有放在上面五條，但同樣不要順手改方向。

| 取向 | 維持什麼 |
| --- | --- |
| 改配列 | 換配列時，輸出的複合鍵要套用另一張表。只能由熱點改，尚未實作。不設硬體開關。 |
| 裝置種類 | 鍵盤，不是手把，也不是類比搖桿。USB 模式是 USB HID。藍牙模式是 BLE HID。格鬥搖桿的按鈕只是開關。 |
| 按鍵接法 | 14 鍵各接一隻 GPIO 到 GND，內部上拉，按下為 LOW。沒有矩陣、沒有二極體、沒有外加上拉、沒有並在開關上的電容。 |
| 接腳對應 | 功能看符號，腳號只在 `include/pin_map.h`。掃描、熱點、輸出開關與送出時機都查這張表，不在別處寫 GPIO 號碼。新的輸入（例如移動腳）加一列，不放進 14 個和弦位元。行為開關的顆數與切換後果在 `include/mode_switches.h`；新的行為開關加一列，和弦鍵與其他輸入不加。 |
| 掃描與送鍵 | `loop` 不為了打完一串字而停住。配方排成佇列，到點最多送一個 HID report。重複字母（例如 `nn`）不用 `Keyboard.write()`：那會在主機一次 poll 之前把按下與放開都送出，兩下會併成一下。BLE 也是上一包送完才送下一包，重複字母不會在同一次連線事件裡壓成一下。佇列最後一格留給 `release-all`，避免修飾鍵漏到下一組。 |
| 純邏輯 | `chord_core` 不碰 Arduino、GPIO、檔案、USB。宿主測試 `pio test -e native` 編譯的就是這份。判斷規則要加在這裡，不要只寫進 `chords.cpp`。 |
| 表的來源 | 唯一可手改的是 `esp32-jis-chordboard-premapping.csv`。機器表與 `include/chord_map.inc` 由腳本產生。出廠表編進韌體，不必上傳 LittleFS 映像。手機存檔 `chords.json` 蓋過出廠表，直到網頁還原出廠。 |
| 配方裡認不得的 token | 裝置上略過那個 token，其餘仍送。略過紀錄溢位時仍送出其餘內容，只少記 token 文字。事件緩衝放不下，整份配方不送。出廠表不允許靠略過：建置時認不得的 token 會讓 `pio run` 失敗。 |
| 熱點 | 只在 GPIO15 接到 GND 時開 SoftAP，不加入家裡的路由器，沒有 HTTPS。密碼是信任邊界。`WIFI_API_TOKEN` 非空時用那組，不印到序列埠。留空時第一次開機產生 token、寫進 NVS，並只印一次；之後 `/api/*` 都要 `X-Api-Token`。瀏覽器仍要 `Origin` 是 `http://192.168.4.1` 或沒有這個標頭。密碼不印到序列埠，`include/secrets.h` 不進 git。 |
| 輸出開關 | GPIO16 開路是 USB HID，導通到 GND 是 BLE HID，一次一種。這塊板的 Arduino 預編是 NimBLE，不是經典藍牙。模式不寫進 NVS 或 LittleFS，切換消彈跳後先放開按鍵再重啟。配對是 Just Works bonding，金鑰在 NimBLE 自己的 NVS；切換不清 bond、不強制重新配對。藍牙模式若 GPIO15 也導通，不啟動 BLE，只服務熱點；這個開關再變也重啟。USB 模式的熱點仍是運行中開關。藍牙模式不呼叫 `Keyboard.begin()`，OTG 不列舉成鍵盤；既有 USB CDC 日誌不變。 |
| 送出時機 | GPIO17 開路是全放再送，導通到 GND 是首鍵放開就送。消抖後立刻換，不重啟，不寫進 NVS 或 LittleFS。熱點頁與序列埠 `fire` 只顯示；`POST /api/fire` 與 `fire release`／`fire first` 不改模式。試打頁的按鈕只影響瀏覽器。 |
| 記憶體 | 活表與 JSON DOM 放 PSRAM，把內部 RAM 留給 Wi-Fi。 |
| 試打頁 | `web/index.html` 用電腦鍵盤試規則，不送 USB HID。作業系統給的按下／放開已經是穩定邊緣，所以不做韌體的 `kChordDebounceMs`。送出時機、比對與連發呼叫由 `chord_core` 編成的 `web/chord_core.wasm`。 |
