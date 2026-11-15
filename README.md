# OpenLatch

門禁控制專案。

| 專案 | 用途 | 狀態 |
| --- | --- | --- |
| [door-release](projects/door-release/README.md) | ESP32-C3、Apple Home 與紅外線開門控制器 | 現有韌體 `1.2.3-web-recovery` |
| [outdoor-reader](projects/outdoor-reader/README.md) | ESP-32S＋PN532 門外 NFC 轉送器 | 門外端已實作，待門內整合與實機測試 |

整體規劃見 [系統架構](docs/architecture.md) 與 [通訊規劃](docs/communication.md)。

## 現有控制器

從 repository 根目錄進入專案後執行工具：

```powershell
cd projects/door-release
python tools/simulate.py test
python tools/simulate.py demo
```

環境安裝、韌體編譯與操作方式見 [控制器說明](projects/door-release/README.md)。原根目錄的韌體、工具、測試、文件及照片均已移至 `projects/door-release/`。

每個專案各自管理建置設定、`build/` 與 `.tools/`；根目錄 `docs/` 放跨專案文件。

HomeKey-ESP32 及其相依以 Git submodule 管理。重新取得此 repository 後，從根目錄執行：

```powershell
git submodule update --init --recursive
```
