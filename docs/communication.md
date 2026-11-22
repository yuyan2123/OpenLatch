# 通訊規劃

門外優先透過同一區網的加密 Wi‑Fi UDP 建立連線。區網連線無法建立或已失效時，門外可透過 ESP‑NOW 重新交握。格式見 [OpenLatch NFC protocol 1](../projects/outdoor-reader/docs/protocol.md)，配置見 [Home Key](../projects/indoor-controller/docs/homekey_setup.md)。

- Wi‑Fi 透過區網探索門內板，ESP‑NOW 依門內所在頻道與配對 MAC 連線。
- PMK／LMK 與獨立 HMAC-SHA256 金鑰。
- nonce、seq、tap、request 隔離交握；重送僅回傳快取。
- 閒置時門內每 250 ms 傳送 Ping。若最近 350 ms 沒有新的有效門內指令，門外暫停 NFC 輪詢。
- 5 秒租約、5 秒感應期限、1500 ms 閒置期限；失效後重新交握。

HomeKey 憑證與授權只在門內。門外通訊金鑰被取得，也不能繞過使用者憑證驗證。

控制器接受經驗證的 HomeKit、已學習紅外線與門內 HomeKey 驗證成功的請求。HomeKey 結果只能使用一次、250 ms 內有效，仍受共用脈衝與冷卻限制；設定頁 token 不提供開門授權。
