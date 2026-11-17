# 實作分工

## 門外端

ESP-32S／PN532 SPI 韌體使用根目錄 `third_party/` 的 `pn532_cxx`、`pn532_hal`、`loggable` 與 ECP 探測方式。提供固定配對、加密 ESP-NOW、HMAC、頻道搜尋、APDU 轉送、重送快取與期限控制。

缺少配置、板子 MAC 不符、PN532 初始化失敗時不開始轉送。僅保留必要的獨立元件原始碼；驗證私鑰與鎖服務不帶到門外。

## 門內端

以下門內功能已加入 `door-release`，原生測試與韌體建置可驗證程式；雙板配置及實機驗收仍待完成：

1. ESP-NOW 接收器與既有 Wi-Fi／AP 復原共存，實作 `protocol.md` 的交握與重送規則。
2. DigitalDoorKey 的 `ApduChannel` 接到 Exchange／Response；門內建立、驗證每個 session。
3. 在門內提供 HomeKit NFC 服務並管理 reader identity、issuer、endpoint；原瞬時 Switch 不會自行提供 NFC 憑證佈建。
4. 已授權 NFC 入口使用獨立權限，沿用 Controller 的自訂脈衝（預設 200 ms）、2 秒冷卻、3 秒開機保護與故障抑制，不冒用 IR 權限。
5. 同一成功驗證最多開門一次。舊回覆、偽造 Tag、門外自報成功或未完成驗證均不能開門。
6. 連續驗證錯誤預設 5 次鎖定 NFC 60 秒；重送不重計，通訊故障不算憑證錯誤。配置見 [Home Key](../../door-release/docs/homekey_setup.md)。

## 驗收

先驗證無鎖的 APDU 往返，再測真實 HomeKey 配對及驗證，最後接入開門。必須保留既有 HomeKit／IR，門外失效不應影響原室內操作。

分拆後增加多次無線往返。ESP-NOW／PN532 延遲與手機逾時相容性，需要實機量測才能確認。
