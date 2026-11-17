# 門外 NFC 轉送器

ESP-32S／ESP32-WROOM-32、4 MB Flash，搭配 SPI 模式 PN532。門外只轉送 NFC 交握；HomeKey 驗證、憑證與開門權限屬於門內控制器。

韌體重用 [HomeKey-ESP32](HomeKey-ESP32/README.md) 的 PN532 通訊元件與 ECP 探測流程，獨立建置。上游 submodule 保持原樣。

門內 `door-release` 已加入 ESP-NOW 接收、DigitalDoorKey HomeKey 驗證、HomeKit NFC 憑證佈建與錯誤次數限制。**雙板 MAC 配置與真實 iPhone／Watch 驗證仍待實機完成，尚未燒錄本次整合。** 見 [門內配置](../door-release/docs/homekey_setup.md)。

```text
HomeKey-ESP32/       上游 submodule
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

## 建置與測試

以下從 `projects/outdoor-reader/` 執行。重新取得 repository 後，先在 repository 根目錄執行 `git submodule update --init --recursive`。

```powershell
python -m pip install --target .tools/python platformio==6.1.19
python tools/build.py
python tools/test.py
```

PlatformIO 固定 `espressif32@6.13.0`（ESP-IDF 5.5.3）；工具下載至本專案 `.tools/platformio/`。Windows 建置期間暫用一個空閒磁碟代號避免長路徑，完成後移除；不修改系統設定。建置指令不搜尋串列埠、不燒錄。輸出為 `build/pio/esp32s/firmware.bin`。

本機測試需要 g++、clang++ 或 Zig，也可使用 `door-release` 已安裝的 Zig。需要時執行 `python -m pip install --target .tools/python ziglang==0.14.1`。Windows 使用 CNG HMAC；Linux 需要 OpenSSL 開發套件。

未配置時可以編譯。韌體只印出自身 **station MAC**，不啟動 NFC 轉送，供首次取得板子身分。

## 配置通訊金鑰

取得兩塊板子的 Wi-Fi **station MAC** 後，產生獨立、隨機的 PMK、LMK 與 HMAC 金鑰：

```powershell
python tools/provision.py --indoor-mac AA:BB:CC:DD:EE:F0 --outdoor-mac AA:BB:CC:DD:EE:F2
python tools/build.py --require-provisioned
```

MAC 僅示範格式，必須換成實際板子。工具建立 `private/credentials.h` 與 `private/indoor-peer.json`，後者供門內實作使用。兩份都包含私密通訊金鑰，不可分享或加入 Git；已配置的韌體映像也包含這些金鑰。既有配置不會被覆寫；`--force` 才會輪替兩份金鑰。

預設搜尋頻道 1～11，`--channels 1,6,11` 可縮小範圍。門內保持路由器目前的頻道，回覆門外 Hello 後開始交握。門外不需要家庭 Wi-Fi 密碼。

詳細格式見 [通訊協定](docs/protocol.md)，分工見 [整合計畫](docs/plan.md)，結果見 [VALIDATION.md](VALIDATION.md)。通訊金鑰只證明設備身分；門內必須獨立驗證當次 HomeKey，不能依卡號或門外自報成功開門。
