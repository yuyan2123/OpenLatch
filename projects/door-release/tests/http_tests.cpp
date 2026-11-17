#include "PortalFixture.h"
#include "../firmware/DoorRelease/FirmwareBoot.h"
using Guard = door::HttpRequestGuard;

std::string header(const std::string& path, size_t size, const std::string& type = "application/x-www-form-urlencoded", const std::string& token = "") {
  return "POST " + path + " HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Type: " + type +
      "\r\nX-Setup-Token: " + token + "\r\nContent-Length: " + std::to_string(size) + "\r\n\r\n";
}
void bounded_headers_and_framing() {
  for (const auto& sample : std::vector<std::pair<std::string,int>>{
      {header("/api/settings", 1025), 413}, {header("/api/firmware", Guard::kOtaLimit + 1, "application/octet-stream"), 413},
      {"POST / HTTP/1.1\r\nHost: x\r\nContent-Length: -1\r\n\r\n",400},
      {"POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\n",400},
      {"POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n",400},
      {"POST / HTTP/1.1\r\nHost: x\r\n\r\n",411}, {"GET / HTTP/1.1\nHost: x\n\n",400},
      {"GET / HTTP/1.1\r\nHost: x\r\nX-Large: " + std::string(2050,'a') + "\r\n\r\n",431}}) {
    Guard guard; guard.begin(0);
    for (uint8_t byte : sample.first) if (!guard.headerByte(0, byte)) break;
    CHECK(guard.error() == sample.second);
  }
  Guard guard; guard.begin(0xfffffff0u);
  CHECK(guard.headerByte(0xfffffff0u, 'G'));
  CHECK(!guard.tick(0xfffffff0u + 2000)); CHECK(guard.error() == 408);
}
void requests_are_bounded_before_body_parsing() {
  Fixture f;
  const int writes = nvsWrites;
  auto socket = fakeWeb->enqueue(header("/api/settings", 1000000));
  f.portal.poll(); CHECK(socket->response == 413); CHECK(!socket->open); CHECK(nvsWrites == writes); CHECK(!fakeGpio);
  socket = fakeWeb->enqueue("GET / HTTP/1.1\r\nHost: 192.168.4.1\r\nX-Large: " + std::string(2100,'a') + "\r\n\r\n");
  f.portal.poll(); CHECK(socket->cursor <= 512); CHECK(socket->open);
  for (int i = 0; i < 6 && socket->open; ++i) f.portal.poll();
  CHECK(socket->response == 431); CHECK(nvsWrites == writes);
  socket = fakeWeb->enqueue(header("/api/settings", 30)); f.portal.poll();
  for (int i = 0; i < 10 && socket->open; ++i) { fakeMillis += 500; socket->input += 'a'; f.portal.poll(); }
  CHECK(socket->response == 408); CHECK(nvsWrites == writes); CHECK(!f.portal.restarting());
  // The absolute deadline prevents a byte trickle from retaining the connection.
  CHECK(fakeMillis == 5000);
}
void form_and_ota_through_the_actual_server_adapter() {
  Fixture f; f.token();
  const std::string token = fakeWeb->inputHeaders["X-Setup-Token"];
  auto socket = fakeWeb->enqueue(header("/api/activity", 1024, "application/x-www-form-urlencoded", token) + "pad=" + std::string(1020,'a'));
  for (int i = 0; i < 10 && socket->open; ++i) f.portal.poll();
  CHECK(socket->response == 200); CHECK(!fakeGpio);
  std::vector<uint8_t> bytes(512,0); bytes[0]=0xe9; bytes[1]=2; bytes[12]=5; bytes[23]=1;
  bytes[32]=0x32; bytes[33]=0x54; bytes[34]=0xcd; bytes[35]=0xab;
  bytes.insert(bytes.end(), std::begin(door::kFirmwareTag), std::end(door::kFirmwareTag));
  std::string request = header("/api/firmware", bytes.size(), "application/octet-stream", token);
  request.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  socket = fakeWeb->enqueue(request);
  for (int i = 0; i < 10 && socket->open; ++i) f.portal.poll();
  CHECK(socket->response == 202); CHECK(otaBytes == bytes); CHECK(otaSelections == 1); CHECK(f.portal.restarting());
}
void interrupted_upload_aborts_the_inactive_partition() {
  Fixture f; f.token(); const std::string token = fakeWeb->inputHeaders["X-Setup-Token"];
  auto socket = fakeWeb->enqueue(header("/api/firmware", 1024, "application/octet-stream", token));
  f.portal.poll(); CHECK(f.portal.updating());
  std::string prefix(288,0); prefix[0]=char(0xe9); prefix[1]=2; prefix[12]=5; prefix[23]=1;
  prefix[32]=0x32; prefix[33]=0x54; prefix[34]=char(0xcd); prefix[35]=char(0xab);
  socket->input += prefix; f.portal.poll(); CHECK(otaBegins == 1);
  fakeMillis += 15000; f.portal.poll();
  CHECK(socket->response == 408); CHECK(!f.portal.updating()); CHECK(otaAborts == 1); CHECK(!otaSelections);
}
void boot_confirmation_and_failure() {
  resetOta(); otaBootState = ESP_OTA_IMG_PENDING_VERIFY;
  FirmwareBoot boot; boot.begin(0); boot.poll(0,true); boot.poll(2999,true);
  CHECK(!boot.healthy()); CHECK(otaConfirmations == 0);
  boot.poll(3000,true); CHECK(boot.healthy()); CHECK(otaConfirmations == 1);
  resetOta(); otaBootState = ESP_OTA_IMG_PENDING_VERIFY;
  boot.begin(0); boot.poll(1000,true); boot.poll(2000,false); boot.poll(3000,true); boot.poll(5999,true);
  CHECK(!boot.healthy()); boot.poll(6000,true); CHECK(boot.healthy());
  resetOta(); otaBootState = ESP_OTA_IMG_PENDING_VERIFY; otaConfirmFails = true;
  boot.begin(0); boot.poll(0,true); boot.poll(30000,true); CHECK(!boot.healthy()); CHECK(otaRollbacks == 1);
  resetOta(); otaBootState = ESP_OTA_IMG_PENDING_VERIFY; otaHashFails = true;
  boot.begin(0xfffffff0u); boot.poll(0xfffffff0u+30000,true); CHECK(otaRollbacks == 1); CHECK(!boot.healthy());
}
int main() {
  bounded_headers_and_framing(); requests_are_bounded_before_body_parsing();
  form_and_ota_through_the_actual_server_adapter(); interrupted_upload_aborts_the_inactive_partition(); boot_confirmation_and_failure();
  std::cout << "PASS: bounded HTTP adapter, framing/size/idle/absolute deadlines, streamed OTA abort and boot health confirmation.\n";
}
