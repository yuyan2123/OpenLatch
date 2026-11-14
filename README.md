# ESP32-C3 門禁按鈕控制器

目前原始碼與實機版本為 **1.2.3-web-recovery**：修正設定頁一次查詢失敗就停止更新的問題，短暫斷線會自動重連並保留輸入，倒數改為瀏覽器逐秒更新。設定窗口開放時暫停 Wi-Fi 省電，關閉後還原。開門與設定頁各 **16 組紅外線按鍵**、操作續時的 10 分鐘窗口、全英文介面及暫停網頁登入均保留。GPIO4 驅動強度 3、**200 ms（0.2 秒）**脈衝與 2 秒冷卻時間不變。已燒錄至 COM7，hash 校驗通過。紀錄見 [VALIDATION.md](VALIDATION.md)。

接線與操作請參考 [繁體中文 PDF 說明書](docs/door_lock_manual_zh-TW.pdf)（10 頁，含零件照片、接線圖與英文網頁／IR 設定頁），另有 [Markdown 版本](docs/door_lock_manual_zh-TW.md)。第 3～4 頁為控制器側接線；對講機只能在原按鈕接點經確認後，依第 7 頁的條件式接法評估介接。

依照 [原始需求](door_lock_instructions.md) 與 `img/` 的零件照片實作。現在不需要接 ESP32；桌面模擬器與韌體直接共用 `firmware/DoorRelease/Controller.h`，不是另外寫一份 Python 門鎖邏輯。

Apple Home 的「Front Door Release」是一個瞬時開關。OFF 只代表控制器沒有執行釋放動作，**不代表門已關閉或上鎖**。本專案沒有門位或鎖舌感測器。

## 用手機設定 Wi-Fi 與紅外線

開機後提供 **10 分鐘**設定網頁；按已學習的設定專用紅外線按鍵，也能重新開放 10 分鐘。使用者點擊、輸入或捲動網頁時重新計時，閒置 10 分鐘關閉；背景狀態查詢不延長。**離線時例外：AP 與設定網頁持續開放，直到 Wi-Fi 連線成功**。沒有 Wi-Fi 設定時立即開 AP；已有設定但連不上時，等待 30 秒後開 AP，並每 30 秒嘗試重連。連線後關閉 AP，網頁依目前窗口決定是否繼續；之後斷線超過 30 秒，會再次開 AP 供修復。

1. 首次燒錄後，用手機連上 **`DoorRelease-XXXXXX`**，初始 AP 密碼為 **`DoorSetup-2026`**。
2. 瀏覽 **`http://192.168.4.1/`**；目前網頁不需帳號密碼。如果系統跳出的網路登入視窗無法操作，改用一般瀏覽器開啟此網址。
3. 在 **Wi-Fi connection** 填入 2.4 GHz 網路名稱與密碼；可展開 **Setup hotspot password**，用 **New hotspot password** 修改 AP 密碼。按 **Save and restart** 儲存、重新啟動；重新開機不會重設密碼。原登入驗證程式保留在註解內，日後恢復時仍使用同一組設定密碼。
4. 成功連線後，手機切回同一 Wi-Fi，開啟 **`http://door-release-xxxxxx.local/`**，或路由器顯示的裝置 IP。網址尾碼與 AP 相同，字母改小寫；存檔前網頁也會顯示此網址。
5. 在 **Remote buttons / Open the door** 按 **Add door button**，依畫面提示分開按同一按鍵兩次，每次之間放開至少 500 ms；30 秒內完成。新增不覆蓋既有按鍵，最多 16 組；可按 **Cancel learning** 取消。清單的 **Remove** 只刪除該筆，**Clear all door buttons** 則清除全部開門按鍵。
6. 在 **Open setup access** 按 **Add setup button**，完成相同的兩次學習；此用途也可儲存 16 組，並有逐筆移除與全部清除。設定按鍵只開放網頁，不開門；尚未設定時，先利用開機窗口進網頁學習。兩種用途不能共用同一按鍵，重複學習不多占名額。學習立即儲存，不必再按 Wi-Fi 的儲存按鈕。

Wi-Fi 與兩種用途的 IR 清單都存入 NVS，斷電仍保留。HomeKit 配對碼會在首次啟動時產生並儲存，網頁開放時可查看；Wi-Fi 改動不清除已有配對。不需要建立 `secrets.h`，也不需要為日常設定接電腦。首次燒錄仍使用 USB。

這裡的「連線成功」指連上 Wi-Fi 並取得 IP，不要求連上外網。HomeKit 和已學習的 IR 在設定網頁關閉後照常使用。網頁使用 `80` 埠；HomeKit 使用 `1201`，經 Bonjour 公布，無須在家庭 App 填埠號。完整流程與故障處理見 [Web setup](docs/web_setup.md)。PDF 說明書仍記錄舊版介面與登入流程，以本節及 Web setup 為準。

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
advance 199
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
| 經 HomeSpan 驗證的 ON 寫入 | 輸出 200 ms 脈衝 |
| ON 期間再次寫入 | 拒絕，不延長脈衝、不排隊 |
| 脈衝結束／提早停止 | 至少冷卻 2 秒才接受新 ON |
| 已授權 OFF | 立刻停止，並進入冷卻 |
| Wi-Fi／最後一條已驗證 HomeKit 連線中斷 | 停止 HomeKit 發起的脈衝；不影響 IR 發起的脈衝；恢復連線不自動觸發 |
| 已學習的 IR 按鍵 | 比對協定、位址、按鍵碼與位元數，觸發 200 ms 脈衝；可離線使用 |
| IR 長按／重複訊號 | 需先有至少 500 ms 沒有解碼訊號才接受新按下；repeat、overflow、校驗失敗與不支援協定不觸發 |
| HomeKit 與 IR 同時控制 | 共用 busy、2 秒冷卻與 3 秒啟動保護；不延長或排隊；HomeKit OFF 可停止任一來源的脈衝 |
| IR 學習中 | 暫停兩種來源的開門；同一按鍵分開按兩次確認；30 秒逾時或取消保留舊設定 |
| 無效脈衝設定／計時器啟動失敗 | OFF 並鎖住錯誤狀態，須重啟 |
| 主迴圈停頓 | `esp_timer` 獨立執行 OFF，正常設定 200 ms，程式上限 800 ms |

冷卻從主迴圈**觀察到停止**開始計算，所以主迴圈延遲時只會更晚接受下一次請求。重複抑制範圍是正在動作與冷卻期間；HomeKit `On` 沒有應用層請求 ID，冷卻後的新 ON 視為新指令。

控制器不提供 HTTP 解鎖 API、MQTT、OTA 或串列直接開門指令。HomeSpan 內建串列輸入、診斷與 AP 佈建入口已關閉，改由本專案的網頁處理設定；USB IR 設定與診斷保留。網頁登入依要求暫時停用；修改及活動續時仍需有效的本次啟動 token，拒絕跨來源請求，不回傳 Wi-Fi 或設定密碼。日誌不印 Wi-Fi、設定密碼或配對碼。所有韌體命名、註解及訊息使用英文。

## 紅外線學習與開門

接收板電源接 `3V3`、接地接 `GND`、訊號接 `GPIO3`。初始未學習任何按鍵，不會因紅外線訊號開門。IR 學習與開門不需要 `secrets.h`、Wi-Fi 或 HomeKit 配對。

1. 開啟上述手機設定網頁，在 **Remote buttons / Open the door** 按 **Add door button**。
2. 先放開遙控器按鍵至少 500 ms，再按一下要用來開門的按鍵。
3. 畫面顯示第一次按壓已擷取後，放開至少 500 ms，再按同一按鍵。需在啟動學習後 30 秒內完成。
4. 顯示 **Button learned and saved** 即完成，這兩次學習按壓不會開門。設定會存入 NVS，重開機仍保留。正常連線模式下若設定網頁到期，未完成的學習會取消並保留舊設定。
5. 再次放開後按該按鍵，會輸出 200 ms 開門脈衝。開機後前 3 秒及脈衝結束後 2 秒內不接受開門。

USB 設定仍可選用：串列監控設定為 **115200**，換行選 LF、CR 或 CRLF。

| USB 指令 | 用途 |
| --- | --- |
| `ir help` | 列出設定指令 |
| `ir status` | 查看硬體、儲存、已學習按鍵及啟用狀態 |
| `ir learn` | 新增一個開門按鍵，保留既有按鍵 |
| `ir cancel` | 取消學習，保留原按鍵 |
| `ir forget` | 清除全部開門按鍵並停用 IR 開門；也可修復損壞的 IR 紀錄，不影響 HomeKit 配對或設定按鍵 |

支援 NEC／NEC2、Sony、Samsung／SamsungLG、RC5、RC6、JVC；不提供未知波形學習。儲存寫入或讀回驗證失敗時，立即停用 IR；清除失敗後，舊設定仍可能於重開機載入，須重試 `ir forget` 直到成功。一般紅外線碼可以被複製及重播，接收器應安裝在受保護的室內範圍；雙次確認與長按防護不提供加密認證。完整英文操作與驗證步驟見 [IR setup](docs/infrared_setup.md)。

桌面互動模擬可輸入 `ir_learn`、`advance 500`、`ir`、`advance 500`、`ir` 完成學習，接著再 `advance 500`、`ir` 測試開門。`ir_repeat` 模擬重複碼；`ir_cancel`、`ir_forget`、`ir_status` 管理模擬設定。模擬設定只在同一次程序中跨 `reboot` 保留，不寫實機 NVS。

## 檔案

```text
firmware/DoorRelease/
  DoorRelease.ino       HomeSpan Switch、Wi-Fi 事件與 IR 接收
  Controller.h          與模擬器共用的控制狀態機
  TimedRelay.h          GPIO4 與獨立 esp_timer 關閉計時器
  Infrared.h            學習、按鍵比對、放開判定與逾時
  IrInput.h             支援協定與 IRremote 旗標轉換
  IrStore.h             NVS 單筆紀錄、版本與 CRC32 驗證
  SerialLine.h          有長度限制的非阻塞 USB 指令輸入
  DeviceSettings.h      Wi-Fi／登入密碼／配對碼與共用驗證
  SettingsStore.h       設定 NVS 紀錄與讀回驗證
  SetupPolicy.h         10 分鐘期限、重連與離線 AP 策略
  SetupPortal.h         登入、設定網頁、DNS 與設定 API
  SetupPage.h           自動產生、嵌入 flash 的網頁
  web/index.html        無外部相依的英文設定介面原始檔
  secrets.example.h     可選的出廠設定範本
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

目前不填任何密碼也能編譯。預設啟動設定 AP，由手機完成 Wi-Fi 及 IR 設定。編譯腳本會先執行 `tools/build_setup_page.py`，將 HTML 嵌入韌體。兩支 PowerShell 腳本均不燒錄、不尋找 COM port。

如果需要預填出廠設定，可複製 `secrets.example.h` 為同資料夾的 `secrets.h`；只在第一次沒有 NVS 設定時採用，之後以網頁儲存的設定為準。私密檔案已加入 `.gitignore`。Wi-Fi、設定密碼及配對資料會存進 NVS；這不是 flash 加密，請勿分享私有檔案、實機韌體或 flash 備份。

網頁瀏覽器驗證可另執行 `python tools/test_setup_page.py`（需已安裝 Chrome 及 `.tools/web-qa` 下的 Playwright）。此檢查使用本機模擬 API，不連實機；結果與限制見 [VALIDATION.md](VALIDATION.md)。

## 照片與實機邊界

- `IMG_0570`～`IMG_0572`：Songle SRD-05VDC-SL-C、5 V H/L 單路繼電器，預定 H 模式，GPIO4 HIGH 觸發。
- `IMG_0573`：1838／KY-022 類接收板，正面接腳朝下為 `-`、電源、`S`，預定 3.3 V 供電、GPIO3 訊號。
- `IMG_0601`～`IMG_0603`：ESP32-C3 SuperMini；`IMG_0602` 的商品圖視角與 `IMG_0603` 腳位圖不同，接線時認 GPIO 絲印，不只依左右位置。

虛擬接點的 OPEN／CLOSED 是理想模型。測試沒有驗證實際 HomeKit 加密配對、HomePod 遠端控制、IR 波形解碼、繼電器 3.3 V 觸發門檻、USB 5 V 路徑、開機毛刺、掉電或接點熔接。`esp_timer` 也是軟體計時器，不能保證整個 RTOS 停擺時的斷電時間。實機必須按原始文件 Stage A～E 分階段驗證；COM／NO 仍不得直接接入未確認的對講機端子。

實作 API 依據：[HomeSpan 2.1.8 原始碼](https://github.com/HomeSpan/HomeSpan/tree/2.1.8)、[HomeSpan API](https://github.com/HomeSpan/HomeSpan/blob/2.1.8/docs/Reference.md)、[IRremote 4.4.1](https://github.com/Arduino-IRremote/Arduino-IRremote/tree/v4.4.1)。
