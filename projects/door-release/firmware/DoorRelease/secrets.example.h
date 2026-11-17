#pragma once
// Optional factory defaults. Runtime settings in NVS take precedence.
// Wi-Fi and infrared can be configured from the setup web page without this file.
#define DOOR_WIFI_SSID ""
#define DOOR_WIFI_PASSWORD ""
// Leave empty to generate and persist a pairing code on first boot.
#define DOOR_PAIRING_CODE ""
// Initial AP password, also used by optional management/OTA protection.
// Change it from the setup page. Never commit private factory overrides.
#define DOOR_SETUP_PASSWORD "DoorSetup-2026"
