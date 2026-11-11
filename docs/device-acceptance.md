# 實機驗收

給改完韌體、準備燒上板子的人。宿主測試 `pio test -e native` 覆蓋和弦判斷，以及檔案替換與熱點工作在記憶體裡的狀態。LittleFS 開機載入、USB HID 佇列、熱點開著時鍵盤不送鍵，要在板上看。

動到 `src/chords.cpp`、`src/hid_keyboard.cpp`、`src/config_server.cpp`、存檔路徑或 USB 送鍵時，燒錄後跑「每次大改必跑」。只改 `chord_core` 或人編 CSV 時，宿主測試與 [試打頁](../web/index.html) 就夠，不必每輪都拔電。

在紙上或複本勾選。不要把勾選提交進 git。每輪記日期、`git rev-parse --short HEAD`、通過或失敗；失敗時寫項次。

通過與否只看既有訊號：序列埠文字、HTTP 狀態與 JSON、記事本裡打出來的字。這份清單不新增序列指令，也不改存檔方式。

## 前置

| | |
| --- | --- |
| 板子 | ESP32-S3-DevKitC-1 N16R8。UART 跑 `pio device monitor`。OTG 接到主機 |
| 主機 | 鍵盤類型設成 Kanade 設定的配列。(目前只實作JIS)，記事本，日文 IME（羅馬字） |
| 熱點 | GPIO15 用維持型開關接到 GND 才開。手機或第二台電腦連上後開 http://192.168.4.1/ |
| 輸出 | GPIO16 維持開路，下面的 USB 鍵盤項才成立。導通到 GND 是藍牙，會重啟。輸出開關那一節才把 GPIO16 接地 |
| 密碼 | `include/secrets.h`。序列埠只印熱點名稱與網址，不印密碼 |

下面的 `curl.exe` 都要加 `-H "X-Api-Token: （你的 token）"`。`WIFI_API_TOKEN` 留空時，token 是第一次開機序列埠印的那一行，之後不再印；手寫的非空 token 不會印出來。token 不要寫進驗收紀錄。要換掉板上產生的那組，清掉 NVS 命名空間 `jis` 的鍵 `apitoken` 再重開。`curl.exe` 預設不帶 `Origin`，板上把這種請求當成熱點裡的非瀏覽器客戶端。

壞 JSON 與並行請求用連上熱點的電腦送。設定頁存不出截斷的內容。

目前出廠表（[`data/chord_map.machine.csv`](../data/chord_map.machine.csv)）裡這幾組用來對答案。人表改過並重新燒錄後，以新的機器表為準。

| 按法 | 記事本 |
| --- | --- |
| 只按 `LT1`，再放開 | `nn`（id 1） |
| 只按 `LL`，再放開 | `na`（id 66，`RT0` 可選） |
| 按住 `LT2` | `Backspace` 連射（id 26） |
| `LT0+RU` 按住 | `Up` 連射（id 4） |
| `LL+RT0+RT2` | `[` |
| `LR+RT0+RT2` | `]` |
| `LT0+LT2+RL` | `（`（`Shift+8`） |

開機應看到送出模式那一行：`送出模式：全放再送（全部放開並維持 N ms）`，或 `送出模式：首鍵放開就送（高峰後第一顆放開即送，其餘放開前不接新和弦）`。N 是 `kChordReleaseGuardMs`，目前是 10。沒有存檔時是 `chords: factory table (N rows)`。有有效存檔時是 `chords: loaded N rows from /chords.json`。`LittleFS 掛載失敗，格式化一次` 表示這輪的存檔已被清掉，存檔相關項不算通過。

表 A、表 B 只用一列，方便斷電後分辨。在連上熱點的電腦放兩個檔（GPIO15 維持接地）。PowerShell 裡用 `curl.exe`，`curl` 本身是 `Invoke-WebRequest`。

`table-a.json`：

```json
{"chords":[{"id":1,"required":["LL"],"optional":[],"kana":"","output":"aaa"}]}
```

`table-b.json` 只把 `aaa` 改成 `bbb`。送表 A：

```bat
curl.exe -s -H "Content-Type: application/json" --data-binary @table-a.json http://192.168.4.1/api/chords
```

成功正文是 `{"ok":true,"source":"saved","count":1}`。表 B 把檔名換成 `table-b.json`。

## 每次大改必跑

- [ ] **1. 開機**

  按 EN 重開，看 UART。沒有非預期的 `LittleFS 掛載失敗，格式化一次`。沒存檔印 `chords: factory table (N rows)`；有存檔印 `chords: loaded N rows from /chords.json`。送出模式那一行有印出。序列埠輸入 `fire` 再換行，回 `fire release` 或 `fire first`，與開機那一行一致。

- [ ] **2. 出廠送鍵**

  先還原出廠（第 5 項的做法），或確認開機是 factory。只按 `LT1` 再放開，記事本是 `nn`。這次若改了送出時機：序列埠 `fire release`，再打一次 `LT1`；然後 `fire first`，再打一次。兩次都是 `nn`，放開後不再多出字。`fire first` 之後那一行是首鍵放開就送。

- [ ] **3. 壞 JSON 不蓋活表**

  先打一次 `LT1`，記下記事本的字，並在序列埠看這次是出廠還是已存檔。然後送兩筆，都應 HTTP 400：

  `bad-cut.json` 的內容是一個字 `{`。`bad-overlap.json`：

  ```json
  {"chords":[{"id":1,"required":["LL"],"optional":["LL"],"kana":"","output":"a"}]}
  ```

  ```bat
  curl.exe -i -H "Content-Type: application/json" --data-binary @bad-cut.json http://192.168.4.1/api/chords
  curl.exe -i -H "Content-Type: application/json" --data-binary @bad-overlap.json http://192.168.4.1/api/chords
  ```

  第一筆正文 `{"ok":false,"error":"JSON 格式不對"}`。第二筆 `{"ok":false,"error":"第 1 列的同一顆鍵不能又必按又可選"}`。再打 `LT1`，輸出與送出前相同。重開機仍是原來的 factory 或 loaded 列數，不是這兩筆的內容。

- [ ] **4. 好存檔**

  送表 A。回應 `{"ok":true,"source":"saved","count":1}`。只按 `LL` 再放開，記事本是 `aaa`。重開機印 `chords: loaded 1 rows from /chords.json`。再按 `LL` 仍是 `aaa`。`LT1` 已不在這張表裡，序列埠是 `無符合規則 LT1`，記事本不增加字。

- [ ] **5. 還原出廠**

  還原前在序列埠送 `fire first`，確認印出首鍵放開就送。然後：

  ```bat
  curl.exe -i -X POST http://192.168.4.1/api/chords/reset
  ```

  回應 200，JSON 裡 `"source":"factory"`。只按 `LL` 是 `na`，只按 `LT1` 是 `nn`。重開機印 `chords: factory table (N rows)`，沒有 `loaded`。送出模式仍是首鍵放開；序列埠 `fire` 回 `fire first`。測完可 `fire release` 改回全放再送。

- [ ] **6. 存檔中途斷電**

  先送表 A，確認 `LL` 打出 `aaa`。再送表 B，手機或 `curl.exe` 還沒顯示成功時拔掉板子供電（USB 供電就拔線）。接回後看 UART，再按 `LL`。至少做 3 次。

  拔電點是統計的。人無法卡在某一次 rename。寫入順序是：寫完 `/chords.json.tmp` 並 flush、舊的 `/chords.json` 改名 `/chords.json.bak`、`.tmp` 改成 `/chords.json`。開機時活檔還在就用活檔；活檔不在才先驗證 `.tmp`，再驗證 `.bak`，通過才改回 `/chords.json`。

  每次只允許一種結局：

  - 仍是 A：`LL` 為 `aaa`。序列埠是 `loaded`，或先 `chords: restored /chords.json.bak` 再 `loaded`。
  - 已是 B：`LL` 為 `bbb`。序列埠是 `loaded`，或先 `chords: restored /chords.json.tmp` 再 `loaded`。
  - 出廠：`LL` 為 `na`，且 factory 那一行帶原因（例如 `saved table rejected`）。`LT1` 再次打出 `nn`。

  不允許 `LL` 打出 `aaa` 與 `bbb` 以外的半截、開機後已知和弦完全不打字、或記事本裡黏著 Shift／Ctrl。出現 `LittleFS 掛載失敗，格式化一次` 這項不通過。

- [ ] **7. 熱點開著不打字**

  還原出廠並重開，確認 `LT1` 是 `nn`。GPIO15 接地，序列埠有 `AP ` 與 `http://192.168.4.1/`。開著設定頁，或正在送表 A 時，連打 `LT1`，並按住 `LT2` 或 `LT0+RU`。記事本不增加字元，也沒有黏住的 Shift 或 Ctrl。

  序列埠輸入 `drops`。`recipes` 不因熱點開著或這輪按鍵而增加。`tokens` 只有配方裡有認不得的 token 才增加；出廠表這兩組應保持 0。撥開 GPIO15，印 `AP off`。再按 `LT1`，仍是 `nn`。

  另一次：開機時 GPIO15 已經接地。在撥開之前按 `LT1`，記事本不增加字。撥開並看到 `AP off` 之後，`LT1` 才是 `nn`。

## 動到該檔才加跑

前置與「每次大改必跑」照舊。下面只在該檔或它呼叫的存檔、送鍵路徑有改時加跑。

### chords.cpp

- [ ] **太大的 POST**

  先確認活表（出廠或表 A）。做一個大於 48 KiB（`kChordsJsonMax`）的檔再送：

  ```bat
  python -c "open('too-big.json','wb').write(b'{'+b'x'*50000)"
  curl.exe -i -H "Content-Type: application/json" --data-binary @too-big.json http://192.168.4.1/api/chords
  ```

  HTTP 413，正文 `{"ok":false,"error":"內容太大"}`。再打剛才那組和弦，輸出不變。刪掉 `too-big.json`。

- [ ] **另外三種 400**

  活表不變。三筆都是 400，`ok` 為 false：

  | 送出 | `error` |
  | --- | --- |
  | `{"count":1}` | `缺少 chords` |
  | `{"chords":[]}` | `至少要有一列` |
  | 兩列 `"id":1`，其餘欄位合法、鍵不重疊 | `第 2 列的 id 重複` |

  送完再打同一組和弦，輸出與送出前相同。

### hid_keyboard.cpp

- [ ] **主機還沒列舉**

  UART 保持接上。拔掉 OTG，等主機卸下這個鍵盤。按一次 `LT1`。接回 OTG，等記事本再次認得鍵盤。剛才那一次不補送 `nn`。序列埠 `drops` 的 `not_ready` 比拔線前大。在記事本打幾個字，沒有黏住的 Shift 或 Ctrl。

- [ ] **日文配列**

  出廠表。`LL+RT0+RT2` 是 `[`，`LR+RT0+RT2` 是 `]`，`LT0+LT2+RL` 是 `（`。主機鍵盤類型要與 Kanade 一致（目前是 JIS）。類型仍是美式時，這三組會對到美式符號，先改成同一套再判失敗。

- [ ] **送鍵耗時**

  熱點可以關著。連續打和弦超過 5 秒。序列埠約每 5 秒一行 `hid poll max N us`。這行只用來確認送鍵有在走、沒有把掃描卡住。不設 `N` 的上限。連打期間和弦明顯停住，或放開後字還一直增加，這項不通過。

### config_server.cpp

- [ ] **上一筆還沒完**

  先記下活表。兩個 `curl.exe` 同時送有效的 `POST /api/chords`（都是表 A，或一筆表 A、一筆表 B）。板上一次只收一個連線，工作還沒回到 Idle 也不接下一筆，所以常見是兩筆先後都 200，後完成的那張整份蓋過前一張。若第二筆在工作尚未結束時就被解析，則是 500、`{"ok":false,"error":"失敗"}`。兩種都通過：活表是成功那一筆的整份，或仍是送出前的整份。不允許 `LL` 的輸出混著兩筆的字。

- [ ] **不相符的 Origin**

  `fire-release.json` 的內容是 `{"mode":"release"}`。

  ```bat
  curl.exe -i -H "Origin: http://example.com" -H "Content-Type: application/json" --data-binary @fire-release.json http://192.168.4.1/api/fire
  ```

  HTTP 403，正文 `{"ok":false,"error":"來源不符"}`。序列埠 `fire` 與送出前相同。

- [ ] **傳輸中斷線**

  先讓 `LL` 的輸出確定（表 A 的 `aaa`，或出廠的 `na`）。開始送另一張表，body 還在傳、對端還沒拿到 `{"ok":true,...}` 時關掉手機 Wi-Fi，或對 `curl.exe` 按 Ctrl+C。再按 `LL`，仍是斷線前那一份的完整輸出。若 body 其實已經到齊、板上已經換成新表，`LL` 必須是新表的整份輸出，不能是半截。

### 輸出開關

GPIO16 開路是 USB，導通到 GND 是藍牙。切換會印 `output switch restart` 或 `wifi switch restart`，然後自己重啟。序列埠看 UART。

- [ ] **開路打字走 USB**

  GPIO16 開路，GPIO15 開路。重開。序列埠有 `output USB`，沒有 `output BLE`，也沒有 `BLE advertising`。主機把 OTG 認成鍵盤。只按 `LT1` 再放開，記事本是 `nn`。

- [ ] **導通後重啟走藍牙**

  維持型開關把 GPIO16 接到 GND。序列埠先 `output switch restart`，重開後是 `output BLE` 和 `BLE advertising`。OTG 不再是鍵盤（CDC 序列埠可以還在）。主機的藍牙設定配對名稱 `Kanade`。連上後序列埠有 `BLE connected`、`BLE encrypted`、`BLE subscribed`。只按 `LT1` 再放開，記事本是 `nn`。

- [ ] **已配對重開不用再配**

  配對成功後拔掉供電再接上，GPIO16 仍導通，GPIO15 開路。主機不要刪掉這台裝置。重開後鍵盤自己連回，`LT1` 仍是 `nn`，主機不再跳出新的配對要求。

- [ ] **藍牙模式下熱點讓位**

  GPIO16 維持導通、藍牙已連上時，GPIO15 接到 GND。序列埠是 `wifi switch restart`，不是在同一次開機裡印 `AP ` 之後繼續用藍牙打字。重開後有 `BLE held, AP only`，以及熱點的 `AP ` 與 `http://192.168.4.1/`。沒有 `BLE advertising`。連打 `LT1`，記事本不增加字。撥開 GPIO15，再一次 `wifi switch restart`。重開後回到 `output BLE` 與 `BLE advertising`，連上後 `LT1` 又是 `nn`。

- [ ] **USB 模式下熱點仍不重啟**

  GPIO16 開路。GPIO15 接到 GND，序列埠有 `AP `，沒有 `output switch restart`，也沒有 `wifi switch restart`。撥開 GPIO15，印 `AP off`，板子不重啟。再按 `LT1`，仍是 `nn`。

- [ ] **藍牙還沒訂閱不補送**

  GPIO16 導通，主機還沒連上或還沒訂閱（還沒看到 `BLE subscribed`）。按一次 `LT1`。等記事本認得鍵盤之後，剛才那一次不補送 `nn`。序列埠 `drops` 的 `not_ready` 比按之前大。之後打幾個字，沒有黏住的 Shift 或 Ctrl。
