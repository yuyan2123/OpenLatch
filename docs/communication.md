# 通訊規劃

已選定加密 ESP-NOW 單播，門外端已實作。格式見 [OpenLatch NFC protocol 1](../projects/outdoor-reader/docs/protocol.md)，門內端尚未實作。

- 門外搜尋門內的 Wi-Fi 頻道，固定配對 station MAC。
- PMK／LMK 與獨立 HMAC-SHA256 金鑰。
- nonce、seq、tap、request 隔離交握；重送僅回傳快取。
- 5 秒租約、5 秒感應期限、1500 ms 閒置期限；失效後重新交握。

HomeKey 憑證與授權只在門內。門外通訊金鑰被取得，也不能繞過使用者憑證驗證。

現有控制器僅接受 HomeKit 與已學習紅外線的開門請求。新增來源需另行實作與驗證，不以設定頁 token 作為開門授權。
