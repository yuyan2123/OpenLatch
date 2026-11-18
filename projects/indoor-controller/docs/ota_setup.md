# OTA updates

Current source **1.4.0-verified-ota** extends the original `1.3.0-ota` update flow: the indoor ESP32-C3 can receive a local firmware
update from **Other settings / Firmware update**. The outdoor ESP-32S firmware
is still installed over USB.

## First installation

Earlier builds used Huge APP with one 3 MB application partition. They cannot
receive this update over Wi-Fi. Install the OTA partition table and firmware
once over USB. The new layout has two **0x1E0000-byte (1,966,080 byte)** app slots
and retains NVS at **0x9000**, size **0x5000**, and OTA data at **0xE000**.

From `projects/indoor-controller`, build the production image:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build-firmware.ps1
```

If Home Key transport is configured, keep the correct private `nfc_peer.h` in
place for every build. The compiled image contains that device's transport keys.
The `-TestNfc` build uses public test keys and is rejected by OTA.

For the first USB installation, select the actual port (COM7 below is an example):

```powershell
./.tools/arduino-cli/arduino-cli.exe upload --config-file arduino-cli.yaml --fqbn "esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs,FlashSize=4M,EraseFlash=none" --port COM7 --input-dir build/esp32c3 firmware/OpenLatch
```

This installs bootloader, partition table, OTA data and application separately;
it does not erase NVS. **Do not use a merged image or erase the whole flash** if
you want to retain Wi-Fi settings, HomeKit pairings, IR buttons and Home Key
credentials. The build command itself never uploads or discovers ports.

## Update from the page

When upgrading from firmware that advertises the old hostname, open the
setup page using the controller's LAN IP address before uploading. The browser
can then confirm the reboot at the same address. After the upgrade, use
`http://openlatch-xxxxxx.local/` with the same six-character suffix.

1. Open setup access using the boot window or a saved setup IR button.
2. Open **Other settings / Firmware update** and check the installed version.
3. Select **build/esp32c3/OpenLatch.ino.bin** from a production build.
4. In password-free management mode, select **Update and restart** directly.
   If password protection is enabled, enter the **current setup hotspot password**.
5. Wait for upload, verification, restart and the new boot health check. Only
   **Update complete** confirms completion. Upload progress or HTTP acceptance
   alone does not indicate success. A network error never automatically replays an upload.

Save or discard other settings edits before updating. Button learning must be
finished or cancelled. Ordinary page access remains without login; firmware
installation requires a per-boot token, same-origin request and an open setup
window. The current password is additionally required when password protection
is enabled. In that mode changing the password or disabling protection also
requires the old password; the new value cannot authorize the request. Use the protected hotspot or LAN.

Door release pauses before writing. HomeKit connections are closed, infrared
capture is stopped, and pending NFC results are invalidated. After a failed
upload, infrared buffers are cleared and a fresh 500 ms quiet interval is
required. Wi-Fi settings, pairing information and learned buttons are not
rewritten by the update.

## Image and failure handling

The device streams the app directly into the inactive slot; it never buffers
the complete file in RAM or writes bootloader, partition table or NVS through
the upload endpoint. Length, ESP32-C3 chip header, app descriptor, appended image
hash and OpenLatch OTA compatibility/build tag are checked. Test builds,
other targets, merged images, truncated/oversized data and incompatible images
are rejected. Compatibility tags identify builds; they are not digital signatures.

Only a fully received and verified image is selected for next boot. Interrupted
or rejected uploads retain the current boot selection. Idle timeout is 15 seconds
and total body time is limited to 180 seconds. The new application overrides
Arduino's early automatic OTA confirmation: relay/controller, settings, IR
storage and expected NFC initialization must remain healthy for 3 seconds
before it confirms the pending image. Failed checks for 30 seconds request
rollback. A loop watchdog is enabled while a boot is pending, so a stalled
loop can reset into the bootloader's rollback path. This uses the pinned
Arduino build's rollback-enabled bootloader; physical boot behavior still
requires validation. USB recovery remains available. The dual-slot behavior follows
[Espressif's OTA model](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-reference/system/ota.html).

Build verification checks the actual partition layout, full image SHA-256,
production/test tag and size, then writes **build/esp32c3/firmware.json** with
version, size, full-file `sha256` and the appended ESP image `image_sha256`. Test artifacts are kept in **build/esp32c3-nfc-test**.

## HTTP API

`POST /api/firmware` accepts one raw app image with `Content-Type:
application/octet-stream`, a fixed `Content-Length`, `X-Setup-Token` and
`X-OTA-Password` only when protection is enabled. The browser sets the body
length from the selected file. A verified upload returns **202** with
`firmware_sha256`; this means accepted for reboot, not update complete.
`GET /api/status` exposes `boot_id`, `firmware_sha256` (the verified appended
ESP image digest) and `firmware_healthy`. The page compares the returned digest
with the uploaded file trailer and waits for a different boot ID with that
same digest and healthy state. A different image or 90-second confirmation
timeout produces an unconfirmed result, never a success message. Older images
without the health/status protocol cannot be automatically confirmed.
Settings mutations retain their 1024-byte limit; this route alone uses the OTA
slot's capacity. There is no firmware download URL, background updater or door
release action attached to this route.

## Bench acceptance on an ESP32-C3

Physical migration, interrupted uploads and boot recovery are pending. Use a
bench controller with the relay contacts disconnected from the door. Record the
firmware version, learned IR button list and configured pulse duration before
the USB migration above. Check all three again after migration, allowing for the
new firmware version.

1. Record the migrated controller's boot ID and running image digest. Begin a
   Wi-Fi upload and interrupt the connection while the body is still transferring.
   Reconnect after the upload timeout. Check that the controller still reports the
   original digest, can accept another update and responds to the saved IR button.
2. Upload a compatible production build with a **different** image digest.
   After reboot, check for a new boot ID, a healthy status and a running digest
   equal to that build's `firmware.json` field `image_sha256`.
3. Compare the learned buttons and pulse setting to the pre-migration record.
   Press the saved button once and measure one contact closure of the configured
   duration. Record the measured duration with the two image digests. Physical
   power-loss recovery requires a separate power-interruption test.
