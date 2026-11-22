# 門外轉送器驗證

## 1.5.1 首次 SELECT

COM4 已寫入 803,536 bytes，esptool 雜湊校驗通過並重置門外板。原生測試涵蓋首次 SELECT 回覆、失敗回報、封包重送與感應隔離。閒置後首次 iPhone 感應的改善幅度仍待實測。

## 2026-11-23 韌體 1.5.0

透過 COM4 寫入 802,192 bytes。esptool 確認雜湊校驗通過並重置裝置。

## 2026-11-23 NFC 連線修正

門外 ESP32 經 USB 寫入 802,192 bytes 並通過雜湊校驗。此次燒錄對應 `2cf439a` 的程式內容，早於 1.5.0 版號更新。雙板狀態機測試已通過；iPhone 紅色驚嘆號的發生率仍待現場量測。

## 2026-11-18 重連掃頻與通訊診斷

門外原生測試通過，同頻重連會立即送出 Hello，500 ms 後仍未綁定才續掃。案例涵蓋不連續的允許頻道、單一頻道與毫秒計數回繞。未配置映像建置使用 671,687 bytes 程式空間與 34,188 bytes 靜態 RAM。另一份隔離副本以虛構配置啟用完整通訊路徑，建置使用 769,832 bytes 程式空間與 42,920 bytes 靜態 RAM。USB 計數、感應耗時與實際重連時間待現場量測。

## 2026-11-17 獨立相依原始碼

- `pn532_cxx`、`pn532_hal`、`loggable` 改由根目錄 `third_party/` 提供，保留來源版本與 MIT 授權；不再需要完整上游專案或 submodule。
- 移除舊工作目錄後，`python tools/test.py` 全部通過；`python tools/build.py` 以 ESP-IDF 5.5.3 重新編譯、連結成功，程式空間 671,687 bytes、靜態 RAM 34,188 bytes，與先前未配置版本相同。
- 已檢查重新產生的 `compile_commands.json`：PN532 frontend 2 個、HAL 3 個與 logging 2 個編譯單元均來自 `third_party/`，沒有舊來源目錄路徑。未燒錄或接觸實機。

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

11 月 16 日測試時僅驗證門外端。門內 ESP-NOW 接收與 Home Key 驗證現已實作。PN532 收訊、手機／手錶交握、ESP-NOW 投遞與延遲仍待雙板實測。重連時分別測門內板在原頻道重啟、路由器改用另一個允許頻道，記錄兩次從租約到期至重新連線的時間。整體進度見[專案現況](../../README.md#目前狀態)。
