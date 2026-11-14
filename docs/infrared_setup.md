# Infrared learning and door release

Current build: **1.2.2-multi-ir**. GPIO4 is explicitly configured as a
GPIO output with drive strength 3; the default release pulse is **200 ms**.
The English setup page supports multiple buttons. Digest login is temporarily disabled.

The firmware supports up to 16 door buttons and 16 setup-access buttons. All firmware identifiers,
comments and messages are English. No key is enrolled by default. Enrollment is
available through the setup website or optional local USB serial
commands, never through the remote. See [web setup](web_setup.md) for Wi-Fi/AP
access and the ten-minute idle window with persistent offline recovery.

Connect receiver VCC to ESP32 3V3, GND to GND and S to GPIO3. Verify the pin
markings on the actual receiver. The relay still uses GPIO4 and the existing
200 ms independent shutoff timer.

## Learn a button

1. Upload the firmware, then open its setup website using your phone on the
   local Wi-Fi or setup AP. Keep relay COM/NO/NC disconnected during bench setup.
   No `secrets.h`, internet connection or HomeKit pairing is required for IR.
2. Select **Add door button** (or send `ir learn` over optional USB at 115200 baud).
   Use **Add setup button** for a button that opens the website without a door pulse.
   Both HomeKit and infrared release are inhibited during
   enrollment, and any current pulse stops. Release all remote buttons for at
   least **500 ms**, then briefly press the intended button.
3. Wait for the first-press message (`ir_learning: captured` on USB). Release for at least **500 ms**, then press
   the same button again within the **30 second** enrollment window. Another
   button reports `mismatch`; the first candidate remains selected.
4. **Button learned and saved** (`ir_learning: learned` on USB) confirms the new button was
   committed and read back from NVS. Neither enrollment press opens the door.
   An additional button becomes active only after successful storage. Cancel or timeout
   retains all saved keys. A duplicate consumes no slot; a button already used
   for the other action is rejected. A storage failure disables that list until successfully
   cleared or reloaded after reboot.
5. Release the button, then press again to request a **200 ms** relay pulse.
   Observe the **3 second boot guard** and **2 second cooldown** after stopping.
   The enrolled key survives reboot; a learning session does not. When the normal
   online setup window closes, unfinished learning is cancelled and the previous
   key retained. Offline AP setup remains open until Wi-Fi connects.

| USB command | Action |
| --- | --- |
| `ir help` | List commands. |
| `ir status` | Show hardware/storage status, the first door key, list counts and learning/enable state. |
| `ir learn` | Start/restart enrollment of an additional door button. |
| `ir cancel` | Cancel enrollment and retain all saved buttons. |
| `ir forget` | Clear all door buttons; repair corrupt door IR storage. |

The website provides **Cancel learning**, per-row **Remove**, and
**Clear all door buttons** / **Clear all setup buttons**. Remove and clear actions
ask for confirmation. There is no
HTTP or serial command that directly energizes the relay. USB commands are case
sensitive and must match exactly. Malformed or oversized lines are discarded.
HomeSpan's own serial command parser stays disabled.

## HomeKit and infrared together

Both inputs run in the same polling loop and share the relay, pulse limit,
boot guard, fault latch and cooldown. Whichever eligible request is processed
first starts the pulse. A second request during a pulse returns `busy`; during
cooldown it returns `cooldown`. Neither request is queued or extends the pulse.
An authenticated HomeKit OFF stops either source. Apple Home's switch follows
the commanded relay action, including an IR pulse; it is not a door/lock sensor.
Short pulses and notifications may not be visible immediately in the Home app.

HomeKit still requires configured credentials and an authenticated connection.
Loss of Wi-Fi or the final authenticated session stops a HomeKit pulse, even if
IR is enabled. An IR pulse is independent of connectivity and continues to its
timer deadline. Network restoration never replays a request.

Supported decoders: NEC/NEC2, Sony, Samsung/SamsungLG, RC5, RC6 and JVC. Matching
uses canonical protocol, address, command and bit count. Toggle bits do not form
part of the key. Unknown/generic pulse protocols, unsupported protocols,
overflow, parity errors and frames with extra identity data are excluded.
Repeat and automatic-repeat frames are ignored. Every decoded frame updates the
activity timestamp: at least 500 ms without a decoded frame is required before
another press is eligible. This also suppresses held buttons that send repeated
full frames without a repeat flag. This quiet interval is a software heuristic;
it cannot establish physical release when reception is interrupted.

Standard IR codes remain copyable/replayable. Install the receiver in a protected
interior location. Two enrollment presses and long-press suppression do not
provide cryptographic authentication.

## Storage and troubleshooting

The `door_ir` and `door_setup_ir` NVS namespaces each contain one versioned,
CRC32-protected `key` blob, independent of HomeKit pairing. Version 2 stores
16 fixed slots (136 bytes); version 1's single button loads into slot 0 without
rewriting flash and upgrades on the next successful change. Unknown versions,
invalid or duplicate keys, wrong lengths and checksum failures disable that list.
`ir forget` writes an empty door list
and can repair that blob without erasing Wi-Fi or HomeKit data. CRC32 detects
accidental corruption; it is not authentication or encryption.

If writing or verification fails, IR is disabled in the running firmware.
The previous persistent key (or a newly committed key whose readback failed)
may load after reboot. Retry `ir forget` until the device reports success when
revoking a key. An unavailable NVS namespace may require storage repair and
reboot. Do not assume a failed erase revoked a persistent key.

`ir_diagnostic` reports protocol, address, command, bits, flags and support status.
It is rate limited independently of authorization. Check `ir status` first when
a remote does not work, then check protocol support, release interval, learning
state, boot guard, cooldown and timer/storage faults. HomeKit configuration is
not required to troubleshoot IR.

## Verification

Run `python tools/simulate.py test` for controller, actual timer/NVS adapter,
decoder mapping, enrollment, failure and mixed-input tests. Run
`python tools/simulate.py demo` for an offline enrollment/release example.
In interactive simulation use `ir_learn`, `ir`, `ir_repeat`, `ir_cancel`,
`ir_forget`, `ir_status` and `advance MS`. Simulated storage survives the simulated
`reboot` command only within that process.

Before connecting the original intercom, verify on isolated hardware:

- Learning, cancellation, mismatched presses, timeout and erase never unlock.
- An enrolled button works with and without Wi-Fi/HomeKit and survives reboot.
- Other buttons and a held button do not trigger extra pulses.
- HomeKit and IR requests during each other's pulse/cooldown are rejected.
- Disconnecting Wi-Fi stops a HomeKit pulse but preserves an IR pulse.
- An authenticated HomeKit OFF stops an IR pulse.
- Scope measurements confirm pulse duration and safe boot/power-loss behavior.

Software tests and compilation do not validate real IR waveforms, relay
electrical behavior or Apple Home integration. See [validation](../VALIDATION.md).

API references: [IRremote 4.4.1](https://github.com/Arduino-IRremote/Arduino-IRremote/tree/v4.4.1)
and [Espressif NVS](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/storage/nvs_flash.html).
