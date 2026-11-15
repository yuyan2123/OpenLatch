# 系統架構

| 專案 | 職責 |
| --- | --- |
| [door-release](../projects/door-release/README.md) | 現有 ESP32-C3 控制器，處理 HomeKit、紅外線及繼電器輸出 |
| [outdoor-reader](../projects/outdoor-reader/README.md) | ESP-32S＋PN532，透過 ESP-NOW 轉送 NFC；不控制門鎖 |

門外使用 ESP-32S、4 MB Flash 與 PN532 SPI，加密 ESP-NOW 單播搭配 HMAC、nonce 與重送處理。門外端已實作，門內接收及獨立 HomeKey 驗證尚未實作。

現有控制器維持 200 ms 脈衝、2 秒冷卻與 3 秒啟動保護。後續若加入磁扣開門請求，需接入控制狀態機，沿用輸出限制；現行韌體沒有 HTTP 開門 API。

HomeKey 憑證、驗證與佈建留在門內。不能依門外「成功」訊息或 UID 直接開門。分工見 [整合計畫](../projects/outdoor-reader/docs/plan.md)。

各專案的韌體、測試、建置工具、硬體文件與照片放在各自目錄。跨設備通訊定義放在 [communication.md](communication.md)。共用程式待實際有共用需求時再抽出。
