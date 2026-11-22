#pragma once
#include "HomeKeySelectReader.h"
#include "pn532_cxx/pn532.hpp"
#include "pn532_hal/spi.hpp"
#include <memory>
#include <optional>
#include <vector>

namespace outdoor {
class PnReader final : public HomeKeySelectReader {
 public:
  bool configure(const std::array<uint8_t, 8>& group) override;
  void startTap(bool isoDep) override;
  ExchangeResult transceive(const uint8_t* command, size_t length, uint8_t* response, size_t& size) override;
  void release() override;
  void endTap(bool isoDep) override;
  std::optional<bool> poll(std::vector<uint8_t>& uid, std::array<uint8_t, 2>& atqa, uint8_t& sak);
  bool healthy();
 private:
  bool pauseField(uint32_t duration);
  std::array<uint8_t, 18> ecp_{};
  std::unique_ptr<pn532::SpiTransport> transport_;
  std::unique_ptr<pn532::Frontend> frontend_;
  bool ready_ = false;
  bool ecpErrorReported_ = false;
  bool fieldPaused_ = false;
  uint32_t detectedAt_ = 0;
  uint32_t fieldOffAt_ = 0, fieldPauseMs_ = 0;
};
}
