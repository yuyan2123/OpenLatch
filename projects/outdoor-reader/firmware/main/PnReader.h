#pragma once
#include "ReaderSession.h"
#include "pn532_cxx/pn532.hpp"
#include "pn532_hal/spi.hpp"
#include <memory>
#include <vector>

namespace outdoor {
class PnReader final : public Reader {
 public:
  bool configure(const std::array<uint8_t, 8>& group) override;
  bool exchange(const uint8_t* command, size_t length, uint8_t* response, size_t& size) override;
  void release() override;
  bool poll(std::vector<uint8_t>& uid, std::array<uint8_t, 2>& atqa, uint8_t& sak);
  bool healthy();
 private:
  std::array<uint8_t, 18> ecp_{};
  std::unique_ptr<pn532::SpiTransport> transport_;
  std::unique_ptr<pn532::Frontend> frontend_;
  bool ready_ = false;
};
}
