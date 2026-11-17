#pragma once
#include <algorithm>
#include <array>
#include <cstring>
#include "FirmwareInfo.h"

namespace door {
struct FirmwareSink {
  virtual ~FirmwareSink() = default;
  virtual size_t capacity() const = 0;
  virtual bool begin(size_t size) = 0;
  virtual bool write(const uint8_t* bytes, size_t size) = 0;
  // Verify the image before selecting its inactive partition for next boot.
  virtual bool finish() = 0;
  virtual void abort() = 0;
};

class FirmwareUpdate {
 public:
  explicit FirmwareUpdate(FirmwareSink& sink) : sink_(sink) {}
  bool start(uint32_t now, size_t size) {
    if (active_ || complete_) return false;
    error_ = nullptr; expected_ = size; received_ = headerSize_ = matched_ = metadataSize_ = 0;
    production_ = test_ = opened_ = metadataActive_ = false;
    if (!sink_.capacity()) return fail("OTA partitions are unavailable. Install the OTA firmware over USB first.");
    if (size < header_.size() || size > sink_.capacity()) return fail("Firmware size is outside the available OTA partition.");
    startedAt_ = lastAt_ = now; active_ = true; return true;
  }
  bool write(uint32_t now, const uint8_t* bytes, size_t size) {
    tick(now);
    if (!active_) return false;
    if (!bytes || !size || size > expected_ - received_) return fail("Firmware length does not match the request.");
    lastAt_ = now; received_ += size;
    scan(bytes, size);
    if (!opened_) {
      const size_t copied = std::min(header_.size() - headerSize_, size);
      std::memcpy(header_.data() + headerSize_, bytes, copied); headerSize_ += copied;
      bytes += copied; size -= copied;
      if (headerSize_ < header_.size()) return true;
      // ESP image header, ESP32-C3 chip ID, SHA-256 trailer and app descriptor.
      if (header_[0] != 0xe9 || !header_[1] || header_[1] > 16 || header_[12] != 5 || header_[13] ||
          header_[23] != 1 || header_[32] != 0x32 || header_[33] != 0x54 || header_[34] != 0xcd || header_[35] != 0xab)
        return fail("Choose an ESP32-C3 Door Release application .bin, not a merged image or bootloader.");
      if (!sink_.begin(expected_)) return fail("Could not start writing the inactive OTA partition.");
      opened_ = true;
      if (!sink_.write(header_.data(), header_.size())) return fail("Firmware flash write failed.");
    }
    if (size && !sink_.write(bytes, size)) return fail("Firmware flash write failed.");
    return true;
  }
  bool finish(uint32_t now) {
    tick(now);
    if (!active_) return false;
    if (received_ != expected_ || !opened_) return fail("Firmware upload is incomplete.");
    if (!production_ || test_) return fail("This is not a production Door Release OTA firmware image.");
    const bool verified = sink_.finish(); opened_ = false;
    if (!verified) return fail("Firmware verification or boot selection failed. The current firmware is retained.");
    active_ = false; complete_ = true; return true;
  }
  void cancel() { if (active_) fail("Firmware upload cancelled. The current firmware is retained."); }
  void tick(uint32_t now) {
    if (active_ && (uint32_t(now - lastAt_) >= 15000 || uint32_t(now - startedAt_) >= 180000))
      fail("Firmware upload timed out. The current firmware is retained.");
  }
  bool active() const { return active_; }
  bool complete() const { return complete_; }
  const char* error() const { return error_; }
  size_t capacity() const { return sink_.capacity(); }
 private:
  bool fail(const char* message) {
    if (opened_) sink_.abort();
    active_ = opened_ = false; error_ = message; return false;
  }
  void scan(const uint8_t* bytes, size_t size) {
    // Prefix is distinct from its build-kind/suffix, so the validator itself
    // cannot make a test image look like production firmware.
    for (size_t i = 0; i < size; ++i) {
      const auto byte = bytes[i];
      if (metadataActive_) {
        metadata_[metadataSize_++] = byte;
        if (metadataSize_ == metadata_.size()) {
          if (!std::memcmp(metadata_.data() + 1, kFirmwareSuffix, sizeof(kFirmwareSuffix))) {
            production_ |= metadata_[0] == 'P'; test_ |= metadata_[0] == 'T';
          }
          metadataActive_ = false; metadataSize_ = 0;
        }
      }
      // Search in parallel with metadata parsing: a plain prefix constant in
      // the validator may immediately precede the actual firmware tag.
      if (byte == uint8_t(kFirmwarePrefix[matched_])) ++matched_;
      else matched_ = byte == uint8_t(kFirmwarePrefix[0]) ? 1 : 0;
      if (matched_ == sizeof(kFirmwarePrefix) - 1) {
        matched_ = metadataSize_ = 0; metadataActive_ = true;
      }
    }
  }
  FirmwareSink& sink_;
  std::array<uint8_t, 288> header_{};
  std::array<uint8_t, 1 + sizeof(kFirmwareSuffix)> metadata_{};
  size_t expected_ = 0, received_ = 0, headerSize_ = 0, matched_ = 0, metadataSize_ = 0;
  uint32_t startedAt_ = 0, lastAt_ = 0;
  bool active_ = false, complete_ = false, opened_ = false, production_ = false, test_ = false;
  bool metadataActive_ = false;
  const char* error_ = nullptr;
};
}  // namespace door
