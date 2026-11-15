# ESP-32S 與 PN532

ESP32-WROOM-32／ESP-32S、4 MB Flash，PN532 使用 SPI。GPIO18=SCK、19=MISO、23=MOSI、21=SS，不占用 ESP32 開機 strapping pin。設定在 `firmware/sdkconfig.defaults`。

兩者共地。ESP32 GPIO 使用 3.3 V；PN532 電源依模組標示，支援 3.3 V 才接 3V3。SPI 模式開關依實際模組手冊設定。IRQ 不需接。

門外僅有電源及 NFC 模組。繼電器、原按鈕接點與門鎖控制線均在門內。

## 實機驗證

1. 建置未配置版本，燒錄後用 115200 baud 讀取 station MAC；此版本不轉送 NFC。
2. 以實際兩端 MAC 產生金鑰，將同一配對資料配置到兩端。
3. 建置已配置版本。門內測試端完成 Hello／Configure，確認門外出現 `Reader ready`。
4. 不接開門動作，先測 Tag、移開／重新感應、Release、逾時及 APDU 往返，也要測手機持續停留時的行為。
5. 門內接入 DigitalDoorKey 與憑證佈建後，測 iPhone／Apple Watch 驗證，量測每個 APDU 及完整 session 時間。
6. 測讀取器拔除、兩端重啟、封包遺失與路由器換頻；舊 session 不得延續。
7. 最後由門內接入 Controller，回歸原 HomeKit／IR、200 ms 脈衝、冷卻及開機保護。

本次未接上這組硬體。編譯與本機測試不能取代射頻及手機／手錶驗證。
