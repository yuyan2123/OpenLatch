# OpenLatch NFC protocol 1

此協定轉送 NFC APDU，不傳送開門命令或 HomeKey 驗證結論。門內是唯一的驗證者。兩端都必須完成本文件要求；門外程式不等於完整的兩端實作。

## 傳輸

ESP-NOW v2 加密單播，固定雙方 station MAC、16-byte PMK 與 LMK。收件者比對來源／目的 MAC，再檢查 HMAC-SHA256 前 16 bytes。HMAC 使用獨立 32-byte key，以常數時間比對。

HMAC 輸入：ASCII `OpenLatch/NFC/1`（無 NUL）、來源 MAC 6 bytes、目的 MAC 6 bytes、header、payload。整數為 little-endian。最大封包 1096 bytes，兩端 SDK 必須支援 ESP-NOW v2。

| Offset | Bytes | 欄位 |
| --- | --- | --- |
| 0 | 4 | ASCII `OLNF` |
| 4 | 1 | version=1 |
| 5 | 1 | type |
| 6 | 1 | code，正常為 0 |
| 7 | 1 | reserved=0 |
| 8 | 4 | seq，從 1 起，不回繞 |
| 12 | 4 | tap，當次感應編號 |
| 16 | 4 | request |
| 20 | 2 | payload length，0～1024 |
| 22 | 2 | reserved=0 |
| 24 | 16 | outdoor nonce |
| 40 | 16 | indoor nonce |
| 56 | variable | payload |
| 56+length | 16 | HMAC tag |

兩方向 seq 各自遞增。門內重啟／重連時產生新隨機 indoor nonce，並檢查 outdoor nonce；只比對 MAC／HMAC 不足以處理重播。

## 訊息

| Type | 方向 | 內容 |
| --- | --- | --- |
| 1 Hello | 外→內 | 新 outdoor nonce；indoor nonce、tap、request、length=0 |
| 2 Configure | 內→外 | seq=1；回填 outdoor nonce、新 indoor nonce；tap/request=0；payload=8-byte HomeKey group ID |
| 3 Ping | 內→外 | tap/request/length=0，維持租約 |
| 4 Tag | 外→內 | 新 tap、request=0；payload=`UID length(1), UID(1..10), ATQA(2), SAK(1)` |
| 5 Exchange | 內→外 | 當次 tap、從 1 遞增的 request；payload=C-APDU，4～252 bytes |
| 6 Response | 外→內 | 回填 Exchange 的 tap/request；payload=R-APDU，包含 SW1/SW2 |
| 7 Release | 內→外 | 結束 tap；request/length=0 |
| 8 Ack | 外→內 | request=已接受的門內 seq；回填 tap，無 payload |
| 9 Error | 外→內 | 保留，目前未使用 |

Response code=3 表示 PN532 交換失敗，payload 為空，tap 已結束。其他保留 code 不構成授權結果。R-APDU 仍由門內 DigitalDoorKey 驗證。

## 流程與重送

1. 開機時門外從最小的允許頻道送 Hello。已連線的讀取器失聯時先在原頻道送 Hello，之後每 500 ms 輪巡下一個允許頻道。正確來源、HMAC 與 nonce 的 Configure 才能停止搜尋。
2. 門內提供公開 group ID；門外產生 ECP、初始化 PN532 並回 Ack。
3. 門內每秒送 Ping。租約 5 秒，只有新的有效請求續時，重送不續時。
4. 偵測到卡片後送 Tag；第一個 Exchange／Release 前，每 150 ms 原樣重送。
5. 門內產生 C-APDU，門外轉送並回傳完整 R-APDU；每次只允許一個未完成的 Exchange。
6. 門內完成／取消驗證後送 Release。只有門內驗證成功能觸發開門，每個 session 最多一次。

PN532 APDU 交換逾時 500 ms。門內可先等 800 ms，再原樣重送同一 Exchange，最多重試 1 次；仍失敗則取消當次驗證。整次 tap 最長 5 秒，新 Exchange 間閒置最長 1500 ms。這些時序仍需手機／讀取器實測。等待 Exchange 時不要插入 Ping，以免覆蓋單筆快取。

只有最後一筆、所有欄位與 payload 相同的輸入，才能重送相同回覆，不重做 NFC 指令。相同 seq 不同內容、舊 seq、跳號 request、錯誤 tap／nonce 都丟棄。門內需接受已處理的重複 Response／Tag，且不重做驗證步驟或開門。

Tag 是非同步事件，不會清除控制請求的回覆快取。快取 Ack／Response 或 Tag 重送可能晚於較新的事件抵達；門內需以當次 nonce、tap、request 及待完成請求判斷，不能只以最高 seq 丟棄所有較舊回覆。Ping 不需要等待 Ack 才能處理 Tag；Exchange 則維持一次一筆。

租約失效時取消 tap、更換 outdoor nonce、清除快取並恢復搜尋。重啟也更換 nonce。結束 tap 或剛開機時，需至少 500 ms 持續未偵測到 NFC 目標才接受新感應；這不是獨立的物理距離感測。

PN532 opcode、狀態及長度都會檢查；錯誤或仍有未處理 chaining 的回覆不當成完整 R-APDU。上游發送端使用 normal frame，C-APDU 上限因此設為 252 bytes；門內的 APDU chaining 必須為 header／Le 保留空間，不能直接沿用 255-byte data chunk。接收端 DMA buffer 上限為 265 bytes（含 TFI、opcode 與狀態），更長回覆會失敗而非截斷。若實機 HomeKey 需要額外 PN532 chaining，需補上受測試的支援。

## 安全邊界

門外的通訊金鑰被取得後，攻擊者仍能偽造讀取器訊息。HomeKey 身分、私鑰、已授權 endpoint 與驗證 session 必須留在門內。普通卡片 UID 不能作為此威脅模型下的獨立開門證明。

本協定不保證防止干擾、設備破壞，或即時轉送遠端合法 NFC 憑證的中繼攻擊。

參考：[Espressif ESP-NOW](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c3/api-reference/network/esp_now.html)、[Wi-Fi 頻道共存](https://docs.espressif.com/projects/esp-faq/en/latest/application-solution/esp-now.html#can-wi-fi-be-used-with-esp-now-at-the-same-time)。
