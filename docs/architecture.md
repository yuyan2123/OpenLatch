# 系統架構

| 專案 | 職責 |
| --- | --- |
| [indoor-controller](../projects/indoor-controller/README.md) | ESP32-C3 門內控制器，處理 HomeKit、Home Key 憑證驗證、紅外線及繼電器輸出 |
| [outdoor-reader](../projects/outdoor-reader/README.md) | ESP-32S＋PN532，透過 Wi‑Fi 或 ESP‑NOW 轉送 NFC |

門外使用 ESP-32S、4 MB Flash 與 PN532 SPI。兩片板在同一區網時優先用加密 Wi‑Fi UDP，ESP‑NOW 為備援。兩條路徑使用相同的 HMAC、nonce 與重送規則。

控制器提供 1–800 ms 自訂脈衝（預設 200 ms）、2 秒冷卻與 3 秒啟動保護。HomeKey 使用獨立的 NFC 來源權限並共用輸出限制；沒有 HTTP 開門 API。連續驗證失敗預設 5 次後暫停 NFC 60 秒，可在 RFID 分頁設定；不影響 HomeKit 與 IR 授權。操作與限制見 [Home Key](../projects/indoor-controller/docs/homekey_setup.md)。

HomeKey 憑證、驗證與佈建留在門內。不能依門外「成功」訊息或 UID 直接開門。分工見 [整合計畫](../projects/outdoor-reader/docs/plan.md)。

各專案的韌體、測試、建置工具、硬體文件與照片放在各自目錄。跨設備通訊定義放在 [communication.md](communication.md)。共用程式待實際有共用需求時再抽出。
