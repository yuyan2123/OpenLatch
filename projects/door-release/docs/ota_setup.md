# OTA updates

Starting with **1.3.0-ota**, the indoor ESP32-C3 can receive a local firmware
update from **Other settings / Firmware update**. The outdoor ESP-32S firmware
is still installed over USB.

## First installation

Earlier builds used Huge APP with one 3 MB application partition. They cannot
receive this update over Wi-Fi. Install the OTA partition table and firmware
once over USB. The new layout has two **0x1E0000-byte (1,966,080 byte)** app slots
and retains NVS at **0x9000**, size **0x5000**, and OTA data at **0xE000**.

From `projects/door-release`, build the production image:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build-firmware.ps1
```

If Home Key transport is configured, keep the correct private `nfc_peer.h` in
place for every build. The compiled image contains that device's transport keys.
The `-TestNfc` build uses public test keys and is rejected by OTA.

For the first USB installation, select the actual port (COM7 below is an example):

```powershell
./.tools/arduino-cli/arduino-cli.exe upload --config-file arduino-cli.yaml --fqbn "esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs,FlashSize=4M,EraseFlash=none" --port COM7 --input-dir build/esp32c3 firmware/DoorRelease
```

This installs bootloader, partition table, OTA data and application separately;
it does not erase NVS. **Do not use a merged image or erase the whole flash** if
you want to retain Wi-Fi settings, HomeKit pairings, IR buttons and Home Key
credentials. The build command itself never uploads or discovers ports.

## Update from the page

1. Open setup access using the boot window or a saved setup IR button.
2. Open **Other settings / Firmware update** and check the installed version.
3. Select **build/esp32c3/DoorRelease.ino.bin** from a production build.
4. Enter the **setup hotspot password** and select **Update and restart**.
5. Wait for upload and verification to complete. Reopen the page after reboot
   to check the new version. A network error does not automatically replay an upload.

Save or discard other settings edits before updating. Button learning must be
finished or cancelled. Ordinary page access remains without login; firmware
installation additionally requires the current setup password, per-boot token,
same-origin request and an open setup window. Use the protected hotspot or LAN.

Door release pauses before writing. HomeKit connections are closed, infrared
capture is stopped, and pending NFC results are invalidated. After a failed
upload, infrared buffers are cleared and a fresh 500 ms quiet interval is
required. Wi-Fi settings, pairing information and learned buttons are not
rewritten by the update.

## Image and failure handling

The device streams the app directly into the inactive slot; it never buffers
the complete file in RAM or writes bootloader, partition table or NVS through
the upload endpoint. Length, ESP32-C3 chip header, app descriptor, appended image
hash and Door Release OTA compatibility/build tag are checked. Test builds,
other targets, merged images, truncated/oversized data and incompatible images
are rejected. Compatibility tags identify builds; they are not digital signatures.

Only a fully received and verified image is selected for next boot. Interrupted
or rejected uploads retain the current boot selection. Idle timeout is 15 seconds
and total upload time is limited to 180 seconds. The standard Arduino bootloader
does not provide automatic rollback if a newly installed image boots and then
malfunctions; USB recovery remains available. The dual-slot behavior follows
[Espressif's OTA model](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-reference/system/ota.html).

Build verification checks the actual partition layout, full image SHA-256,
production/test tag and size, then writes **build/esp32c3/firmware.json** with
version, size and SHA-256. Test artifacts are kept in **build/esp32c3-nfc-test**.

## HTTP API

`POST /api/firmware` accepts one raw app image with `Content-Type:
application/octet-stream`, a fixed `Content-Length`, `X-Setup-Token` and
`X-OTA-Password`. The browser sets the body length from the selected file.
Settings mutations retain their 1024-byte limit; this route alone uses the OTA
slot's capacity. There is no firmware download URL, background updater or door
release action attached to this route.

Actual USB migration, interrupted power and over-the-air booting still need
validation on the physical controller. This change has not been flashed.
