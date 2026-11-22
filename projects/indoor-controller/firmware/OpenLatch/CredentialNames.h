#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <nvs.h>
#include "CredentialName.h"

namespace door {
// Names are optional metadata in the same NVS namespace. Their identity check
// keeps a reused slot from inheriting a different credential's name.
class CredentialNames {
 public:
  using Identity = std::array<uint8_t, 11>;

  static CredentialName load(nvs_handle_t handle, uint8_t slot, const Identity& identity) {
    const Key key(slot);
    Record record{};
    size_t size = record.size();
    CredentialName name;
    if (nvs_get_blob(handle, key.value, record.data(), &size) == ESP_OK &&
        size > identity.size() && size <= record.size() &&
        std::equal(identity.begin(), identity.end(), record.begin()))
      CredentialName::parse(reinterpret_cast<const char*>(record.data() + identity.size()), size - identity.size(), name);
    return name;
  }

  static bool save(nvs_handle_t handle, uint8_t slot, const Identity& identity, const CredentialName& name) {
    if (name.empty()) return clear(handle, slot);
    const Key key(slot);
    Record record{}, verify{};
    const size_t size = identity.size() + std::strlen(name.c_str());
    std::copy(identity.begin(), identity.end(), record.begin());
    std::memcpy(record.data() + identity.size(), name.c_str(), size - identity.size());
    size_t readSize = verify.size();
    return nvs_set_blob(handle, key.value, record.data(), size) == ESP_OK && nvs_commit(handle) == ESP_OK &&
           nvs_get_blob(handle, key.value, verify.data(), &readSize) == ESP_OK && readSize == size && record == verify;
  }

  static bool clear(nvs_handle_t handle, uint8_t slot) {
    const Key key(slot);
    const auto result = nvs_erase_key(handle, key.value);
    return result == ESP_ERR_NVS_NOT_FOUND || (result == ESP_OK && nvs_commit(handle) == ESP_OK);
  }

 private:
  using Record = std::array<uint8_t, Identity{}.size() + CredentialName::kMaxBytes>;
  struct Key {
    explicit Key(uint8_t slot) { std::snprintf(value, sizeof(value), "label%u", unsigned(slot)); }
    char value[9]{};
  };
};
}  // namespace door
