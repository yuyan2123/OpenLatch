#pragma once
#include "ReaderSession.h"

namespace outdoor {
// SELECT is public and fixed. All subsequent APDUs and authorization stay indoors.
class HomeKeySelectReader : public Reader {
 public:
  inline static constexpr std::array<uint8_t, 13> select{
      0x00,0xa4,0x04,0x00,0x07,0xa0,0x00,0x00,0x08,0x58,0x01,0x01,0x00};

  void startTap(bool isoDep) override {
    pending_ = isoDep;
    if (!isoDep) return;
    length_ = response_.size();
    result_ = transceive(select.data(), select.size(), response_.data(), length_);
  }

  ExchangeResult exchange(const uint8_t* command, size_t length, uint8_t* response, size_t& size) final {
    if (pending_) {
      pending_ = false;
      if (length == select.size() && std::equal(select.begin(), select.end(), command)) {
        if (result_ != ExchangeResult::Ok) return result_;
        if (length_ < 2 || length_ > response_.size() || length_ > size) return ExchangeResult::Failed;
        std::copy_n(response_.begin(), length_, response);
        size = length_;
        return ExchangeResult::Ok;
      }
    }
    return transceive(command, length, response, size);
  }

 protected:
  virtual ExchangeResult transceive(const uint8_t*, size_t, uint8_t*, size_t&) = 0;
 private:
  std::array<uint8_t, kMaxPayload> response_{};
  size_t length_ = 0;
  ExchangeResult result_ = ExchangeResult::Failed;
  bool pending_ = false;
};
}
