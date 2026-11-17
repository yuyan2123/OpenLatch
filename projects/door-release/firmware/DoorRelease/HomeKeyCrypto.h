#pragma once
#include <psa/crypto.h>
#include <sodium.h>

// Initialized on the main task before HAP provisioning or the NFC worker.
inline bool homeKeyCryptoReady() {
  static const bool ready = psa_crypto_init() == PSA_SUCCESS && sodium_init() >= 0;
  return ready;
}
