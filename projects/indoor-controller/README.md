# OpenLatch ESP32-C3 門禁按鈕控制器

本專案位於 `projects/indoor-controller/`。以下指令均從此目錄執行；若目前在 repository 根目錄，先執行 `cd projects/indoor-controller`。其他專案見 [OpenLatch](../../README.md)。

目前原始碼為 **1.5.0**：免密碼模式下，OTA 不需密碼；啟用 **Require a password for changes and firmware updates** 後，設定修改、密碼變更／停用與 OTA 必須驗證目前的設定密碼。設定熱點仍一律使用密碼。更新會先完整驗證映像，再重啟；網頁確認新的開機識別、映像 SHA-256 與開機健康後，才顯示 **Update complete**。**舊版首次啟用必須經 USB 安裝雙 OTA 分割區**；之後才可透過網頁更新。操作、首次安裝與限制見 [OTA 更新](docs/ota_setup.md)。

門內 Apple Home Key 驗證、ESP-NOW 轉送接收與 HomeKit NFC 憑證佈建保留。**RFID** 分頁可設定連續錯誤次數與暫停秒數，預設 **5 次／60 秒**；有效驗證清除連續錯誤，重送不重計，讀取／通訊／儲存故障不算憑證錯誤。未配置雙板 MAC 與通訊金鑰時，NFC 保持停用。配置步驟見 [Home Key](docs/homekey_setup.md)。

**Other settings / Door release** 可自訂開門時間 **1–800 ms**，預設 **200 ms（0.2 秒）**，HomeKit、紅外線與已驗證 Home Key 共用。按 **Save and restart** 後生效，斷電仍保留。v1–v3 設定自動載入，保留 Wi-Fi、HomeKit 配對碼、IR 按鍵及已有自訂時間。GPIO4 驅動強度 3、2 秒冷卻與兩種用途各 16 組 IR 按鍵均保留。

2026-11-23 已透過 USB 寫入 NFC 修正並通過雜湊校驗。紀錄見 [VALIDATION.md](VALIDATION.md)。

接線與操作請參考 [繁體中文 PDF 說明書](docs/openlatch_manual_zh-TW.pdf)（10 頁，含零件照片、接線圖與英文網頁／IR 設定頁），另有 [Markdown 版本](docs/openlatch_manual_zh-TW.md)。第 3～4 頁為控制器側接線；對講機只能在原按鈕接點經確認後，依第 7 頁的條件式接法評估介接。

依照 [原始需求](openlatch_instructions.md) 與 `img/` 的零件照片實作。現在不需要接 ESP32；桌面模擬器與韌體直接共用 `firmware/OpenLatch/Controller.h`，不是另外寫一份 Python 門鎖邏輯。

Apple Home 的「OpenLatch」保留瞬時開關，新增 Home Key 所需的鎖與 NFC 服務。OFF 只代表控制器沒有執行釋放動作，**不代表門已關閉或上鎖**；鎖的實體狀態回報 Unknown。本專案沒有門位或鎖舌感測器。

## 用手機設定 Wi-Fi 與紅外線

開機後提供 **10 分鐘**設定網頁；按已學習的設定專用紅外線按鍵，也能重新開放 10 分鐘。使用者點擊、輸入或捲動網頁時重新計時，閒置 10 分鐘關閉；背景狀態查詢不延長。**離線時例外：AP 與設定網頁持續開放，直到 Wi-Fi 連線成功**。沒有 Wi-Fi 設定時立即開 AP；已有設定但連不上時，等待 30 秒後開 AP，並每 30 秒嘗試重連。連線後關閉 AP，網頁依目前窗口決定是否繼續；之後斷線超過 30 秒，會再次開 AP 供修復。

1. 首次燒錄後，用手機連上 **`OpenLatch-XXXXXX`**，初始 AP 密碼為 **`DoorSetup-2026`**。
2. 瀏覽 **`http://192.168.4.1/`**；目前網頁不需帳號密碼。如果系統跳出的網路登入視窗無法操作，改用一般瀏覽器開啟此網址。
3. 在 **Wi-Fi connection** 填入 2.4 GHz 網路名稱與密碼；可展開 **Setup hotspot password**，用 **New hotspot password** 修改 AP 密碼。按 **Save and restart** 儲存、重新啟動；重新開機不會重設密碼。預設管理操作免密碼；勾選 **Require a password for changes and firmware updates** 後儲存啟用。啟用時，設定頁上方的 **Current setup password** 必須填入舊密碼，才能修改設定、IR 按鍵、密碼或關閉保護；OTA 使用更新表單內的目前密碼。空白新密碼代表保留，不代表停用保護。
4. 成功連線後，手機切回同一 Wi-Fi，開啟 **`http://openlatch-xxxxxx.local/`**，或路由器顯示的裝置 IP。網址尾碼與 AP 相同，字母改小寫；存檔前網頁也會顯示此網址。
5. 在 **Remote buttons / Open the door** 按 **Add door button**，依畫面提示分開按同一按鍵兩次，每次之間放開至少 500 ms；30 秒內完成。新增不覆蓋既有按鍵，最多 16 組；可按 **Cancel learning** 取消。清單的 **Remove** 只刪除該筆，**Clear all door buttons** 則清除全部開門按鍵。
6. 在 **Open setup access** 按 **Add setup button**，完成相同的兩次學習；此用途也可儲存 16 組，並有逐筆移除與全部清除。設定按鍵只開放網頁，不開門；尚未設定時，先利用開機窗口進網頁學習。兩種用途不能共用同一按鍵，重複學習不多占名額。學習立即儲存，不必再按 Wi-Fi 的儲存按鈕。
7. 在 **Other settings / Door release** 的 **Relay on time (ms)** 輸入整數 1–800，按 **Save and restart**。例如 500 代表繼電器吸合 0.5 秒；冷卻時間仍為 2 秒。切換分頁前若有未儲存變更，可選擇儲存、放棄或取消。
8. 在 **RFID** 設定 **Failed attempt limit**（1–20）及 **Lockout time (seconds)**（1–3600），按 **Save and restart**。只限制 NFC，不封鎖 HomeKit 或 IR。次數與暫停計時保留在門內 RAM，重啟門內控制器會清除；重啟門外板不會清除。頁面同時顯示配置、憑證、連線及鎖定狀態。

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
| 經 HomeSpan 驗證的 ON 寫入 | 輸出設定時間的脈衝（預設 200 ms，可設 1–800 ms） |
| ON 期間再次寫入 | 拒絕，不延長脈衝、不排隊 |
| 脈衝結束／提早停止 | 至少冷卻 2 秒才接受新 ON |
| 已授權 OFF | 立刻停止，並進入冷卻 |
| Wi-Fi／最後一條已驗證 HomeKit 連線中斷 | 停止 HomeKit 發起的脈衝；不影響 IR 發起的脈衝；恢復連線不自動觸發 |
| 已學習的 IR 按鍵 | 比對協定、位址、按鍵碼與位元數，使用相同設定時間；可離線使用 |
| IR 長按／重複訊號 | 需先有至少 500 ms 沒有解碼訊號才接受新按下；repeat、overflow、校驗失敗與不支援協定不觸發 |
| HomeKit 與 IR 同時控制 | 共用 busy、2 秒冷卻與 3 秒啟動保護；不延長或排隊；HomeKit OFF 可停止任一來源的脈衝 |
| IR 學習中 | 暫停兩種來源的開門；同一按鍵分開按兩次確認；30 秒逾時或取消保留舊設定 |
| 無效脈衝設定／計時器啟動失敗 | OFF 並鎖住錯誤狀態，須重啟 |
| 主迴圈停頓 | `esp_timer` 依設定時間獨立執行 OFF，預設 200 ms，程式上限 800 ms |

冷卻從主迴圈**觀察到停止**開始計算，所以主迴圈延遲時只會更晚接受下一次請求。重複抑制範圍是正在動作與冷卻期間；HomeKit `On` 沒有應用層請求 ID，冷卻後的新 ON 視為新指令。

控制器不提供 HTTP 解鎖 API、MQTT 或串列直接開門指令。HomeSpan 內建串列輸入、診斷與 AP 佈建入口已關閉，改由本專案的網頁處理設定；USB IR 設定與診斷保留。瀏覽設定頁不需登入；管理操作依儲存的密碼模式驗證。修改及活動續時仍需有效的本次啟動 token，拒絕跨來源請求，不回傳 Wi-Fi 或設定密碼。活動續時不提供設定或 OTA 權限。日誌不印 Wi-Fi、設定密碼或配對碼。所有韌體命名、註解及訊息使用英文。

## 紅外線學習與開門

接收板電源接 `3V3`、接地接 `GND`、訊號接 `GPIO3`。初始未學習任何按鍵，不會因紅外線訊號開門。IR 學習與開門不需要 `secrets.h`、Wi-Fi 或 HomeKit 配對。

1. 開啟上述手機設定網頁，在 **Remote buttons / Open the door** 按 **Add door button**。
2. 先放開遙控器按鍵至少 500 ms，再按一下要用來開門的按鍵。
3. 畫面顯示第一次按壓已擷取後，放開至少 500 ms，再按同一按鍵。需在啟動學習後 30 秒內完成。
4. 顯示 **Button learned and saved** 即完成，這兩次學習按壓不會開門。設定會存入 NVS，重開機仍保留。正常連線模式下若設定網頁到期，未完成的學習會取消並保留舊設定。
5. 再次放開後按該按鍵，會輸出設定時間的開門脈衝，預設 200 ms。開機後前 3 秒及脈衝結束後 2 秒內不接受開門。

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
firmware/OpenLatch/
  OpenLatch.ino       HomeSpan Switch、Wi-Fi 事件與 IR 接收
  Controller.h          與模擬器共用的控制狀態機
  TimedRelay.h          GPIO4 與獨立 esp_timer 關閉計時器
  Infrared.h            學習、按鍵比對、放開判定與逾時
  IrInput.h             支援協定與 IRremote 旗標轉換
  IrStore.h             NVS 單筆紀錄、版本與 CRC32 驗證
  SerialLine.h          有長度限制的非阻塞 USB 指令輸入
  DeviceSettings.h      Wi-Fi／登入密碼／配對碼與共用驗證
  SettingsStore.h       設定 NVS 紀錄與讀回驗證
  NfcAttemptLimiter.h    Home Key 連續錯誤、單次結果與暫停計時
  IndoorSession.h       門內 ESP-NOW APDU 交握及重送狀態機
  HomeKeyStore.h        門內憑證 NVS、CRC 與讀回驗證
  HomeKeyManager.h      ESP-NOW、獨立 Home Key 驗證工作及一次性結果
  HomeKeyServices.h     Apple Home NFC 憑證佈建、配對同步與鎖服務
  FirmwareInfo.h        韌體版本及 OTA 相容性標記
  FirmwareUpdate.h      串流檢查、長度／期限及映像識別
  EspFirmwareSink.h     寫入未使用分割區、驗證及開機切換
  partitions.csv        4 MB flash 的雙 OTA 分割區
  SetupPolicy.h         10 分鐘期限、重連與離線 AP 策略
  SetupPortal.h         密碼模式、設定網頁、DNS 與設定 API
  HttpRequestGuard.h    固定大小 HTTP 標頭、body 上限與讀取期限
  BoundedWebServer.h    每次最多讀取 512 bytes 的設定頁 HTTP 入口
  FirmwareBoot.h        新映像開機檢查與延後回滾確認
  SetupPage.h           自動產生、嵌入 flash 的網頁
  web/index.html        無外部相依的英文設定介面原始檔
  secrets.example.h     可選的出廠設定範本
sim/                   虛擬 GPIO、接點、計時器與互動介面
tests/                 C++ 行為測試、隨機事件與真實計時器驅動的 OS 模擬測試
tools/                 模擬、環境安裝、僅編譯韌體的工具
```

## 韌體編譯（仍不需要硬體）

固定相依版本：Arduino-ESP32 **3.3.0**、HomeSpan **2.1.8**、IRremote **4.4.1**。使用 ESP32-C3 generic board、4 MB flash、min_spiffs 雙 OTA 分割區；每個應用程式上限 **1,966,080 bytes**，NVS 位置保留。安裝資料都放在 `.tools/`。

```powershell
powershell -ExecutionPolicy Bypass -File tools/setup.ps1
powershell -ExecutionPolicy Bypass -File tools/build-firmware.ps1
```

目前不填任何密碼也能編譯。預設啟動設定 AP，由手機完成 Wi-Fi 及 IR 設定。編譯腳本會先執行 `tools/build_setup_page.py`，將 HTML 嵌入韌體。兩支 PowerShell 腳本均不燒錄、不尋找 COM port。

建置後另檢查實際分割區、晶片、應用程式 SHA-256、OTA 大小與 production/test 標記，並產生 `build/esp32c3/firmware.json`。網頁 OTA 選擇 `OpenLatch.ino.bin`；不要選 merged、bootloader、partitions 或 `-TestNfc` 測試映像。

Home Key 建置使用根目錄 [third_party](../../third_party/README.md) 的 DigitalDoorKey HomeKey 原始碼，不再依賴完整上游專案。TinyCBOR 0.6.0 公開標頭仍以固定校驗碼補足 Arduino SDK 缺漏，首次下載需要網路，之後使用本機快取；不修改 SDK。Windows 編譯暫用空閒磁碟代號縮短工具鏈路徑，結束後移除。`tools/build-firmware.ps1 -TestNfc` 另建完整 NFC 測試映像，使用公開測試金鑰，只供編譯驗證。

如果需要預填出廠設定，可複製 `secrets.example.h` 為同資料夾的 `secrets.h`；只在第一次沒有 NVS 設定時採用，之後以網頁儲存的設定為準。私密檔案已加入 `.gitignore`。Wi-Fi、設定密碼及配對資料會存進 NVS；這不是 flash 加密，請勿分享私有檔案、實機韌體或 flash 備份。

設定紀錄 v4 保留 v1–v3 的 Wi-Fi、配對碼、熱點密碼、脈衝及 NFC 限制；舊紀錄載入為免密碼管理模式，載入本身不回寫。HTTP 標頭上限 2,048 bytes，設定 body 上限 1,024 bytes；標頭及一般 body 各有 5 秒總期限、2 秒閒置期限。OTA body 使用分割區容量上限、15 秒閒置與 180 秒總期限。等待資料時會返回主迴圈。

`python tools/simulate.py test` 另包含真實 HTTP 入口與 NFC 管理／HomeKit 服務整合測試，使用本機傳輸及加密結果替身；不代表已通過真實 Apple 配對或無線驗收。

網頁瀏覽器驗證可另執行 `python tools/test_setup_page.py`（需已安裝 Chrome 及 `.tools/web-qa` 下的 Playwright）。此檢查使用本機模擬 API，不連實機；結果與限制見 [VALIDATION.md](VALIDATION.md)。

## 照片與實機邊界

- `IMG_0570`～`IMG_0572`：Songle SRD-05VDC-SL-C、5 V H/L 單路繼電器，預定 H 模式，GPIO4 HIGH 觸發。
- `IMG_0573`：1838／KY-022 類接收板，正面接腳朝下為 `-`、電源、`S`，預定 3.3 V 供電、GPIO3 訊號。
- `IMG_0601`～`IMG_0603`：ESP32-C3 SuperMini；`IMG_0602` 的商品圖視角與 `IMG_0603` 腳位圖不同，接線時認 GPIO 絲印，不只依左右位置。

虛擬接點的 OPEN／CLOSED 是理想模型。測試沒有驗證實際 HomeKit 加密配對、HomePod 遠端控制、IR 波形解碼、繼電器 3.3 V 觸發門檻、USB 5 V 路徑、開機毛刺、掉電或接點熔接。`esp_timer` 也是軟體計時器，不能保證整個 RTOS 停擺時的斷電時間。實機必須按原始文件 Stage A～E 分階段驗證；COM／NO 仍不得直接接入未確認的對講機端子。

實作 API 依據：[HomeSpan 2.1.8 原始碼](https://github.com/HomeSpan/HomeSpan/tree/2.1.8)、[HomeSpan API](https://github.com/HomeSpan/HomeSpan/blob/2.1.8/docs/Reference.md)、[IRremote 4.4.1](https://github.com/Arduino-IRremote/Arduino-IRremote/tree/v4.4.1)。
