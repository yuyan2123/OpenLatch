#pragma once
#include <WebServer.h>
#include <algorithm>
#include "HttpRequestGuard.h"

// Retain Arduino routing/response APIs, replacing its blocking body parser.
// Only the main loop owns this server and the controller; no extra task races.
class BoundedWebServer final : public WebServer {
 public:
  explicit BoundedWebServer(int port) : WebServer(port) {}
  bool listening() { return static_cast<bool>(_server); }
  void stop() { closeClient(true); WebServer::stop(); }
  void handleClient() override {
    if (!receiving_) {
      _currentClient = _server.accept();
      if (!_currentClient) return;
      receiving_ = true; bodySize_ = 0; rawStarted_ = false;
      guard_.begin(millis());
      _currentClient.setTimeout(100);
      _clearResponseHeaders(); _chunked = false; _contentLength = CONTENT_LENGTH_NOT_SET;
      _currentVersion = 1; _currentHandler = nullptr;
      for (auto* header = _currentHeaders; header; header = header->next) header->value = "";
      _currentRaw.reset();
    }
    if (!guard_.tick(millis())) { reject(guard_.error()); return; }
    if (!_currentClient.connected() && !_currentClient.available()) { closeClient(true); return; }
    // Yield after at most 512 incoming bytes, including during an OTA upload.
    size_t budget = 512;
    while (budget && _currentClient.available()) {
      if (!guard_.ready()) {
        const int byte = _currentClient.read();
        if (byte < 0) break;
        --budget;
        if (!guard_.headerByte(millis(), uint8_t(byte))) { reject(guard_.error()); return; }
        if (guard_.ready() && !prepare()) return;
      } else {
        const size_t size = std::min({budget, guard_.remaining(), size_t(_currentClient.available())});
        if (!size) break;
        uint8_t* output = guard_.raw() ? _currentRaw->buf : reinterpret_cast<uint8_t*>(body_.data() + bodySize_);
        const int count = _currentClient.read(output, size);
        if (count <= 0) break;
        budget -= size_t(count);
        if (!guard_.bodyBytes(millis(), size_t(count))) { reject(guard_.error()); return; }
        if (guard_.raw()) {
          _currentRaw->status = RAW_WRITE; _currentRaw->currentSize = size_t(count);
          _currentRaw->totalSize += size_t(count);
          _currentHandler->raw(*this, _currentUri, *_currentRaw);
          if (!_currentClient.connected()) { closeClient(true); return; }
        } else bodySize_ += size_t(count);
      }
      if (guard_.complete()) { dispatch(); return; }
    }
  }
 private:
  bool prepare() {
    _currentMethod = guard_.post() ? HTTP_POST : HTTP_GET;
    _currentUri = guard_.uri(); _hostHeader = guard_.value("Host");
    _clientContentLength = int(guard_.length());
    for (size_t i = 0; i < guard_.headerCount(); ++i) {
      const auto& header = guard_.header(i); _collectHeader(header.name, header.value);
    }
    for (auto* handler = _firstHandler; handler; handler = handler->next()) {
      if (handler->canHandle(*this, _currentMethod, _currentUri)) { _currentHandler = handler; break; }
    }
    if (guard_.raw()) {
      if (!_currentHandler || !_currentHandler->canRaw(*this, _currentUri)) { reject(404); return false; }
      _currentRaw.reset(new HTTPRaw()); _currentRaw->status = RAW_START;
      rawStarted_ = true;
      _currentHandler->raw(*this, _currentUri, *_currentRaw);
      if (!_currentClient.connected()) { closeClient(true); return false; }
    }
    return true;
  }
  void dispatch() {
    if (guard_.raw()) {
      _currentRaw->status = RAW_END;
      _currentHandler->raw(*this, _currentUri, *_currentRaw);
      rawStarted_ = false;
    } else {
      if (std::memchr(body_.data(), 0, bodySize_)) { reject(400); return; }
      body_[bodySize_] = 0;
      String arguments = guard_.query();
      if (bodySize_) { if (arguments.length()) arguments += '&'; arguments += body_.data(); }
      size_t fields = 1;
      for (size_t i = 0; i < arguments.length(); ++i) {
        if (!arguments[i] || (arguments[i] == '&' && ++fields > 24)) { reject(400); return; }
      }
      _parseArguments(arguments);
    }
    _handleRequest(); closeClient(false);
  }
  void reject(int code) {
    // Abort first, allowing the OTA owner to discard an incomplete flash write.
    abortRaw();
    sendHeader("Connection", "close"); sendHeader("Cache-Control", "no-store");
    send(code ? code : 400, "application/json", "{\"message\":\"Request exceeds the size or time limit, or has invalid HTTP framing.\"}");
    closeClient(false);
  }
  void abortRaw() {
    if (rawStarted_ && _currentRaw && _currentHandler) {
      _currentRaw->status = RAW_ABORTED;
      _currentHandler->raw(*this, _currentUri, *_currentRaw);
    }
    rawStarted_ = false;
  }
  void closeClient(bool abort) {
    if (abort) abortRaw();
    _currentClient.stop(); _currentRaw.reset(); _currentUri = "";
    _currentHandler = nullptr; receiving_ = false;
  }
  door::HttpRequestGuard guard_;
  std::array<char, door::HttpRequestGuard::kFormLimit + 1> body_{};
  size_t bodySize_ = 0;
  bool receiving_ = false, rawStarted_ = false;
};
