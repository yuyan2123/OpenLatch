# Apple Home Key

`1.2.5-rfid-limits` adds indoor Home Key verification and credential provisioning
to the ESP32-C3 controller. The outdoor ESP-32S/PN532 forwards NFC APDUs;
the indoor controller runs the vendored DigitalDoorKey HomeKey profile from
`third_party/DigitalDoorKey`. Its exact source version and license are included
in the repository. A card UID
or outdoor response alone cannot open the door.

## Connect the controllers

Obtain both boards' **Wi-Fi station MAC addresses**, printed by the unconfigured
firmware over USB. Actual device profiles have intentionally not been generated
yet. Do not substitute AP or Bluetooth addresses.

From `projects/outdoor-reader`, generate the paired transport profile once:

```powershell
python tools/provision.py --indoor-mac AA:BB:CC:DD:EE:F0 --outdoor-mac AA:BB:CC:DD:EE:F2
python tools/build.py --require-provisioned
```

Replace both example addresses. From `projects/door-release`, import the matching
profile and build the indoor firmware:

```powershell
python tools/import_nfc_peer.py ../outdoor-reader/private/indoor-peer.json
powershell -ExecutionPolicy Bypass -File tools/build-firmware.ps1
```

The importer reuses the same PMK, LMK and HMAC key. It refuses to overwrite
`firmware/DoorRelease/nfc_peer.h` unless `--force` is supplied. This file and
the outdoor `private` directory are ignored by Git. A profile for another
station MAC cannot start the indoor transport. These are transport keys;
Home Key credentials come from Apple Home and remain indoors.

Both boards need firmware built with the matching profile. Build tools do not
upload or search serial ports. The indoor reader uses the connected Wi-Fi channel;
the outdoor channel mask must include it. Wi-Fi loss cancels NFC transport;
infrared retains its offline operation.

## Provision Home Key

Connect the indoor controller to Wi-Fi and pair it using Apple Home and the
setup-page pairing code. The accessory advertises lock and NFC access services
alongside the original release switch. Apple Home supplies the reader identity
and credentials over the authenticated NFC access control point. Paired HomeKit
controllers supply issuer public keys. Removing a controller revokes its
issuers/endpoints; removing the last pairing clears Home Key credentials.
Pending verification results cannot survive credential changes.

Verification runs in a separate task. Each successful result is consumed once
and expires after 250 ms. Opening still passes the shared boot guard, cooldown,
busy, learning, restart and fault checks. Results do not queue openings or extend
a pulse. The original switch keeps its existing HomeKit IIDs.

No lock sensor is fitted. The HomeKit lock's physical state is **Unknown**;
the original momentary release switch remains available.

## Failed attempts

In **RFID**, set **Failed attempt limit** (1–20) and **Lockout time (seconds)**
(1–3600), then select **Save and restart**. Defaults are **5** and **60**.

- Completed invalid Home Key verification increments a global consecutive count.
- Valid verification clears failures. Changing UID does not reset the count.
- Duplicate packets/completions count once. Transport, reader, storage,
  cancelled and expired attempts do not count as invalid credentials.
- At the threshold, all new NFC attempts are refused until lockout expires.
  Scanning during lockout does not extend it.
- HomeKit commands and infrared retain their authorization paths.

Settings survive power loss. Counts and timers are kept in RAM on the indoor
controller and reset when it restarts. Restarting or reconnecting the outdoor
reader does not reset them. The page reports configuration, provisioning,
connection, failure count and lockout. There is no HTTP unlock, verification
result or counter-reset endpoint.

## Validation

Native tests cover the limiter, settings migration, credential NVS corruption and
readback failures, both transport state machines together, stale/duplicate
responses and shared output rules. Chrome checks cover RFID settings and layout.

Without a transport profile the standard firmware keeps NFC disabled. To compile
and link the entire NFC path with public test keys, use this separate build:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build-firmware.ps1 -TestNfc
```

Output is **build/esp32c3-nfc-test**, for compilation only. Do not install this
test image. The command refuses to run when a real indoor profile exists.

Physical ESP-NOW exchanges, Apple Home provisioning, iPhone/Watch authentication
timing and relay contacts still require tests on the two boards.
