# 通訊規劃

加密 ESP-NOW 單播的門內與門外端已實作，待實際 MAC 配置與雙板驗證。格式見 [OpenLatch NFC protocol 1](../projects/outdoor-reader/docs/protocol.md)，配置見 [Home Key](../projects/door-release/docs/homekey_setup.md)。

- 門外搜尋門內的 Wi-Fi 頻道，固定配對 station MAC。
- PMK／LMK 與獨立 HMAC-SHA256 金鑰。
- nonce、seq、tap、request 隔離交握；重送僅回傳快取。
- 5 秒租約、5 秒感應期限、1500 ms 閒置期限；失效後重新交握。

HomeKey 憑證與授權只在門內。門外通訊金鑰被取得，也不能繞過使用者憑證驗證。

控制器接受經驗證的 HomeKit、已學習紅外線與門內 HomeKey 驗證成功的請求。HomeKey 結果只能使用一次、250 ms 內有效，仍受共用脈衝與冷卻限制；設定頁 token 不提供開門授權。
