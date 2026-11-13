# ESP32-C3 門禁按鈕控制器

接線與操作請參考 [繁體中文 PDF 說明書](docs/door_lock_manual_zh-TW.pdf)（8 頁，含零件照片與接線圖），另有 [Markdown 版本](docs/door_lock_manual_zh-TW.md)。第 3～4 頁為控制器側接線；對講機只能在原按鈕接點經確認後，依第 7 頁的條件式接法評估介接。

依照 [原始需求](door_lock_instructions.md) 與 `img/` 的零件照片實作。現在不需要接 ESP32；桌面模擬器與韌體直接共用 `firmware/DoorRelease/Controller.h`，不是另外寫一份 Python 門鎖邏輯。

Apple Home 的「Front Door Release」是一個瞬時開關。OFF 只代表控制器沒有執行釋放動作，**不代表門已關閉或上鎖**。本專案沒有門位或鎖舌感測器。

## 立即執行虛擬測試

在此資料夾執行（目前環境已準備編譯器）：

```powershell
python tools/simulate.py test
python tools/simulate.py demo
python tools/simulate.py interactive
```

測試範圍與結果見 [虛擬驗證紀錄](VALIDATION.md)。

第一次在其他電腦使用，若沒有 g++ 或 clang++，先安裝專案內的可攜式編譯器：

```powershell
python -m pip install --target .tools/python ziglang==0.14.1
```

互動模擬範例：

```text
wifi on
session on
advance 3000
on
advance 399
status
advance 1
status
on
advance 2000
ir
on
wifi off
trace
quit
```

`session on` 僅模擬一條已驗證 HomeKit 連線；這個本機程式不實作 HomeKit 配對、加密或網路服務。時間不會自行流動，`advance` 推進虛擬時間並執行主迴圈，`stall` 推進時間但暫停主迴圈；兩者都會執行獨立虛擬計時器。`unauth` 模擬未授權寫入；`fault` 鎖住控制器直到 `reboot`。

## 已實作行為

| 條件 | 行為 |
| --- | --- |
| 啟動／重啟 | GPIO4 LOW，不恢復先前請求；啟動後 3 秒內禁止動作 |
| 經 HomeSpan 驗證的 ON 寫入 | 輸出 400 ms 脈衝 |
| ON 期間再次寫入 | 拒絕，不延長脈衝、不排隊 |
| 脈衝結束／提早停止 | 至少冷卻 2 秒才接受新 ON |
| 已授權 OFF | 立刻停止，並進入冷卻 |
| Wi-Fi／最後一條已驗證 HomeKit 連線中斷 | 停止；恢復連線不自動觸發 |
| IR 訊號 | 解碼診斷；未知、repeat、overflow 略過；絕不解鎖 |
| 無效脈衝設定／計時器啟動失敗 | OFF 並鎖住錯誤狀態，須重啟 |
| 主迴圈停頓 | `esp_timer` 獨立執行 OFF，正常設定 400 ms，程式上限 800 ms |

冷卻從主迴圈**觀察到停止**開始計算，所以主迴圈延遲時只會更晚接受下一次請求。重複抑制範圍是正在動作與冷卻期間；HomeKit `On` 沒有應用層請求 ID，冷卻後的新 ON 視為新指令。

控制器不提供 HTTP 解鎖 API、MQTT、OTA、串列解鎖或 IR 解鎖設定。HomeSpan 串列輸入、內建診斷輸出與 AP 佈建入口已關閉；程式自己的日誌只記錄指令結果與 IR 代碼，不印 Wi-Fi 或配對密碼。恢復設定請在現場使用 USB 重新設定／清除 flash。

## 檔案

```text
firmware/DoorRelease/
  DoorRelease.ino       HomeSpan Switch、Wi-Fi 事件與 IR 接收
  Controller.h          與模擬器共用的控制狀態機
  TimedRelay.h          GPIO4 與獨立 esp_timer 關閉計時器
  secrets.example.h     私密設定範本
sim/                   虛擬 GPIO、接點、計時器與互動介面
tests/                 C++ 行為測試、隨機事件與真實計時器驅動的 OS 模擬測試
tools/                 模擬、環境安裝、僅編譯韌體的工具
```

## 韌體編譯（仍不需要硬體）

固定相依版本：Arduino-ESP32 **3.3.0**、HomeSpan **2.1.8**、IRremote **4.4.1**。使用 ESP32-C3 generic board，4 MB flash 與 Huge APP（3 MB 應用程式分割區，無 OTA）。安裝資料都放在 `.tools/`。

```powershell
powershell -ExecutionPolicy Bypass -File tools/setup.ps1
powershell -ExecutionPolicy Bypass -File tools/build-firmware.ps1
```

目前不填任何密碼也能編譯。沒有有效 `secrets.h` 的韌體會維持 OFF，不啟動 Wi-Fi／HomeSpan；這不影響桌面模擬。兩支腳本均不燒錄、不尋找 COM port。

未來準備實機時，複製 `secrets.example.h` 為同資料夾的 `secrets.h`，填入 2.4 GHz Wi-Fi SSID、密碼及私下選擇的八位 HomeKit 配對碼。檔案已加入 `.gitignore`。設定會進入韌體，HomeSpan 配對資料會存進 NVS；這不是 flash 加密，請勿分享私有檔案、實機韌體或 flash 備份。使用自己保管的配對碼加入 Apple Home，不使用預設碼。

## 照片與實機邊界

- `IMG_0570`～`IMG_0572`：Songle SRD-05VDC-SL-C、5 V H/L 單路繼電器，預定 H 模式，GPIO4 HIGH 觸發。
- `IMG_0573`：1838／KY-022 類接收板，正面接腳朝下為 `-`、電源、`S`，預定 3.3 V 供電、GPIO3 訊號。
- `IMG_0601`～`IMG_0603`：ESP32-C3 SuperMini；`IMG_0602` 的商品圖視角與 `IMG_0603` 腳位圖不同，接線時認 GPIO 絲印，不只依左右位置。

虛擬接點的 OPEN／CLOSED 是理想模型。測試沒有驗證實際 HomeKit 加密配對、HomePod 遠端控制、IR 波形解碼、繼電器 3.3 V 觸發門檻、USB 5 V 路徑、開機毛刺、掉電或接點熔接。`esp_timer` 也是軟體計時器，不能保證整個 RTOS 停擺時的斷電時間。實機必須按原始文件 Stage A～E 分階段驗證；COM／NO 仍不得直接接入未確認的對講機端子。

實作 API 依據：[HomeSpan 2.1.8 原始碼](https://github.com/HomeSpan/HomeSpan/tree/2.1.8)、[HomeSpan API](https://github.com/HomeSpan/HomeSpan/blob/2.1.8/docs/Reference.md)、[IRremote 4.4.1](https://github.com/Arduino-IRremote/Arduino-IRremote/tree/v4.4.1)。
