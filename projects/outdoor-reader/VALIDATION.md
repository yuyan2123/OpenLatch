# 門外轉送器驗證

日期：2026-11-16。目標 ESP-32S／ESP32-WROOM-32、4 MB Flash、PN532 SPI。

## 本機測試

`python tools/test.py` 全部通過。使用 Zig 0.14.1（C++17，`-Wall -Wextra -Werror`）、韌體共用的 Protocol／ReaderSession、Windows CNG HMAC-SHA256 與受控 Reader 替身。

- RFC 4231 HMAC 向量、獨立 Python 產生的 wire/HMAC 向量、封包往返、來源／目的 MAC 與方向綁定。
- 每個封包位元翻轉、截斷、超長、5000 個固定種子的無效封包拒絕。
- nonce 交握、有效連線不可取代、舊 epoch 拒絕。
- Tag 重送、相同 APDU 請求只執行一次、快取回覆、錯誤 tap／request／seq 拒絕。
- 租約、最長期限、閒置逾時、32-bit 時鐘回繞及持續無卡片的重啟感應門檻。
- 讀取器失敗、無效長度、PN532 狀態與 ECP CRC-A。
- 配置地址／頻道驗證、兩端金鑰一致、禁止意外覆寫與明確輪替。

尚未測真實 RF／RTOS／HomeKey。

## 韌體

`python tools/build.py` 通過，`esp32dev`，PlatformIO `espressif32@6.13.0`／ESP-IDF 5.5.3、Xtensa GCC 14.2.0。

| 組態 | 程式用量 | 靜態 RAM | 結果 |
| --- | --- | --- | --- |
| 未配置，轉送停用 | 671,687 bytes | 34,188 bytes | 通過 |
| 虛構配置副本，完整轉送程式啟用 | 770,368 bytes | 42,992 bytes | 通過 |

4 MB Flash，預設單一 1 MB 應用程式分割區足夠。輸出為 `build/pio/esp32s/firmware.bin`，另有同目錄 bootloader／partition table。

完整路徑編譯檢查在 `build/configured-check/` 的獨立副本進行，使用虛構 MAC 與隨機測試通訊金鑰。未在正式專案建立 `private/credentials.h`；測試映像不可部署。紀錄為 `build/firmware-build.log`、`build/firmware-configured-check.log`。

Windows 工具透過暫時的磁碟代號處理 ESP-IDF 長路徑，完成後自動移除。上游 fmt 的字串巨集在 PlatformIO/SCons 會被拆開；獨立專案改用相同 fail-stop 意圖的 compiler trap，上游原始碼保持原樣。

預設未配置版本不啟動 NFC 轉送；正式配置需實際 MAC 與本機產生的金鑰。

## 實機邊界

本次未燒錄或操作繼電器。PN532 收訊、手機／手錶交握、ESP-NOW 遺失／延遲與跨頻道復原待實機驗證。門內接收與 HomeKey 驗證整合尚未實作。
