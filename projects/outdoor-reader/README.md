# OpenLatch 門外 NFC 轉送器

ESP-32S／ESP32-WROOM-32、4 MB Flash，搭配 SPI 模式 PN532。門外優先透過同一區網的 Wi‑Fi 轉送 NFC 交握，並以 ESP‑NOW 備援。Home Key 驗證、憑證與開門權限留在門內控制器。

韌體使用根目錄 [third_party](../../third_party/README.md) 中的 PN532 通訊元件，獨立建置；來源版本與授權隨專案保存。

門內 `indoor-controller` 已加入 ESP-NOW 接收、DigitalDoorKey HomeKey 驗證、HomeKit NFC 憑證佈建與錯誤次數限制。目前韌體版本為 **1.5.0**。雙板 NFC 修正版已於 2026-11-23 完成 USB 燒錄；手機首次感應成功率仍待實測。 見 [門內配置](../indoor-controller/docs/homekey_setup.md)。

```text
firmware/main/      ESP-NOW 與 PN532 韌體
include/            封包與轉送狀態機
tests/              本機測試
tools/              編譯、測試、離線配置
docs/               接線、協定與整合計畫
private/            本機配置，Git 忽略
build/              建置輸出，Git 忽略
```

## 接線

PN532 切換到 **SPI** 模式；開關組合依模組絲印／手冊確認。先斷電接線。

| PN532 | ESP-32S |
| --- | --- |
| SCK | GPIO18 |
| MISO | GPIO19 |
| MOSI | GPIO23 |
| SS／SSEL | GPIO21 |
| GND | GND |
| VCC | 模組支援 3.3 V 供電時接 3V3 |

SPI 訊號使用 3.3 V 邏輯。若模組只標示 5 V 電源輸入，需先確認其電源及邏輯電路；不要把 5 V 訊號接入 ESP32。IRQ 不需接。門外不接繼電器、開門接點或門鎖控制線。詳見 [實機驗證](docs/hardware.md)。

## PN532 感應測試

執行 `python tools/build.py --card-test` 建置獨立測試韌體，輸出在 `build/pio/esp32s-card-test/`。燒錄到門外板後，GPIO2 LED 開機亮一下，感應到相容的 13.56 MHz 卡片時亮起，移開後熄滅。USB 序列埠（115200）會顯示 `Card detected` 和 `Card removed`。

可用 MIFARE、FeliCa 或 ISO/IEC 14443 Type A／B 卡片測試。測試韌體不需要門內板或 Home Key。測完後燒回 `build/pio/esp32s/` 的配對版，即可恢復 NFC 轉送。

## 建置與測試

以下從 `projects/outdoor-reader/` 執行。所需元件已隨 repository 放在 `third_party/`，不需要額外取得上游專案。

```powershell
python -m venv .tools/build-venv
./.tools/build-venv/Scripts/python.exe -m pip install platformio==6.1.19
./.tools/build-venv/Scripts/python.exe tools/build.py
python tools/test.py
```

PlatformIO 固定 `espressif32@6.13.0`（ESP-IDF 5.5.3）；工具下載至本專案 `.tools/platformio/`。Windows 建置期間暫用一個空閒磁碟代號避免長路徑，完成後移除；不修改系統設定。建置指令不搜尋串列埠、不燒錄。輸出為 `build/pio/esp32s/firmware.bin`。

本機測試需要 g++、clang++ 或 Zig，也可使用 `indoor-controller` 已安裝的 Zig。需要時執行 `python -m pip install --target .tools/python ziglang==0.14.1`。Windows 使用 CNG HMAC；Linux 需要 OpenSSL 開發套件。

未配置時可以編譯。韌體只印出自身 **station MAC**，不啟動 NFC 轉送，供首次取得板子身分。

## 配置通訊金鑰

取得兩塊板子的 Wi-Fi **station MAC** 後，產生獨立、隨機的 PMK、LMK 與 HMAC 金鑰：

```powershell
python tools/provision.py --indoor-mac AA:BB:CC:DD:EE:F0 --outdoor-mac AA:BB:CC:DD:EE:F2
./.tools/build-venv/Scripts/python.exe tools/build.py --require-provisioned
```

MAC 僅示範格式，必須換成實際板子。工具建立 `private/credentials.h` 與 `private/indoor-peer.json`，後者供門內實作使用。兩份都包含私密通訊金鑰，不可分享或加入 Git；已配置的韌體映像也包含這些金鑰。既有配置不會被覆寫；`--force` 才會輪替兩份金鑰。

如要走區網，先在本機終端執行 `python tools/configure_wifi.py`，輸入與門內板同一區網的 2.4 GHz Wi‑Fi 名稱與密碼，再建置並燒錄門外韌體。Wi‑Fi 設定存於 Git 忽略的 `private/reader_wifi.h`，也包含在建置映像中。設定頁的 RFID 狀態顯示目前使用的連線。

未設定 Wi‑Fi 或區網交握逾時時，門外走 ESP‑NOW。預設搜尋頻道 1～11，`--channels 1,6,11` 可縮小範圍。門內使用路由器目前的頻道。連線失效會結束當次感應，再以新的工作階段交握。

ESP‑NOW 失聯後會先在上次完成交握的允許頻道送出 Hello。等候 500 ms 後仍未連線，才繼續輪巡允許的頻道。首次搜尋從編號最小的允許頻道開始。

USB 序列紀錄每 10 秒列出目前頻道、連線狀態及三個自開機累計的數值。`rx_queue_full` 是接收佇列已滿而丟棄的封包數，`tx_submit_failed` 是無線驅動拒收的傳送請求數，`tx_delivery_failed` 是驅動接受請求後回報投遞失敗的次數。每次感應結束另列 `duration_ms`，從讀取器開始處理標籤算到工作迴圈觀察到交握結束，包含逾時個案。

詳細格式見 [通訊協定](docs/protocol.md)，分工見 [整合計畫](docs/plan.md)，結果見 [VALIDATION.md](VALIDATION.md)。通訊金鑰只證明設備身分；門內必須獨立驗證當次 HomeKey，不能依卡號或門外自報成功開門。
