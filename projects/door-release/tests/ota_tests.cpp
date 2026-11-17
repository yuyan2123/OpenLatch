#include <cstdlib>
#include <iostream>
#include <vector>
#include <fstream>
#include <iterator>
#include "../firmware/DoorRelease/EspFirmwareSink.h"
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)
std::vector<uint8_t> image(char kind = 'P') {
  std::vector<uint8_t> bytes(512, 0);
  bytes[0] = 0xe9; bytes[1] = 2; bytes[12] = 5; bytes[23] = 1;
  bytes[32] = 0x32; bytes[33] = 0x54; bytes[34] = 0xcd; bytes[35] = 0xab;
  const auto* prefix = reinterpret_cast<const uint8_t*>(door::kFirmwarePrefix);
  bytes.insert(bytes.end(), prefix, prefix + sizeof(door::kFirmwarePrefix) - 1);
  bytes.push_back(uint8_t(kind));
  bytes.insert(bytes.end(), std::begin(door::kFirmwareSuffix), std::end(door::kFirmwareSuffix));
  return bytes;
}
void successful_upload_and_chunk_boundaries() {
  const auto bytes = image();
  for (size_t chunk : {1u, 7u, 287u, 288u, 512u, 1436u}) {
    resetOta(); EspFirmwareSink sink; door::FirmwareUpdate update(sink);
    CHECK(update.start(0, bytes.size())); CHECK(!otaBegins);
    for (size_t i = 0; i < bytes.size(); i += chunk) {
      CHECK(update.write(1, bytes.data() + i, std::min(chunk, bytes.size() - i)));
      CHECK(!otaSelections);
    }
    CHECK(update.finish(2)); CHECK(update.complete()); CHECK(!update.active());
    CHECK(otaBytes == bytes); CHECK(otaBegins == 1); CHECK(otaEnds == 1); CHECK(otaSelections == 1);
    CHECK(!update.finish(3)); CHECK(!update.start(3, bytes.size())); CHECK(otaSelections == 1);
  }
  // A prefix constant adjacent to the actual tag must not hide the latter.
  auto adjacent = image(); adjacent.resize(512);
  adjacent.insert(adjacent.end(), std::begin(door::kFirmwarePrefix), std::end(door::kFirmwarePrefix));
  adjacent.insert(adjacent.end(), std::begin(door::kFirmwareTag), std::end(door::kFirmwareTag));
  resetOta(); EspFirmwareSink sink; door::FirmwareUpdate update(sink);
  CHECK(update.start(0, adjacent.size())); CHECK(update.write(0, adjacent.data(), adjacent.size())); CHECK(update.finish(0));
}
void rejected_images_and_limits() {
  for (int mode = 0; mode < 6; ++mode) {
    resetOta(); auto bytes = image(mode == 5 ? 'T' : 'P');
    if (mode == 0) bytes[0] = 0;
    if (mode == 1) bytes[12] = 0;
    if (mode == 2) bytes[23] = 0;
    if (mode == 3) bytes[32] = 0;
    if (mode == 4) bytes.back() ^= 1;
    EspFirmwareSink sink; door::FirmwareUpdate update(sink); CHECK(update.start(0, bytes.size()));
    update.write(0, bytes.data(), bytes.size()); CHECK(!update.finish(0)); CHECK(!otaSelections);
    CHECK(update.error()); CHECK(mode >= 4 || otaBegins == 0);
  }
  resetOta(); EspFirmwareSink sink; door::FirmwareUpdate update(sink);
  CHECK(!update.start(0, 287)); CHECK(!update.start(0, sink.capacity() + 1)); CHECK(!otaBegins);
  otaDual = false; CHECK(!sink.capacity()); CHECK(!update.start(0, 1000)); CHECK(!otaBegins);
  otaDual = true; otaSameTarget = true; CHECK(!sink.capacity()); CHECK(!update.start(0, 1000));
}
void failures_abort_and_timeout() {
  const auto bytes = image();
  for (int mode = 0; mode < 7; ++mode) {
    resetOta(); EspFirmwareSink sink; door::FirmwareUpdate update(sink);
    CHECK(update.start(0, bytes.size()));
    otaBeginFails = mode == 0; otaWriteFails = mode == 1; otaEndFails = mode == 2; otaSelectFails = mode == 3;
    otaHashFails = mode == 6;
    update.write(0, bytes.data(), mode == 4 ? bytes.size() - 1 : bytes.size());
    if (mode == 5) update.cancel();
    CHECK(!update.finish(1)); CHECK(!otaSelections); CHECK(!update.active());
  }
  resetOta(); EspFirmwareSink sink; door::FirmwareUpdate update(sink);
  const uint32_t now = 0xfffffff0u;
  CHECK(update.start(now, bytes.size())); CHECK(update.write(now, bytes.data(), 288));
  update.tick(now + 14999); CHECK(update.active()); update.tick(now + 15000); CHECK(!update.active());
  CHECK(otaAborts == 1); CHECK(!otaSelections);
  CHECK(update.start(20000, bytes.size())); CHECK(!update.write(20000, bytes.data(), bytes.size() + 1));
  CHECK(!otaSelections);
}
int main(int argc, char** argv) {
  if (argc > 1) {
    std::ifstream input(argv[1], std::ios::binary); CHECK(input.good());
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    resetOta(); EspFirmwareSink sink; door::FirmwareUpdate update(sink); CHECK(update.start(0, bytes.size()));
    for (size_t i = 0; i < bytes.size(); i += 1436)
      CHECK(update.write(0, bytes.data() + i, std::min(size_t(1436), bytes.size() - i)));
    const bool accepted = update.finish(0);
    CHECK(accepted == (argc == 2));
    std::cout << "PASS: built firmware streamed through the production OTA validator; accepted=" << accepted << ".\n";
    return 0;
  }
  successful_upload_and_chunk_boundaries(); rejected_images_and_limits(); failures_abort_and_timeout();
  std::cout << "PASS: OTA streaming/chunk boundaries, compatibility and test-image rejection, partition limits, abort, flash/verification failure and timeout.\n";
}
