# 虛擬驗證紀錄

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

操作手冊已重建為 10 頁，第 9 頁為英文 IR 設定說明，第 10 頁為手機網頁／AP 設定；已檢查頁面數、版面溢位與新增頁面渲染結果。

這些測試驗證程式邏輯與可控制的回呼順序，未模擬 CPU／ESP32 指令集、真實 RTOS 排程、HomeKit 網路協定、IR 波形或類比電路。接點狀態是理想模型；實際電氣與 Apple Home／HomePod 整合仍須按原始需求文件實測。
