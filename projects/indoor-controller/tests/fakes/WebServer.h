#pragma once
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include <deque>
#include <algorithm>
#include "Arduino.h"
#include "WiFi.h"

enum HTTPMethod { HTTP_GET, HTTP_POST, HTTP_OPTIONS };
enum HTTPAuthMethod { BASIC_AUTH, DIGEST_AUTH };
enum HTTPRawStatus { RAW_START, RAW_WRITE, RAW_END, RAW_ABORTED };
constexpr size_t CONTENT_LENGTH_NOT_SET = size_t(-1);
struct HTTPRaw { HTTPRawStatus status = RAW_START; size_t totalSize = 0, currentSize = 0; uint8_t buf[1436]{}; };
struct FakeSocket {
  std::string input;
  size_t cursor = 0;
  bool open = true;
  int timeout = 0, response = 0;
  IPAddress source{"192.168.1.23"}, destination{"192.168.1.42"};
};
struct FakeClient {
  std::shared_ptr<FakeSocket> socket;
  bool stopped = false;
  explicit operator bool() const { return socket && socket->open; }
  bool connected() const { return socket && socket->open; }
  int available() const { return socket ? int(socket->input.size() - socket->cursor) : 0; }
  int read() { return available() ? uint8_t(socket->input[socket->cursor++]) : -1; }
  int read(uint8_t* output, size_t size) {
    size = std::min(size, size_t(available()));
    if (size) { std::memcpy(output, socket->input.data() + socket->cursor, size); socket->cursor += size; }
    return int(size);
  }
  void setTimeout(int timeout) { if (socket) socket->timeout = timeout; }
  void stop() { stopped = true; if (socket) socket->open = false; }
  IPAddress remoteIP() const { return socket ? socket->source : IPAddress{}; }
  IPAddress localIP() const { return socket ? socket->destination : IPAddress{}; }
};
struct FakeNetworkServer {
  std::deque<std::shared_ptr<FakeSocket>> clients;
  bool listening = false;
  explicit operator bool() const { return listening; }
  FakeClient accept() { if (clients.empty()) return {}; auto socket = clients.front(); clients.pop_front(); return {socket, false}; }
};
class WebServer;
struct RequestHandler {
  std::string path; HTTPMethod method; std::function<void()> action, upload; RequestHandler* following = nullptr;
  RequestHandler* next() { return following; }
  bool canHandle(WebServer&, HTTPMethod input, const String& uri) { return input == method && uri == path; }
  bool canRaw(WebServer&, const String&) { return bool(upload); }
  void raw(WebServer&, const String&, HTTPRaw&) { upload(); }
};
inline WebServer* fakeWeb = nullptr;
class WebServer {
 public:
  explicit WebServer(int port) : port(port) { fakeWeb = this; }
  virtual ~WebServer() = default;
  std::map<std::pair<std::string,HTTPMethod>, std::function<void()>> routes;
  std::map<std::string, std::function<void()>> rawRoutes;
  HTTPRaw rawUpload;
  FakeClient connection;
  std::map<std::string,String> inputHeaders, arguments, outputHeaders;
  std::function<void()> notFound;
  int port = 0, code = 0, length = 0, starts = 0, stops = 0;
  String host = "192.168.4.1", body, suppliedPassword;
  HTTPMethod verb = HTTP_GET;
  HTTPAuthMethod challenge = BASIC_AUTH;
  bool active = false, authenticated = false, beginFails = false;
  void collectHeaders(const char*[], size_t) {}
  void on(const char* path, HTTPMethod method, std::function<void()> handler) { on(path, method, handler, {}); }
  void on(const char* path, HTTPMethod method, std::function<void()> handler, std::function<void()> upload) {
    routes[{path,method}] = handler;
    if (upload) rawRoutes[path] = upload;
    auto entry = std::make_unique<RequestHandler>(); entry->path = path; entry->method = method; entry->action = handler; entry->upload = upload;
    if (!handlers.empty()) handlers.back()->following = entry.get(); else _firstHandler = entry.get();
    handlers.push_back(std::move(entry));
  }
  HTTPRaw& raw() { return _currentRaw ? *_currentRaw : rawUpload; }
  FakeClient& client() { return _currentClient.socket ? _currentClient : connection; }
  void startRaw(const char* path, int size) {
    code = 0; body.clear(); outputHeaders.clear(); verb = HTTP_POST; length = size; _currentUri = path;
    connection.stopped = false; rawUpload = {}; rawRoutes.at(path)();
  }
  void writeRaw(const char* path, const uint8_t* data, size_t size) {
    rawUpload.status = RAW_WRITE; rawUpload.currentSize = size; rawUpload.totalSize += size;
    std::memcpy(rawUpload.buf, data, size); rawRoutes.at(path)();
  }
  void finishRaw(const char* path, bool abort = false) {
    rawUpload.status = abort ? RAW_ABORTED : RAW_END; rawRoutes.at(path)();
    if (!abort && !connection.stopped) routes.at({path, HTTP_POST})();
  }
  void onNotFound(std::function<void()> handler) { notFound = handler; }
  void begin() { _server.listening = active = !beginFails; ++starts; }
  void stop() { _server.listening = active = false; ++stops; }
  virtual void handleClient() {}
  std::shared_ptr<FakeSocket> enqueue(std::string input) {
    auto socket = std::make_shared<FakeSocket>(); socket->input = std::move(input); _server.clients.push_back(socket); return socket;
  }
  void sendHeader(const char* name, const String& value) { outputHeaders[name] = value; }
  void send(int status, const char*, const String& content) { code = status; body = content; if (_currentClient.socket) _currentClient.socket->response = status; }
  void send_P(int status, const char* type, const char* content) { send(status, type, content); }
  HTTPMethod method() const { return _currentClient.socket ? _currentMethod : verb; }
  String uri() const { return _currentUri; }
  String hostHeader() const { return _currentClient.socket ? _hostHeader : host; }
  String header(const char* name) const {
    String wanted(name); wanted.toLowerCase();
    for (const auto& field : inputHeaders) { String key(field.first); key.toLowerCase(); if (key == wanted) return field.second; }
    return "";
  }
  bool authenticate(const char* user, const char* password) const {
    return std::string(user) == "admin" && authenticated && suppliedPassword == password;
  }
  void requestAuthentication(HTTPAuthMethod method, const char*, const String& message) { challenge = method; send(401, "text/plain", message); }
  int clientContentLength() const { return _currentClient.socket ? _clientContentLength : length; }
  bool hasArg(const char* name) const { return arguments.count(name); }
  String arg(const char* name) const { auto it = arguments.find(name); return it == arguments.end() ? String("") : it->second; }
  void request(const char* path, HTTPMethod method = HTTP_GET) {
    code = 0; body.clear(); outputHeaders.clear(); verb = method; _currentUri = path;
    const auto it = routes.find({path, method});
    if (it == routes.end()) notFound(); else it->second();
  }
 protected:
  struct RequestArgument { RequestArgument* next = nullptr; String value; };
  RequestArgument* _currentHeaders = nullptr;
  FakeNetworkServer _server;
  FakeClient _currentClient;
  HTTPMethod _currentMethod = HTTP_GET;
  String _currentUri, _hostHeader;
  int _currentVersion = 1, _clientContentLength = 0;
  bool _chunked = false;
  size_t _contentLength = 0;
  RequestHandler* _currentHandler = nullptr;
  RequestHandler* _firstHandler = nullptr;
  std::unique_ptr<HTTPRaw> _currentRaw;
  std::vector<std::unique_ptr<RequestHandler>> handlers;
  void _clearResponseHeaders() { outputHeaders.clear(); inputHeaders.clear(); arguments.clear(); }
  void _collectHeader(const char* name, const char* value) { inputHeaders[name] = value; }
  void _parseArguments(const String& text) {
    arguments.clear(); size_t start = 0;
    const auto decode = [](std::string input) {
      std::string output;
      for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '+') output += ' ';
        else if (input[i] == '%' && i + 2 < input.size()) { output += char(std::strtoul(input.substr(i + 1, 2).c_str(), nullptr, 16)); i += 2; }
        else output += input[i];
      }
      return output;
    };
    while (start < text.size()) {
      size_t end = text.find('&', start); if (end == std::string::npos) end = text.size();
      auto field = text.substr(start, end - start); size_t equal = field.find('=');
      if (equal != std::string::npos) arguments[decode(field.substr(0, equal))] = decode(field.substr(equal + 1));
      start = end + 1;
    }
  }
  void _handleRequest() {
    if (_currentHandler) _currentHandler->action(); else if (notFound) notFound();
  }
};
