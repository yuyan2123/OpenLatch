#pragma once
#include <functional>
#include <map>
#include "Arduino.h"
enum HTTPMethod { HTTP_GET, HTTP_POST, HTTP_OPTIONS };
enum HTTPAuthMethod { BASIC_AUTH, DIGEST_AUTH };
enum HTTPRawStatus { RAW_START, RAW_WRITE, RAW_END, RAW_ABORTED };
struct HTTPRaw { HTTPRawStatus status = RAW_START; size_t totalSize = 0, currentSize = 0; uint8_t buf[1436]{}; };
struct FakeClient { bool stopped = false; void stop() { stopped = true; } };
class WebServer;
inline WebServer* fakeWeb = nullptr;
class WebServer {
 public:
  explicit WebServer(int port) : port(port) { fakeWeb = this; }
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
  bool active = false, authenticated = false;
  void collectHeaders(const char*[], size_t) {}
  void on(const char* path, HTTPMethod method, std::function<void()> handler) { routes[{path,method}] = handler; }
  void on(const char* path, HTTPMethod method, std::function<void()> handler, std::function<void()> upload) {
    on(path, method, handler); rawRoutes[path] = upload;
  }
  HTTPRaw& raw() { return rawUpload; }
  FakeClient& client() { return connection; }
  void startRaw(const char* path, int size) {
    code = 0; body.clear(); outputHeaders.clear(); verb = HTTP_POST; length = size;
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
  void begin() { active = true; ++starts; }
  void stop() { active = false; ++stops; }
  void handleClient() {}
  void sendHeader(const char* name, const String& value) { outputHeaders[name] = value; }
  void send(int status, const char*, const String& content) { code = status; body = content; }
  void send_P(int status, const char* type, const char* content) { send(status, type, content); }
  HTTPMethod method() const { return verb; }
  String hostHeader() const { return host; }
  String header(const char* name) const {
    auto it = inputHeaders.find(name); return it == inputHeaders.end() ? String("") : it->second;
  }
  bool authenticate(const char* user, const char* password) const {
    return std::string(user) == "admin" && authenticated && suppliedPassword == password;
  }
  void requestAuthentication(HTTPAuthMethod method, const char*, const String& message) {
    challenge = method; send(401, "text/plain", message);
  }
  int clientContentLength() const { return length; }
  bool hasArg(const char* name) const { return arguments.count(name); }
  String arg(const char* name) const { auto it = arguments.find(name); return it == arguments.end() ? String("") : it->second; }
  void request(const char* path, HTTPMethod method = HTTP_GET) {
    code = 0; body.clear(); outputHeaders.clear(); verb = method;
    const auto it = routes.find({path, method});
    if (it == routes.end()) notFound(); else it->second();
  }
};
