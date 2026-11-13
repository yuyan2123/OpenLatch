# 虛擬驗證紀錄

日期：2026-11-12。環境：Windows、Python 3.13、Zig 0.14.1（C++17，`-Wall -Wextra -Werror`）。未連接 ESP32，未燒錄韌體。

執行 `python tools/simulate.py test` 通過：

1. 啟動 LOW、離線／未授權拒絕、啟動保護時間邊界。
2. 400 ms 脈衝、期間連按抑制、停止後 2000 ms 冷卻邊界。
3. 主迴圈停頓 10000 ms，虛擬獨立計時器仍在 400 ms 關閉。
4. Wi-Fi／已驗證 session 中斷立即停止，重連不執行舊指令。
5. 脈衝期間重啟、故障鎖定、重新啟動後恢復。
6. 紅外線請求不觸發；未授權 OFF 不修改狀態，已授權 OFF 立即停止。
7. 零／超過 800 ms 的設定拒絕；驅動啟動失敗保持 OFF。
8. 32 位元 `millis()` 溢位時的啟動保護、脈衝與冷卻。
9. 固定種子 20261112 的 20,000 次隨機事件，檢查離線無 HIGH、所有脈衝不超過 400 ms。

另外將韌體原本的 `TimedRelay.h` 直接編譯到桌面測試，替換 GPIO／OS 計時器 API，驗證：預設禁止動作、脈衝上下限、400000 μs 計時器參數、計時器獨立關閉、斷線撤銷、計時器建立／啟動失敗、計時器在 HIGH 前就逾時，以及同一時間窗發生 Wi-Fi 斷線。全部通過。

`demo` 與 `interactive` 均已執行。示範脈衝軌跡：3000 ms HIGH、3400 ms LOW；顯示 `door_state=UNKNOWN`。

## ESP32-C3 編譯檢查

使用 Arduino CLI 1.6.0、Arduino-ESP32 3.3.0、HomeSpan 2.1.8、IRremote 4.4.1，目標 `esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app,FlashSize=4M`。

- 未填私密設定的原始 sketch 編譯成功。
- 另將 sketch 複製到 `build/configured-check/`，只在副本加入虛構的 Wi-Fi／配對資料，確認完整設定分支可編譯連結。此副本和生成的二進位僅供編譯檢查，不能當作實機部署設定。
- 完整設定版本使用 1,528,934 bytes（48%）程式空間及 50,832 bytes（15%）靜態 RAM；3 MB 應用分割區足夠。
- 編譯紀錄：`build/firmware-build.log`、`build/firmware-configured-check.log`。

上述建置未呼叫 upload，原始韌體資料夾沒有建立 `secrets.h`。

這些測試驗證程式邏輯與可控制的回呼順序，未模擬 CPU／ESP32 指令集、真實 RTOS 排程、HomeKit 網路協定、IR 波形或類比電路。接點狀態是理想模型；實際電氣與 Apple Home／HomePod 整合仍須按原始需求文件實測。
