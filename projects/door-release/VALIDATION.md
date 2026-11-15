# 虛擬驗證紀錄

## 2026-11-16 設定頁分頁（原始碼，未燒錄）

- 沿用原有顏色、字型與文案，改成 Wi-Fi、Infrared、RFID、Other settings 四個分頁；RFID 僅預留空白分頁，HomeKit 資訊移到 Other settings。
- 未保存表單切頁時提供 Save and restart、Discard、Cancel。取消保留原頁與輸入；捨棄還原欄位；保存失敗或欄位驗證失敗不切頁。紅外線維持立即保存。
- `python tools/test_setup_tabs.py`、`python tools/test_setup_page.py`、`python tools/test_setup_resilience.py` 與 `python tools/simulate.py test` 通過。涵蓋五種寬度、鍵盤、三種確認操作、錯誤處理與原本連線復原流程。
- 已重新產生 `SetupPage.h`，並檢查桌面與手機截圖。此為本機模擬 API 驗證，未燒錄或連線實機。

## 2026-11-14 目前原始碼與實機：1.2.3-web-recovery

- 修正前端一次狀態查詢失敗就設為停止的問題；短暫錯誤改為 1／2／4／8 秒退避自動重試。保留已輸入欄位，確認連線恢復後恢復操作；伺服器明確回覆的儲存／驗證錯誤仍顯示原訊息，不與斷線混淆。
- 倒數使用裝置回傳的毫秒剩餘時間及 `performance.now()` 在瀏覽器本機更新，每秒顯示下一秒，不依賴兩秒一次的狀態查詢。操作續時回覆會同步新的期限；微小網路抖動不讓數字倒退。背景更新依然不續時。
- 所有 API 請求排成一次一筆，請求逾時由 5 秒調整為 12 秒，涵蓋同步 WebServer 的等待時間；不自動重送儲存、學習或刪除操作。窗口確實關閉時停用修改，每五秒查詢是否被 IR 重新開放，開放後自動恢復原頁面與輸入。
- 設定窗口開放期間使用 `WIFI_PS_NONE`，結束時還原開放前的 Wi-Fi 省電值；路由測試涵蓋一般到期與 AP 救援時的切換及還原。
- `python tools/simulate.py test`、`python tools/test_setup_page.py`、新增的 `python tools/test_setup_resilience.py` 均通過。新測試注入 3.2 秒慢回應、503、破損 JSON、斷線，確認等待時倒數仍逐秒跳動、API 同時在途數最多 1、初次及中途失敗均自動恢復、未儲存文字保留、沒有自動重播設定寫入。
- 編譯使用 1,612,228 bytes 程式空間及 52,040 bytes 靜態 RAM。已燒錄至同一台 COM7，各映像 hash 校驗通過，未清除 NVS。USB 確認硬體／儲存正常，載入 9 組開門與 2 組設定按鍵；未發送開門指令或修改已學習按鍵。
- 修正前的實機抽測 8 次有 7 次逾時，另一次約 4.1 秒；此次重啟並更新後，連續 20 次狀態查詢全數成功，平均 33 ms、最大 50 ms，RSSI -54 dBm。結果只代表當次區網環境，不是長期可靠度測試。紀錄：`build/firmware-http-1.2.3-web-recovery.json`。
- 實機 Chrome 12 秒觀察倒數從 8:32 逐秒至 8:20，未跳秒；採樣間隔約 0.77 至 1.22 秒，6 次背景查詢均 HTTP 200，無 JavaScript 錯誤。頁面與嵌入建置逐位元組相同。紀錄：`build/firmware-browser-1.2.3-web-recovery.json`、`build/firmware-upload-1.2.3-web-recovery.log`、`build/firmware-boot-1.2.3-web-recovery.log`。

## 2026-11-14 上一版原始碼與實機：1.2.2-multi-ir

- 開門與開啟設定頁各支援 16 個固定槽位，新增不覆蓋，取消／逾時保留所有既有按鍵。重複學習不寫入、不占新槽位；用途衝突檢查涵蓋整份清單。
- 新 NVS v2 格式為 136-byte CRC32 blob，保留原 `door_ir`／`door_setup_ir` 命名空間與 `key` 名稱。v1 單鍵資料讀入槽位 0，讀取時不寫入；後續成功新增、移除或清除才原子升級。任一清單寫入驗證失敗時停用該清單。
- 新增 `/api/ir/remove` 與 `/api/setup-ir/remove`，需 POST、token 及現有來源／窗口檢查。ID 範圍與內容驗證、比對按鍵 fingerprint，拒絕過期頁面刪除被重用槽位中的新按鍵。保留原 forget 路由作為各用途的全部清除與修復。
- 英文頁面顯示清單與容量，提供 Add door button、Add setup button、每筆 Remove 及分開的全部清除。新增與刪除互不影響另一用途；學習期間停用相關變更與繼電器動作，清單滿載時提示先移除。
- `python tools/simulate.py test` 通過，包含新增的 16 組比對、容量限制、重複學習、刪除／槽位重用、重啟持久化、v1 升級、兩用途資料隔離、刪除寫入失敗及路由驗證。原控制器、200 ms 計時器與 20,000 個隨機事件測試仍通過。
- Chrome QA 通過五種寬度；驗證多筆清單、逐筆移除只影響指定按鍵、16 筆滿載在 320 px 仍可操作，以及原續時、網路救援與存檔流程。多組截圖見 `build/setup-multi-desktop.png`、`build/setup-multi-mobile.png`。
- ESP32-C3 編譯成功，使用 1,608,990 bytes 程式空間、52,024 bytes 靜態 RAM。已寫入同一台 COM7，所有映像 hash 驗證通過。紀錄：`build/firmware-upload-1.2.2-multi-ir.log`。
- 升級前擷取的兩用途各 1 組按鍵，在重啟後均與原識別值一致；USB 確認 `door=1/16 setup=1/16`、GPIO4 功能 1／驅動 3／硬體與儲存正常。HTTP 200 頁面與本次嵌入建置逐位元組相同，兩清單容量均為 16。紀錄：`build/firmware-boot-1.2.2-multi-ir.log`、`build/firmware-http-1.2.2-multi-ir.json`。未發送開門指令，也未更動實機已學習按鍵。

## 2026-11-14 上一版原始碼與實機：1.2.1-ir-setup

- 保留開機 10 分鐘，新增獨立紅外線設定按鍵，可重新開放 10 分鐘；有效的 POST 操作及 `/api/activity` 續時，GET 狀態查詢與遭拒請求不續時。閒置到期後活動請求不能重新開放，須用設定按鍵或重開機。
- `door_ir` 原開門按鍵資料不變，新增 `door_setup_ir` 命名空間。兩組各自學習、取消、清除並讀回驗證；禁止學成同一按鍵。任一學習期間抑制 HomeKit 與 IR 開門，設定訊號不產生繼電器動作。
- 依要求將 Digest 驗證整段保留於註解；Host、POST token、Origin、大小限制與設定窗口保留。AP 密碼仍生效。
- Hallmark 重新設計全英文頁面，使用系統字型與本機 CSS token；嵌入時展開 CSS，不增加裝置外部資源請求。桌面雙欄、手機單欄及段落捷徑，分開顯示門與設定按鍵。清除需確認；斷網／到期提供 Retry，重開窗口後可恢復，保留未儲存欄位。
- 網頁只在可信任的操作事件後送出續時，每 15 秒最多一次；狀態更新每 2 秒一次，背景分頁暫停查詢。背景更新不產生活動續時。
- `python tools/simulate.py test` 通過，含原有 20,000 個控制器隨機事件、GPIO／200 ms 計時器、IR、NVS，以及新增的窗口續時／溢位、雙按鍵、重複訊號、用途衝突、設定頁重開、無登入與請求防護測試。
- Chrome QA 通過 320、390、768、1280、1920 px，測試無憑證載入、背景不續時、實際輸入續時、兩種 IR 學習與清除、設定存檔失敗與重啟、到期及重開。截圖見 `build/setup-desktop.png`、`build/setup-mobile.png`；設計比較見 `.hallmark/compare.html`。
- ESP32-C3 編譯使用 1,600,334 bytes 程式空間與 51,784 bytes 靜態 RAM。依使用者指示，已將同一建置寫入 COM7（115200），各映像 hash 校驗通過並自動重啟；未清除 NVS。燒錄紀錄：`build/firmware-upload-1.2.1-ir-setup.log`。
- USB `ir status` 確認 GPIO4 功能 1、輸出啟用、驅動 3，以及 `hardware=ready storage=ready learned=yes enabled=yes`；原開門按鍵保留。開機訊息：`build/firmware-boot-1.2.1-ir-setup.log`。既有的 core-dump 校驗警告在舊版啟動紀錄也存在，本次正常啟動並提供網頁。
- 實機 `/` 及 `/api/status` 無憑證 HTTP 200；頁面與 `SetupPage.h` 嵌入內容逐位元組相同。Wi-Fi、硬體、設定儲存及兩組 IR 儲存皆正常，原開門按鍵啟用，新設定按鍵尚未學習。結果見 `build/firmware-http-1.2.1-ir-setup.json`。本次未發送開門指令，也未替使用者學習或清除按鍵。

最新實機操作請見文末 2026-11-14 各版本與回退紀錄；本檔開頭保留最初的模擬驗證歷史。

本次修改前，實機與主程式版本為 **1.2.0-gpiofix-200ms**：保留原版介面與登入，GPIO4 功能 1、驅動強度 3、脈衝 200 ms。當時已將實測版本同步至 `firmware/DoorRelease`；目前原始碼與實機則為上節的 1.2.1-ir-setup。歷次診斷草稿另存於本機 `.tools/diagnostic-work-before-stable-20261114.patch`，不納入部署。以下保留各次實驗的歷史紀錄，非現行功能清單。

日期：2026-11-13，韌體 1.2.0。環境：Windows、Python 3.13、Zig 0.14.1（C++17，`-Wall -Wextra -Werror`）。未連接 ESP32，未燒錄韌體。

執行 `python tools/simulate.py test` 通過：

1. 啟動 LOW、離線／未授權拒絕、啟動保護時間邊界。
2. 400 ms 脈衝、期間連按抑制、停止後 2000 ms 冷卻邊界。
3. 主迴圈停頓 10000 ms，虛擬獨立計時器仍在 400 ms 關閉。
4. Wi-Fi／已驗證 session 中斷立即停止 HomeKit 發起的脈衝，重連不執行舊指令。
5. 脈衝期間重啟、故障鎖定、重新啟動後恢復。
6. 未比對已學習按鍵的紅外線請求不觸發；未授權 OFF 不修改狀態，已授權 OFF 立即停止。
7. 零／超過 800 ms 的設定拒絕；驅動啟動失敗保持 OFF。
8. 32 位元 `millis()` 溢位時的啟動保護、脈衝與冷卻。
9. 固定種子 20261112 的 20,000 次隨機事件，在未授權 IR 的情境檢查離線無 HIGH、所有脈衝不超過 400 ms。
10. 已比對的 IR 可離線開門；網路狀態同步不會中止 IR 脈衝。HomeKit 與 IR 共用 busy／cooldown，已授權 OFF 可中止任一來源，學習期間兩者均被抑制。

另外將韌體原本的 `TimedRelay.h` 直接編譯到桌面測試，替換 GPIO／OS 計時器 API，驗證：預設禁止動作、脈衝上下限、400000 μs 計時器參數、計時器獨立關閉、斷線撤銷、計時器建立／啟動失敗、計時器在 HIGH 前就逾時，以及同一時間窗發生 Wi-Fi 斷線。全部通過。

新增來源權限測試：IR 權限不會放行 HomeKit；撤銷 HomeKit 權限不會中止 IR；撤銷 IR 權限不會中止 HomeKit；IR 在計時器啟動至 GPIO HIGH 的時間窗內遭撤權時，不會誤輸出 HIGH。

## 紅外線學習與儲存驗證

`infrared_tests.cpp` 直接使用韌體的 `Infrared.h`、`IrStore.h`、`IrInput.h` 與 `SerialLine.h`；NVS API 與解碼資料由受控替身提供。全部通過：

- 預設無按鍵；兩次相同、間隔至少 500 ms 的按下才儲存；學習確認不開門。
- 不同按鍵提示不符；協定、位址、按鍵碼及位元數均參與比對。
- 學習取消、30 秒逾時保留舊按鍵；學習及放開時間計算可跨 `millis()` 溢位。
- 存入後模擬重開機載入；清除後重開機仍停用；損壞的 IR 紀錄可單獨清除修復。
- 重複碼、自動重複碼、未標記 repeat 的連續完整訊號不會在冷卻後再次開門；雜訊也會重設放開間隔。
- 不支援協定、overflow、parity error、額外身分欄位均不放行；RC5／RC6 toggle bit 不改變按鍵身分。
- NVS 開啟、讀取、寫入、commit 或讀回驗證失敗時停用 IR。紀錄每個位元組受損、長度不足或超長均拒絕載入。
- USB 指令支援 CR／LF／CRLF，過長或含非文字位元組的整行拒絕，下一行可正常處理。
- 已學習按鍵接入共用控制器：學習中 HomeKit 被抑制、長按不重開、IR 與 HomeKit 互相受 busy／cooldown 限制、離線 IR 脈衝正常關閉。

NVS 替身不模擬 flash wear、真實掉電或 ESP-IDF 的內部寫入步驟；IR 解碼替身檢查應用程式的欄位轉換與旗標，未測真實波形。

`demo` 與 `interactive` 均已執行。HomeKit 示範軌跡為 3000 ms HIGH、3400 ms LOW；新增離線 IR 學習後軌跡為 13000 ms HIGH、13400 ms LOW。互動流程也確認重啟保留、清除後拒絕、HomeKit 脈衝中 IR 回覆 busy。顯示 `door_state=UNKNOWN`。

## 網頁設定、AP 與瀏覽器驗證

`python tools/simulate.py test` 的新增設定與路由測試全部通過：

- 無 Wi-Fi 設定立即提供 AP；有設定時先嘗試 30 秒，失敗開 AP，每 30 秒重試。
- 正常連線時設定介面在開機 10 分鐘截止；請求跨過截止瞬間，即使尚未執行下一次主迴圈更新也拒絕修改。
- 持續離線超過 10 分鐘，AP 與設定介面仍開啟；連線成功後關閉 AP，若已逾期亦關閉網頁。後續斷線 30 秒可再開 AP。
- 離線復原跨完整 `millis()` 溢位仍保持開放；正常期限到期後不因計時器溢位重開。
- SSID 長度、密碼長度與字元、64 位十六進位 PSK、明確選擇開放網路、更換 SSID 不沿用舊密碼、空白欄位保留原設定。
- 真正的 `SettingsStore.h` 對缺少、錯誤長度、每個位元組損壞、開啟／讀取／寫入／commit／讀回驗證失敗的處理。
- 真正的 `SetupPortal.h` 路由：登入檢查與 Digest challenge、每次啟動 token、Origin／Host 檢查、過大請求拒絕、GET 不執行設定動作、狀態不回傳密碼。
- 網頁啟動 IR 學習會停止目前脈衝，兩次確認不開門；取消、清除與到期停止學習；設定存檔失敗不排定重啟，成功後抑制開門且等待 1 秒重啟。

路由測試替換 Wi-Fi、HTTP、DNS、NVS 與 GPIO 介面；不驗證 ESP32 的實際 TCP／認證實作或射頻行為。

`python tools/test_setup_page.py` 使用已安裝的 Chrome 與 QA 專用 Playwright，在本機 Digest-auth 模擬 API 上通過：

- 登入、配對碼顯示、桌面 1280 px 與手機 390 px 版面無橫向溢位。
- 背景更新不覆寫使用者正在修改的 SSID；IR 學習、取消、完成狀態及清除。
- 離線且期限已過仍可設定；儲存失敗提示並恢復控制項；儲存成功清除密碼欄位並顯示重啟／重連指引。
- 到期顯示關閉原因並停用控制項；無 JavaScript 執行錯誤。

截圖已檢視：`build/setup-desktop.png`、`build/setup-mobile.png`。瀏覽器測試使用模擬 API，不代表已對 ESP32 網頁或 Apple Home 實測。

## ESP32-C3 編譯檢查

使用 Arduino CLI 1.6.0、Arduino-ESP32 3.3.0、HomeSpan 2.1.8、IRremote 4.4.1，目標 `esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app,FlashSize=4M`。

- 未填私密設定的原始 sketch 編譯成功。
- 另將 sketch 複製到 `build/configured-check/`，只在副本加入虛構的 Wi-Fi／配對資料，確認完整設定分支可編譯連結。此副本和生成的二進位僅供編譯檢查，不能當作實機部署設定。
- 未填私密設定版本使用 1,592,104 bytes（50%）程式空間及 51,736 bytes（15%）靜態 RAM；3 MB 應用分割區足夠。
- 預填虛構出廠 Wi-Fi／配對碼／設定密碼的副本亦編譯成功；兩種版本均含網頁、IR 與 HomeKit 的完整路徑。
- 編譯紀錄：`build/firmware-build.log`、`build/firmware-configured-check.log`。

上述建置未呼叫 upload，原始韌體資料夾沒有建立 `secrets.h`。

## 2026-11-14 實機燒錄與 GPIO4 初始化修正（1.2.1）

- 實機確認為 ESP32-C3 revision v0.4、內建 4 MB flash，USB 連接埠 COM7。使用原始 sketch 建置並寫入，未使用 `build/configured-check` 的虛構設定副本。
- 使用者回報繼電器指示燈隨按鍵閃動但無吸合聲；USB 曾回報 IR 請求 `accepted`。單獨以 5 V、3.3 V 觸發的測試均由使用者回報有聲，尚待接回 GPIO4 後確認整體動作。
- `TimedRelay::begin()` 改用 `gpio_config()`，明確選擇 GPIO 功能、推挽輸出、停用上拉／中斷並啟用下拉；在啟用輸出前先預載 LOW。GPIO 初始化失敗時不啟動繼電器。
- 原始 ESP-IDF 5.5 實作的 `gpio_set_direction()` 不切換 IO_MUX 功能；`gpio_config()` 會切換。這項缺口已修正，但仍不能據此斷定先前無聲的實際原因。
- GPIO 替身新增腳位功能選擇與輸出鎖存狀態；原版本在新模型下無法輸出 HIGH，修正後通過。新增預載 HIGH 時開機仍無 HIGH、GPIO 設定／預載失敗時兩個來源均不能輸出的檢查。
- `python tools/simulate.py test` 全部通過；實機編譯使用 1,591,992 bytes 程式空間與 51,736 bytes 靜態 RAM。
- 1.2.1 已使用 `UploadSpeed=115200` 重新寫入 COM7，各映像 hash 校驗通過。開機 USB 回報 `hardware=ready storage=ready learned=yes enabled=yes`，已學習的 NEC 按鍵保留。
- 上述 GPIO 替身不模擬實際電壓或接線。GPIO4 接回後的繼電器吸合、接點及脈衝寬度仍待現場確認。

## 2026-11-14 紅外線學習進度與收訊測試（1.3.0）

- 紅外線介面改為繁體中文，直接顯示第一次按鍵、第二次確認、儲存成功，以及取消、逾時、儲存錯誤；學習倒數 30 秒。不同按鍵或無法辨識的訊號不會推進學習步驟，成功結果保留至下一次操作。
- 新增 60 秒收訊測試；進入時停止目前脈衝並抑制 IR／HomeKit，回報按鍵比對、未知／錯誤訊號、訊號數及最後收到的按鍵資料。測試不寫入按鍵、不觸發繼電器；可手動結束，逾時或設定頁關閉也會結束。退出後仍需 500 ms 無訊號，避免長按立即開門。
- `python tools/simulate.py test` 全部通過，涵蓋學習狀態、診斷、重複訊號保留結果、測試期間兩個來源均受抑制、儲存不變、逾時跨溢位、結束後放開間隔，以及新路由的登入／token／方法限制。
- Chrome 瀏覽器 QA 通過手機／桌面版面、步驟變化、未支援訊號、按鍵不符、成功、保留舊按鍵的逾時、測試比對與結束、存檔與設定頁到期。已檢視 `build/setup-ir-learning.png`、`build/setup-ir-test-mobile.png`、`build/setup-ir-test-desktop.png`。
- 最終 ESP32-C3 編譯使用 1,605,380 bytes 程式空間與 51,752 bytes 靜態 RAM。已寫入 COM7，hash 校驗通過；紀錄為 `build/firmware-upload-1.3.0.log`。
- 實機 `device-lan-ip` 的已驗證 HTTP API 確認連線、硬體、IR 儲存正常，已學習 NEC `[redacted]` 保留。GET 確認提供新介面；POST 測試開始／結束確認 testing、倒數、IR 停用／恢復與原按鍵不變，未送出繼電器開啟請求。
- Chrome 直接連線實機亦確認已儲存按鍵正確顯示、測試按鈕可用、手機無橫向溢位且無 JavaScript 錯誤；IR 區塊截圖為 `build/setup-ir-device.png`。
- 實體繼電器在接回 GPIO4 後的吸合與接點仍待現場確認；收訊測試只驗證接收與比對。

## 2026-11-14 NEC 重複碼顯示修正（1.3.1）

- 實機測試頁回報 `ir_signal=unsupported`，但最後資料為 NEC `[redacted]`、0 bits；USB 同時記錄相同按鍵完整的 32-bit 有效訊號。
- 核對已安裝的 IRremote 4.4.1 `ir_NEC.hpp`：短重複碼設定 repeat 旗標並沿用協定、位址與按鍵碼，不填完整資料位元數。舊順序先驗證完整按鍵，因此把 0-bit 重複碼誤判為不支援格式。
- 改為先處理重複碼，再驗證完整按鍵；重複尾碼保留先前完整按鍵的比對與資料。單獨收到重複碼只提示放開按鍵；損壞及真正未知的非重複碼仍拒絕，長按不推進學習或再次開門。
- 新測試以實際 `IRData` 欄位形狀重現問題，修正前失敗、修正後全部通過，涵蓋測試比對、學習第一／二步、不同按鍵、損壞訊號與未知格式。`python tools/simulate.py test` 全部通過。
- 編譯通過並寫入 COM7，hash 校驗通過；紀錄為 `build/firmware-upload-1.3.1.log`。重啟後實機 HTTP 與 USB 確認硬體／儲存正常，原 NEC 按鍵保留。

## 2026-11-14 暫停網頁登入（1.3.2）

- 依使用者要求，將 `SetupPortal::authorize()` 內的帳號密碼驗證／Digest challenge 整段註解，保留原碼供恢復。設定頁與狀態 API 可直接存取；Host、設定期限、POST token／Origin／大小檢查保留。設定熱點仍使用原本的 AP 密碼。
- 網頁同步標示登入暫停，密碼欄位說明改為設定 AP 密碼，避免繼續顯示網頁登入指引。
- 更新原有路由測試：無認證可讀頁面／狀態，未帶 token 的修改仍拒絕；其餘控制器、IR、NVS、設定政策測試全數通過。Chrome QA 改以無帳號密碼的新工作階段執行，頁面與操作流程通過。
- 編譯使用 1,599,046 bytes 程式空間與 51,752 bytes 靜態 RAM。1.3.2 已寫入 COM7，hash 校驗通過；紀錄為 `build/firmware-upload-1.3.2.log`。
- 實機 `device-lan-ip` 的頁面與 `/api/status` 皆在未提供帳號密碼時回傳 HTTP 200，未帶登入 challenge；全新 Chrome 工作階段可直接載入並啟用 IR 控制項，無 JavaScript 錯誤。實機回報連線、硬體與 IR 儲存正常，已學習按鍵仍保留。

## 2026-11-14 實機 GPIO4 直接脈衝診斷（1.3.3）

- 使用者明確要求直接測試繼電器。新增 USB `relay status` 與單次 `relay pulse`，後者透過原控制器及 IR 來源權限送出 400 ms 脈衝，保留開機保護、冷卻、故障／學習／測試抑制及獨立關閉計時器。未新增 HTTP 開門路由。
- GPIO4 同時啟用數位輸入以回讀腳位，保留推挽輸出；記錄最近脈衝的寫入錯誤、回讀 HIGH、關閉前位準、微秒時間與停止原因。
- 新測試涵蓋診斷指令對應控制器入口的保護限制、400 ms 時間記錄、腳位受外部負載拉低時不誤報 HIGH，以及輸出寫入失敗時拒絕啟動。`python tools/simulate.py test` 全部通過。
- ESP32-C3 編譯使用 1,600,738 bytes 程式空間與 51,800 bytes 靜態 RAM。已寫入 COM7，hash 校驗通過，紀錄為 `build/firmware-upload-1.3.3.log`。
- 實機 USB 單次診斷：GPIO4 初始回讀 LOW；脈衝開始及約 150／300 ms 時均為 HIGH；結束後 LOW；記錄 `duration_us=399930 write_error=0 before_low=1`，未見權限提前關閉。
- 實機 IO 設定為 input=1、output=1、function=1、signal_out=128、open_drain=0、drive=1。紀錄為 `build/relay-diagnostic-1.3.3.log`。
- 以上確認晶片腳位的數位位準及韌體脈衝時間，不能據此確認腳位電壓、外部 IN 接線、機械吸合或接點導通；實際喀聲待使用者回報。
- 依使用者要求重試三次，每次 400 ms、相隔 3 秒，測得 400047／399930／399931 us，均回讀 HIGH 後回 LOW、無寫入錯誤。使用者明確回報三次均無喀聲。重試紀錄為 `build/relay-diagnostic-repeat-1.3.3.log`；因此尚不能宣稱實體繼電器可運作。

## 2026-11-14 GPIO 驅動強度比較（1.3.4）

- 新增僅限 USB 的 `relay drive 1`／`relay drive 3`，只在輸出停止時調整驅動能力，不自動送出脈衝。未更改啟動預設強度。
- 編譯使用 1,601,342 bytes 程式空間與 51,800 bytes 靜態 RAM，寫入 COM7 並通過 hash 校驗，紀錄為 `build/firmware-upload-1.3.4.log`。
- 驅動強度 3 的第一輪三次脈衝記錄為 399940／399935／399936 us，GPIO4 回讀 HIGH 後 LOW，無寫入錯誤；使用者回報無喀聲。之後相同設定重試三次，記錄 400001／399937／399937 us，使用者明確回報三次均有喀聲。
- 兩輪結束皆恢復強度 1 並確認輸出 LOW。紀錄為 `build/relay-diagnostic-strong-1.3.4.log` 與 `build/relay-diagnostic-strong-retry-1.3.4.log`。相同設定的回報先後不同，不能據此斷定提高驅動強度已解決原始故障。
- 使用者隨後明確要求回退最初燒入的完整 1.2.0；診斷修正版原始碼保留於工作目錄。

## 2026-11-14 完整回退 1.2.0 與單一變因對照

- 使用者明確選擇最初燒入的完整 1.2.0。以 `git archive` 取出提交 `9419cc8faf01182cd440645a1332ba3bd4a837e5` 的原始韌體至 `build/rollback-1.2.0-source`，工作目錄診斷修正版保留。
- 原始 1.2.0 編譯為 1,592,104 bytes 程式空間與 51,736 bytes 靜態 RAM，與最初建置一致。已寫入 COM7，hash 校驗通過，紀錄為 `build/firmware-rollback-1.2.0.log`。原版映像保留於 `build/rollback-1.2.0-images`。
- 原版 USB 確認硬體／IR 儲存正常，NEC `[redacted]` 按鍵保留；實際接收到按鍵並回報 accepted／ON。使用者明確回報原版無喀聲。
- 接續建立 `build/diagnostic-1.2.0-drive3` 對照副本：相較原始韌體，只新增 GPIO4 驅動強度 3 設定，以及版本識別 `1.2.0-d3`；介面、登入與控制行為均為原版。目的是隔離驅動強度這一個變因。
- 對照版另加入只讀設定回報。實機回報 `function=0 output=1 drive=3`；SDK `io_mux_reg.h` 確認功能 0 為 MTMS、功能 1 才是 GPIO4。這證實原版 `gpio_set_direction()` 未把 IO_MUX 切到 GPIO 功能。
- 在原版介面基礎補上預載 LOW 與 `gpio_config()`，保留驅動強度 3，識別為 `1.2.0-gpiofix`。實機回報 `function=1 output=1 drive=3`，使用者明確回報遙控器三次均有喀聲。紀錄為 `build/relay-1.2.0-gpiofix-status.log` 與 `build/relay-1.2.0-gpiofix-remote-test.log`。

## 2026-11-14 脈衝改為 200 ms

- 依使用者要求，將已可吸合版本的 IR／HomeKit 共用預設脈衝由 400 改為 200 ms，保留 GPIO 初始化修正、驅動強度 3、原版介面與 2 秒冷卻；版本為 `1.2.0-gpiofix-200ms`。頁面顯示同步改為 200 ms。
- 原始碼工作目錄的共用預設與 USB 診斷時間說明也同步更新為 200 ms；控制器邊界、跨溢位與隨機事件測試通過。
- `build/active-pulse-200ms-test.cpp` 直接編譯實機版本的 Controller／TimedRelay，確認 IR 與 HomeKit 均要求 200000 us 計時，199 ms 尚未關閉，200 ms 計時到期後關閉。
- 編譯使用 1,592,884 bytes 程式空間、51,736 bytes 靜態 RAM。已寫入 COM7 並通過 hash 校驗，紀錄為 `build/firmware-1.2.0-gpiofix-200ms.log`。開機確認 GPIO4 功能 1、驅動 3、硬體與儲存正常，原按鍵保留並收到 accepted／ON 的遙控器請求。
- 最終二進位保存於 `build/firmware-1.2.0-gpiofix-200ms`，原始碼保存於 `build/DoorRelease-1.2.0-gpiofix-200ms-source.zip`。200 ms 為實作與計時器替身驗證結果，未以示波器測量接點時間。
- 推送前已將穩定版同步回 `firmware/DoorRelease`。現行測試驗證 GPIO 功能選擇、開機預載 LOW、驅動強度 3、初始化失敗拒絕啟動，以及 IR／HomeKit 200 ms 時序；完整原生測試、原版 Digest 網頁 Chrome QA、標準建置腳本皆通過，建置用量與實機版本一致。公開紀錄已省略實際按鍵識別與區網 IP。

先前版本的操作手冊已重建為 10 頁，第 9 頁為英文 IR 設定說明，第 10 頁為手機網頁／AP 設定；已檢查頁面數、版面溢位與新增頁面渲染結果。PDF 尚未納入 1.3.0 的進度與測試介面，最新操作請見 `docs/infrared_setup.md`。

這些測試驗證程式邏輯與可控制的回呼順序，未模擬 CPU／ESP32 指令集、真實 RTOS 排程、HomeKit 網路協定、IR 波形或類比電路。接點狀態是理想模型；實際電氣與 Apple Home／HomePod 整合仍須按原始需求文件實測。
