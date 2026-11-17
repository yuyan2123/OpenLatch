# NFC host adapters

`nfc_manager_tests.cpp` includes the production `HomeKeyManager.h` and
`HomeKeyServices.h`, drives the real indoor/outdoor sessions, and replaces
ESP-NOW, FreeRTOS, HomeSpan and the cryptographic profile with deterministic
host adapters. Profile success/failure is controlled by the test; this suite
does not validate Apple cryptography, radio behavior or real HomeKit pairing.

Only this test target uses these include paths and `DOOR_NATIVE_NFC_TEST`.
The fixed profile is always used, even if a private device profile exists.
The production header rejects that test flag when compiled for Arduino.
