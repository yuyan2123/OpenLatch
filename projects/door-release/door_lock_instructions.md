# ESP32-C3 Smart Door-Release Controller

**Project:** Wi-Fi / Apple Home / infrared control of an existing Mingood/Ge-chi intercom door-release system  
**Document:** `door_lock_instructions.md`  
**Revision:** 1.0 — 12 November 2026  
**Status:** Design and procurement plan; **not an authorization to connect to the existing electric lock**  
**Language:** English  

> **Firmware 1.2.0 update — 13 November 2026:** Configuration now uses an
> authenticated on-device website for Wi-Fi and IR. Normal LAN setup closes ten
> minutes after boot. Offline AP recovery stays available until Wi-Fi connects,
> including when the deadline has passed, as requested by the user. No Wi-Fi
> configuration starts AP immediately; failed connection starts AP after 30 s.
> This replaces USB-only enrollment and disabled web/AP provisioning statements
> below. HomeKit uses TCP 1201 and setup uses TCP 80. There is no HTTP unlock API.
> See [current web setup](docs/web_setup.md).

> **Firmware scope update — 13 November 2026:** The user has requested IR learning
> and release. Firmware 1.1.0 now supports one button enrolled by two separate
> presses after the local USB `ir learn` command. The key persists in NVS and can
> release offline. HomeKit and IR share pulse/cooldown limits; Wi-Fi loss stops
> HomeKit-originated pulses only. This supersedes the diagnostic-only/default
> IR-disable and unconditional Wi-Fi-loss behavior described below. A fresh
> device still has no enrolled key. Keep the receiver in a protected interior
> location; consumer IR codes remain replayable. See
> [current IR setup and limitations](docs/infrared_setup.md). Electrical
> commissioning requirements remain applicable.

> **Important distinction:** This project operates the *existing door-release button*. It is not a replacement for the electric lock, and it does not measure whether the door is closed or the bolt is locked. The previously discussed `PT` door-position input, optocouplers, and extra door sensors are **excluded from the current scope**.
>
> **Commissioning gate:** The electrical behavior of the existing intercom's release-button circuit and the exact 3.3 V trigger threshold of the particular relay board have **not** been verified. The controller must first pass isolated bench tests. Connection to the building's release circuit must be approved from measurements and the actual installed wiring by a qualified installer.

## 1. Objectives and scope

The intended result is a small, always-powered ESP32-C3 SuperMini that:

1. Joins the home's **2.4 GHz Wi-Fi** network.
2. Appears in the Apple **Home** app using **HomeSpan** (HomeKit over Wi-Fi), without a Home Assistant server.
3. Receives a deliberate, authenticated HomeKit command and produces a **brief relay-contact closure** that emulates the existing release button, subject to final electrical verification.
4. Can also read a **38 kHz infrared receiver**. IR decoding is supported in the design, but an IR command must **not** be enabled as a door-unlock credential until the replay/security risk is explicitly accepted and mitigated.
5. Keeps the original intercom handset, doorbell, release button, door-lock power supply, and any existing door-open LED operating normally.
6. Leaves the relay contact **open by default** on power loss, reboot, firmware fault, and Wi-Fi loss, to the extent verified by actual hardware testing.

**Not included:** Door-open/door-closed sensing; `PT` electrical monitoring; lock-bolt position feedback; Home Key/NFC; replacing the original power unit; a custom public web-based unlock API; or any direct connection from the ESP32 supply to the intercom's AC/DC power wiring.

## 2. Existing intercom system: identified information

### 2.1 Brand and model identification

- **Brand:** 機智牌 (Ge-chi), formerly 明谷 (Mingood).
- **User-reported indoor intercom:** `MG-C`, detached-house installation.
- **Model printed in the supplied wiring diagram:** `MG-CE` ("MG-CE household/detached-house intercom connection diagram"). **MG-C and MG-CE must not be assumed electrically identical without checking the installed handset label.**
- **Outdoor door station in the supplied drawing:** `MD-C`.
- **Intercom power/rectifier unit:** `MG-P`.
- **Existing electric lock:** Model not supplied; its internal switching arrangement, exact voltage and current, and release-button circuit are **unknown**.
- **Observed user behavior:** The indoor intercom's indicator LED lights when the door is open and goes out when the door closes. This confirms an existing *door-position indication*, not a proven bolt-lock sensor.

### 2.2 What the supplied manufacturer/distributor PDF establishes

The user provided a three-page product/wiring PDF from Yeannet Corporation. **Page 2** shows the `MG-CE` detached-house wiring arrangement:

| Marking | Location/meaning shown in the document | Implication for this project |
|---|---|---|
| `MG-P` | Intercom rectifier/power unit | Retain unchanged |
| `CH` | Power-unit terminal; listed as not connected for the illustrated setup | Do not use |
| `-`, `+` | Document specifies **12 V DC between `-` and `+`** | **Not** ESP32 power terminals |
| `AC` (relative to `-`) | Document specifies **24 V AC between `-` and `AC`** | **Not** a safe GPIO connection |
| `MD-C` | Outdoor station | Retain unchanged |
| `AO`, `T`, `R`, `-`, `+`, `AC`, `1`–`5` | Terminals shown at the outdoor station, as applicable to the drawing | Do not reassign without installer verification |
| `MG-CE`: `AC`, `T`, `R`, `-`, `B`, `1`–`5`, `PT`, `EL` | Indoor intercom terminals shown in the detached-house schematic | Identifiers only; not a verified release-button contact map |
| Electric lock: `AC`, `PT`, `EL` | Wires from the lock to the intercom wiring arrangement | Do **not** bridge or attach the ESP32 directly to any of these based only on the drawing |
| `PT` | Existing lock-to-intercom wiring associated with door-position indication | **Out of scope**; do not tap |
| `EL` | Electric-lock/release-related wiring | **Not equivalent to a proven isolated dry contact** |

The PDF explicitly identifies the MG-P voltages as AC110 V input / AC24 V / DC12 V. **The presence of a 24 V AC rail does not establish the voltage across the `EL` terminals, the internal release switch, or the `PT` line.** The drawing shows external interconnections, **not** the switch contacts or the lock's internal schematic. [S1]

**Original document:** [Yeannet intercom product and MG-CE connection PDF](https://yeannet.com.tw/index.php?route=extension/module/advanced_downloads/download&product_id=1913&download_id=363). See page 2, titled `MG-CE 戶戶式(透天)對講機配接圖`.

### 2.3 What remains unknown and must not be guessed

- Whether the installed indoor unit is actually `MG-C`, `MG-CE`, or a compatible replacement.
- The release button's **two physical contact terminals** and whether its action is a simple closure or part of a powered/electronic circuit.
- The release circuit's voltage (AC or DC), current, and whether an external parallel contact is permitted.
- The electric lock's model, safe release duration, and behavior if held energized.
- Whether the existing wiring is shared with other indoor stations or building controls.

**Do not assume that connecting `COM`/`NO` directly across `AC`, `EL`, or `PT` is correct.** Such a connection is not established by the supplied drawing and could damage the intercom or create an unsafe release condition.

## 3. Hardware bill of materials

The first column indicates whether the item is already shown in the user's photographs or identified in the conversation. Quantities are for one controller.

| Status | Qty | Item / exact identification | Purpose / purchase specification |
|---|---:|---|---|
| Already selected | 1 | **ESP32-C3 SuperMini**, 16-pad USB-C board, ESP32-C3 | Wi-Fi, HomeSpan, GPIO control; verify board pad markings and flash/partition capacity before assembly |
| Already pictured | 1 | **Single-channel, 5 V, H/L-trigger relay board** with **Songle SRD-05VDC-SL-C** and yellow `H/L` jumper | Releases door by isolated mechanical contact; board input threshold **unverified** |
| Already pictured | 1 | **KY-022 / 1838-type IR receiver board**, three pins marked `-`, middle supply, `S` | Receives approximately 38 kHz modulated infrared remote signals |
| Existing installation | 1 | Mingood/Ge-chi `MG-C` or `MG-CE` intercom, `MG-P` and its current electric lock | Retained without modification until connection verification |
| Required | 1 | **Certified regulated USB power supply, 5 V, at least 1 A** (2 A acceptable) | Powers the ESP32 and relay; adequate regulation and safe mains isolation |
| Required | 1 | Quality **USB-C cable** compatible with ESP32 SuperMini | Supplies power to board |
| Required | 1 | **10 kΩ, 0.25 W resistor** | External pull-down on the relay-control GPIO in the proposed direct-drive configuration |
| Required | As needed | **22–26 AWG insulated hookup wire**, short runs, crimp ferrules appropriate for screw terminals, and/or secure 2.54 mm connectors | Low-voltage internal signal/power wiring |
| Required | 1 | **Insulated nonconductive enclosure**, mounting standoffs, cable glands/strain relief | Protects controller, prevents shorts and accidental operation |
| Required | As needed | Screw-terminal accessories, insulating heat shrink, labels and cable ties | Reliable installation and traceability |
| Installation tool | 1 | Digital multimeter suitable for the circuit, used by a qualified person | Identifies release-button behavior, voltage/current and verifies relay isolation |
| Optional, for IR control | 1 | Compatible handheld infrared remote (typically 38 kHz modulation) | Source of IR commands; **not secure authentication** |
| Optional / alternative if direct GPIO fails | 1 | **2N3904** NPN transistor, plus **2.2 kΩ**, **47 kΩ**, and **10 kΩ** resistors | Allows active-low relay trigger via a transistor; requires separate bench validation |
| For remote Apple Home access | 1 | Existing or purchased **HomePod / HomePod mini** (or supported Apple TV home hub) | Apple home hub for away-from-home control |

**No part to purchase for the current scope:** Optocoupler input module, ADuM1201 isolator, LTV-814, door magnetic sensor, PT sensing board, or additional door-position hardware.

### 3.1 Relay specifications versus the complete relay board

The **relay component** is `Songle SRD-05VDC-SL-C`:

- Coil nominal supply: **5 V DC**.
- Coil power: approximately **0.36 W**; nominal coil current about **72 mA**.
- Contact form: **SPDT / one changeover contact**, exposed as `NO`, `COM`, `NC` on the photographed board.
- The relay body's printed load rating is **not** proof that the complete low-cost relay module's PCB/terminal block is suitable for a particular installation. No mains voltage should be carried through this DIY unit. [S4]

The **input electronics of the specific red relay board** are **not documented by a matching manufacturer schematic**. In particular, the exact high-level trigger voltage, current demand, low-level behavior, and power-on state require testing. Do not substitute a random online relay-module specification as proof of this board's parameters.

### 3.2 ESP32-C3 SuperMini pinout reference

For the commonly sold **V1601-style 16-pad** SuperMini with the USB-C end at the top, the two pad rows are commonly marked:

| Left side, top to bottom | Right side, top to bottom |
|---|---|
| `5V` | `GPIO5` |
| `GND` / `G` | `GPIO6` |
| `3V3` / `3.3` | `GPIO7` |
| `GPIO4` | `GPIO8` |
| `GPIO3` | `GPIO9` |
| `GPIO2` | `GPIO10` |
| `GPIO1` | `GPIO20` |
| `GPIO0` | `GPIO21` |

**Confirm the actual PCB silkscreen:** SuperMini versions and board markings vary. `GPIO2`, `GPIO8` and `GPIO9` are ESP32-C3 boot-strapping pins; the proposed design avoids them. `GPIO4` also has a JTAG alternate function, so test its startup behavior in the chosen firmware. GPIO pins use approximately **3.3 V logic** and must not be connected to 5 V or 24 V signals. [S2, S3]

## 4. Electrical wiring: ESP32, relay and IR receiver

### 4.1 Preferred initial configuration: direct GPIO-to-relay input, **bench-test only**

This keeps the previously selected **no-transistor** arrangement, contingent on passing the tests in Section 9.

**Proposed relay-board jumper:** `H` (high-level trigger). **Never move the H/L jumper while powered.**

| Source | Destination | Notes |
|---|---|---|
| 5 V USB power supply | ESP32 USB-C port | Single external power source for ESP32 assembly |
| ESP32 `5V` pad | Relay `DC+` | Only if this exact SuperMini's 5 V pad is verified to provide USB VBUS and its current path supports the relay |
| ESP32 `GND` pad | Relay `DC-` | Common **controller-side** ground |
| ESP32 `GPIO4` | Relay `IN` | High-trigger control signal; **3.3 V compatibility not yet confirmed** |
| ESP32 `GPIO4` | 10 kΩ resistor → ESP32 `GND` | External pull-down to help keep GPIO4 low during reset; **not a guarantee against glitches** |
| ESP32 `3V3` pad | IR receiver middle `+` / VCC | 3.3 V only |
| ESP32 `GND` pad | IR receiver left `-` / GND | Controller-side ground |
| ESP32 `GPIO3` | IR receiver right `S` | Demodulated IR signal input |

**IR orientation:** For the **user's photograph**, with the component face toward you and the three male pins pointing downward, the receiver PCB indicates **left `-`, middle supply, right `S`**. Verify its markings before applying power; never infer the pin order just from a different KY-022 product photograph. Joy-IT's KY-022 reference documents a **3.3–5 V** operating range and **38 kHz** carrier reception. [S5]

**Important power boundaries:**

- **The relay coil is powered from 5 V, not from the 3.3 V rail and never from GPIO4.** GPIO4 supplies only the relay board's control input.
- Some SuperMini revisions expose the USB 5 V rail on a `5V` pad; **verify that connection**. If it is absent or not rated for the load, use a properly distributed **single regulated 5 V source** for both devices instead. Never connect an additional 5 V supply into the 5 V pad while another supply is feeding USB-C unless the board's power OR-ing/isolation has been verified.
- **Do not connect the controller's `GND`, `5V`, or `3V3` to the MG-P/intercom `-`, `+`, `AC`, `EL`, or `PT` wiring.** Mechanical relay **contact** terminals are electrically separate from the coil/controller supply and are the only possible interface to the intercom release circuit, once verified.

### 4.2 Controller-side text wiring diagram

```text
                   CERTIFIED 5 V USB SUPPLY
                             |
                        ESP32 USB-C
                             |
                    ESP32-C3 SuperMini
                    +------------------+
                    | 5V --------------+----> Relay DC+
                    | GND -------------+----> Relay DC-
                    |    +-------------+----> IR receiver (-)
                    | 3V3 ------------------> IR receiver (+)
                    | GPIO3 <---------------- IR receiver (S)
                    | GPIO4 ------------+---> Relay IN
                    +-------------------|
                                        |
                                       10kΩ
                                        |
                                  ESP32 GND

                    Relay jumper: H (HIGH-trigger).
                    Relay COM / NO / NC: not yet
                    connected to the intercom.
```

**Note on the diagram:** The 10 kΩ resistor connects **GPIO4 to GND** as a pull-down; it is **not** in series with `IN`. The 5 V pad is used only after confirming the USB VBUS relationship and board power-path limits. All GND connections shown are strictly on the **controller side**, never to the intercom's power system.

### 4.3 Relay terminal order seen in the user's photograph

With the relay board oriented as in the first user-supplied photo (blue Songle relay in the middle, contact terminals on the **left**, input terminals on the **right**):

| Terminal block | Top | Middle | Bottom |
|---|---|---|---|
| Left / **relay contacts** | `NO` | `COM` | `NC` |
| Right / **power and control** | `DC+` | `DC-` | `IN` |

`NO` = normally open, `COM` = common, `NC` = normally closed. **This is the photographed board's orientation only.** Identify the silkscreen again when installing; boards can be rotated or revised.

### 4.4 Relay behavior intended by the controller

| Condition | Expected GPIO4 behavior | Relay contacts |
|---|---|---|
| Power off | Controller unpowered | `COM`–`NO` open (relay released) |
| Boot, Wi-Fi reconnect, network outage | Default low/disabled | `COM`–`NO` open; **must be verified** |
| Valid release request | HIGH for a short configured pulse | `COM`–`NO` closed briefly |
| After pulse | LOW immediately | `COM`–`NO` open again |

**Initial pulse target:** approximately **400 ms**, to be adjusted **only after** validating the original release-button operation and electric-lock requirements. A longer pulse must never be used simply to overcome bad wiring or a sticking mechanism. Apply a firmware-enforced maximum pulse duration and anti-repeat interval.

### 4.5 Contingency if 3.3 V HIGH does not reliably trigger the board

Do **not** force 5 V into GPIO4. The supported contingency is to add a small driver or replace the relay board with one explicitly specified for **3.3 V GPIO inputs**. For a verified compatible active-low relay input, a possible driver is:

- `GPIO4` → **2.2 kΩ** → `2N3904` base.
- `2N3904` base → **47 kΩ** → controller GND.
- `2N3904` emitter → controller GND.
- `2N3904` collector → relay `IN`.
- Relay `IN` → **10 kΩ** pull-up → relay `DC+` (5 V), **only after confirming the input stage is compatible**.
- Relay jumper → `L` (low-level trigger), with power disconnected.

This driver is **optional, not part of the initial wiring**; its transistor pinout must be checked against the **particular manufacturer's 2N3904 datasheet**, as packages and lead arrangements can vary. Its startup behavior must be tested before connecting the electric lock.

## 5. Interface to the existing intercom release button

### 5.1 Intended functional connection — conditional only

The design uses **only the relay's voltage-free mechanical contacts**. If, and **only if**, an installer verifies that the original release button is an electrically compatible, parallelable **momentary contact**, connect the external relay's `COM` and `NO` **across those two verified switch contacts**:

```text
           VERIFIED EXISTING RELEASE SWITCH CIRCUIT

                    [Original release button]
        Point A o----------/  /----------o Point B
                |                       |
                +------ COM   NO -------+
                      [External relay]

       NC: unconnected.
       PT, EL, AC, +/-: no connection by assumption.
       ESP32 5V/GND: no connection to intercom power.
```

This is a **conditional circuit topology**, **not** an MG-C/MG-CE pin assignment. The **two actual Point A/Point B locations are not identified in the supplied PDF**. A qualified installer must verify the topology and the contact voltage/current before any contact is made. The **external relay is normally open**, preserving the original button operation when the ESP32 is off.

**Never**:

- Short `EL` to `PT`, `AC` to `EL`, or any other terminals as a speculative test.
- Feed 5 V from the ESP32 into the original lock or intercom.
- Bond controller GND to the intercom's `-` based only on matching symbols.
- Replace the original lock supply with the ESP32 supply.
- Assume a Songle relay's printed maximum rating proves compatibility with an unknown electric lock or intercom interface.

### 5.2 Installation acceptance criteria for the existing lock circuit

Before proceeding, the installer shall document:

1. Exact indoor handset label: `MG-C`, `MG-CE`, or other.
2. Actual lock model and connection terminal photographs.
3. Original release-button electrical configuration and **verified points A/B**.
4. A/B idle and operated voltages (AC/DC), current and safe contact rating; **no resistance/continuity measurement on an energized circuit**.
5. Whether the circuit is shared with other indoor stations and whether the proposed parallel contact affects their function.
6. Whether the original button, doorbell, intercom audio, and mechanical release still work after integration.

**Hard stop:** If the existing button is not a simple contact suitable for paralleling, or the measured values exceed the relay assembly's safe application limits, redesign the interface instead of proceeding.

## 6. Apple Home and HomePod design

### 6.1 Software stack (requirements only; no code in this document)

- **Firmware platform:** Arduino-ESP32 on the ESP32-C3, with the open-source **HomeSpan** HomeKit library. [S6]
- **HomeSpan 2.1.8 (verified November 2026)** supports ESP32-C3. At the time of verification it requires **Arduino-ESP32 core 3.3.0 or later**, with a **minimum 1.9 MB application partition**; check the exact board's flash and partition layout. [S6]
- **Apple accessory connection:** ESP32 ↔ HomePod / Apple Home across the local Wi-Fi network using HomeKit. No Home Assistant, MQTT broker, Firebase server, static public IP, or router port forwarding is needed for this design.
- **Away-from-home control:** Requires a functioning Apple **home hub** (HomePod/HomePod mini or supported Apple TV), internet connectivity at home, and functioning HomeKit communication between hub and ESP32. The phone may use 4G/5G elsewhere. [S7]
- **HomeSpan-specific note:** The HomeSpan project says a home hub is required for its **fully supported** operation. Do not treat purely hub-free use as a verified deployment mode. [S6]

### 6.2 Apple Home accessory representation

**Recommended with the current no-sensor configuration:** Present the operation as a clearly labeled **momentary release switch/action** (e.g., *Front Door Release*), with firmware that automatically resets the exposed switch action to OFF after the pulse.

HomeSpan supports the HomeKit `Switch` service. A `LockMechanism` service is also available, but without physical state sensing, it would have to **invent** locked/unlocked status. That would misrepresent the physical lock. A `GarageDoorOpener` service similarly requires door-state reporting and is not appropriate without reliable feedback. [S8]

- An **OFF** switch is not evidence that the door is locked.
- A successful HomeKit request is not evidence that the relay actuated or the door opened.
- Apple Home Key/NFC is **not provided** by HomeSpan plus this relay.
- The Home app's controls, automations, sharing and Siri behavior must be reviewed carefully because **any release action is security-sensitive**.

### 6.3 Pairing and remote access

1. Configure reliable 2.4 GHz Wi-Fi coverage at the ESP32 installation point. Allow local communication/mDNS between the HomePod and ESP32; avoid client-isolated guest networks.
2. Prepare the HomeSpan firmware and confirm its selected Arduino-ESP32 core and partition settings.
3. **Replace the example/default HomeSpan pairing code** with a unique private pairing code, and store it offline. Never publish the setup credentials or a reusable QR code. [S9]
4. Pair the ESP32 to Apple Home using the HomeSpan-provided HomeKit setup process.
5. In the Home app, verify the HomePod appears as the active home hub; keep its power and internet connection stable. [S7]
6. Test local Home app operation **while the relay contacts are still disconnected from the intercom**.
7. Test remote access using a phone with Wi-Fi disabled, **again with the intercom disconnected**.
8. Limit invited Home members and restrict automations; keep firmware and Apple home hub software maintained.

**Network security:** No unauthenticated `/unlock` HTTP endpoint, unauthenticated MQTT topic, public TCP port, exposed Wi-Fi SoftAP, or exposed serial-control interface may actuate the relay. Do not publish HomeSpan pairing data.

## 7. Infrared receiver and security

**Wiring:** `VCC` → ESP32 `3V3`, `GND` → ESP32 `GND`, `S` → `GPIO3` (see Section 4.1). The KY-022-like module **only receives demodulated IR pulses**; the ESP32 must decode and interpret them. [S5]

**Required IR handling:**

1. Capture IR input for diagnostics without actuating the relay.
2. Identify the actual remote control's protocol and address/command codes.
3. Ignore unknown codes, repeat frames and spurious transitions; implement debouncing, rate limiting and a release cooldown.
4. **Default to IR-unlock disabled**. Standard consumer IR remotes generally have replayable codes and are not adequate as the only authentication for a residence entry point.
5. If physical IR release is explicitly enabled later, require a local security measure (for example, a protected interior-only location or an independent authorized confirmation). A long press or multi-button sequence alone is **not cryptographic protection**.

**Do not mistake the relay board's advertised optional IR capability for an IR receiver integrated into that board.** In this design, the separate KY-022-like receiver is connected to the **ESP32**, and all commands are evaluated in the firmware before GPIO4 is activated.

## 8. Firmware behavior requirements (without implementation code)

The firmware shall:

- Initialize relay control to **OFF** as early as possible during boot; use the external pull-down as a secondary precaution.
- Accept release requests only from an authorized paired HomeKit session, or from a later **explicitly approved** IR path.
- Trigger one short relay pulse per approved request and automatically release it, independent of phone/app/network state.
- Enforce a hard maximum ON duration (target pulse 400 ms initially; adjust after intercom testing), request cooldown and duplicate-request suppression.
- Leave the relay OFF after Wi-Fi loss, HomeKit disconnection, repeated errors, watchdog reset or brownout; do not queue old unlock commands for later execution.
- Distinguish **command received**, **relay commanded**, and **door actually opened**. Only the first two are knowable without feedback sensors.
- Avoid false lock-state claims in the Apple Home UI.
- Protect configuration and HomeKit pairing credentials; require deliberate physical access to reset or re-pair.
- Support local troubleshooting without logging private pairing secrets.

## 9. Installation, testing and commissioning plan

### Stage A — verify items and labels (no live intercom connection)

- [ ] Confirm ESP32 SuperMini board version/pin labels and USB-to-5V-pad relationship.
- [ ] Confirm relay board actually says `DC+`, `DC-`, `IN`, `NO`, `COM`, `NC`, `H`, `L`.
- [ ] Confirm IR receiver pin order **on the actual module**.
- [ ] Inspect all solder joints, screw terminals, wire strain relief and insulation.
- [ ] Set the relay jumper to `H` **with power disconnected**.

### Stage B — controller-only electrical test

- [ ] With the relay **not connected to the MG-C/MG-CE**, verify ESP32 `3V3`, 5 V power rail, relay `DC+`/`DC-` polarity and IR supply.
- [ ] Verify GPIO4 output LOW and HIGH states without allowing any 5 V input to back-drive the pin.
- [ ] Verify HIGH at **3.3 V** consistently operates the relay and LOW releases it; if not, **stop and use the alternate driver/module**.
- [ ] Confirm `COM`–`NO` is open with no power and at rest, and closes **only** during the intended test pulse.
- [ ] Confirm `COM`–`NC` has the reverse contact state; leave `NC` unused.
- [ ] Repeat across power-up, unplug/replug, reset, brownout simulation where safe, Wi-Fi disconnection and repeated HomeKit commands. **Any unintended closure is a fail.**
- [ ] Verify a command produces one pulse (initial target approximately 400 ms), not a permanently energized relay.
- [ ] Verify IR reception and decoding separately **without opening a door**.

### Stage C — Apple Home functionality, intercom still disconnected

- [ ] Pair to HomeKit with unique credentials.
- [ ] Verify HomePod is active and local Home commands produce the correct bench relay pulse.
- [ ] Verify remote 4G/5G control produces the correct bench pulse and no unexpected repeats.
- [ ] Verify guests, Siri and automations do not provide unwanted release paths.
- [ ] Verify Home app labels do not claim the door is physically locked or closed.

### Stage D — licensed/qualified installer assessment of intercom

- [ ] Compare actual installed model with the supplied `MG-CE` drawing.
- [ ] Identify the existing release-button circuit using the physical circuit/measurements, **not guessed terminal names**.
- [ ] Confirm the relay's isolated `COM`/`NO` contact is electrically suitable for the original switch circuit.
- [ ] Confirm no contact can unintentionally bridge MG-P's **AC24 V**, **DC12 V**, or **AC110 V** circuits.
- [ ] If and only if compatible, connect `COM`/`NO` across the **verified** release-button contacts, leaving `NC` open.
- [ ] Test original manual release, doorbell, handset audio, and all other connected handsets.

### Stage E — final operational and fault tests

- [ ] Confirm each authorized command releases the lock once for the shortest effective pulse.
- [ ] Confirm loss of ESP32 power, failure to boot, Wi-Fi loss, and home-hub unavailability **do not release the door**.
- [ ] Confirm the existing mechanical/manual access method remains available during outages.
- [ ] Mount the controller in a protective enclosure, with no accessible live conductors or exposed screw terminals.
- [ ] Label installation, supply, contact wires and service disconnect procedure.
- [ ] Record actual measured intercom voltages, board revision, pairing recovery procedure and commissioning results.

**Go/no-go rule:** Do **not** commission for real door access unless all safety checks pass. A brief relay click by itself is not proof of safe integration with the existing lock.

## 10. Faults, limitations and corrective actions

| Symptom | Likely cause / proper response |
|---|---|
| Relay does not energize when GPIO4 is HIGH | 3.3 V may not meet this board's HIGH threshold; check measured input and use a verified driver or 3.3 V-compatible relay board. **Never apply 5 V to GPIO4.** |
| Relay activates unexpectedly at boot | GPIO float/startup behavior, wrong H/L jumper or board-input circuit; **disconnect the door lock**, test hardware fail-safe, and redesign if necessary |
| ESP32 resets when relay operates | Insufficient 5 V supply, poor wiring or interference; correct the power path and wiring before integration |
| IR signal detected but no release | Normal when IR-unlock is disabled; IR decoding and authorization are separate functions |
| Apple Home reports “No Response” | Check power, Wi-Fi, mDNS/client isolation, hub status and HomeSpan diagnostics |
| Works at home but not on cellular | Check active HomePod/Apple TV hub and its internet connectivity; no port forwarding should be needed |
| Home app shows OFF after release | Expected with a momentary release switch; **not** a physical lock/door-state indication |
| Existing release button behaves differently after adding relay | **Disconnect external relay immediately** and have installer re-evaluate actual switch circuit and loading |
| Door stays unlocked/open | Stop operation; restore original circuit and investigate relay contact welding, incorrect NO/NC use or lock characteristics |

## 11. Final material-to-wire summary

| System | Connection | Status |
|---|---|---|
| USB 5 V supply | → ESP32 USB-C | Planned |
| ESP32 `5V` (verified USB VBUS) | → relay `DC+` | Conditional on PCB/power-path check |
| ESP32 `GND` | → relay `DC-` | Planned |
| ESP32 `GPIO4` | → relay `IN` | Planned, must pass 3.3 V HIGH-trigger test |
| 10 kΩ resistor | ESP32 `GPIO4` → ESP32 `GND` | Planned startup pull-down |
| ESP32 `3V3` | → IR receiver center supply pin | Planned |
| ESP32 `GND` | → IR receiver `-` pin | Planned |
| ESP32 `GPIO3` | ← IR receiver `S` | Planned |
| Relay `COM`/`NO` | → **two installer-verified original release-button contacts** | **NOT APPROVED UNTIL VERIFIED** |
| Relay `NC` | Unconnected | Planned |
| Existing `PT` / door-open LED | Unmodified | **Out of scope** |
| Existing `EL`, `AC`, `+`, `-` lines | Unmodified; **no direct GPIO/power connection** | Mandatory restriction |

## 12. Sources and evidence hierarchy

References are supplied so the specifications can be independently checked. The **intercom drawing** is the primary evidence for its external wiring; photographed components and manufacturer datasheets establish component identification; other design choices are engineering proposals that require real-world validation.

- **[S1] Yeannet Corporation / Mingood-Ge-chi intercom catalog and wiring diagrams**, user-supplied, three pages; especially **page 2: MG-CE detached-house connection diagram**. <https://yeannet.com.tw/index.php?route=extension/module/advanced_downloads/download&product_id=1913&download_id=363>
- **[S2] Espressif, ESP32-C3 Series Datasheet**, GPIO operation, boot-strapping pins, current/voltage and JTAG alternate functions. <https://documentation.espressif.com/esp32-c3_datasheet_en.html>
- **[S3] Apache NuttX, ESP32-C3 SuperMini V1601 board documentation**, common header/USB 5 V pad mapping; verify against the actual board. <https://nuttx.incubator.apache.org/docs/latest/platforms/risc-v/esp32c3/boards/esp32c3-supermini/index.html>
- **[S4] Songle, SRD relay datasheet (manufacturer document hosted by distributor)**, SRD-05VDC-SL-C coil/contact characteristics. <https://naylampmechatronics.com/img/cms/000263/SRD-05VDC-SL-C-Datasheet.pdf> ; component listing: <https://www.lcsc.com/zh-TW/product-detail/Power-Relays_Ningbo-Songle-Relay-SRD-05VDC-SL-C_C35449.html>
- **[S5] Joy-IT SensorKit, KY-022 infrared receiver**, voltage, signal and IR modulation details. <https://sensorkit.joy-it.net/en/sensors/ky-022>
- **[S6] HomeSpan official GitHub**, supported ESP32-C3, version, Arduino core, partition and hub requirements. <https://github.com/HomeSpan/HomeSpan>
- **[S7] Apple Support, Set up a HomePod or Apple TV as a home hub**, remote access and setup requirements. <https://support.apple.com/en-us/102557>
- **[S8] HomeSpan official supported HomeKit services**, distinctions between `Switch`, `LockMechanism`, and garage-door state. <https://github.com/HomeSpan/HomeSpan/blob/master/docs/ServiceList.md>
- **[S9] HomeSpan official User Guide, pairing security**, setup code and pairing instructions. <https://github.com/HomeSpan/HomeSpan/blob/master/docs/UserGuide.md>

### Documentation limitations

- The exact **input-stage circuit and 3.3 V HIGH-trigger threshold of the photographed relay board** have not been independently confirmed.
- The actual **MG-C/MG-CE release-button terminals, voltage and switching topology** are **not specified** by the supplied diagram.
- A normally-open relay and firmware OFF state reduce risk, but do **not** prove fail-safe behavior; all abnormal startup/reset cases require bench tests.
- Infrared remote codes are generally replayable; **IR must not silently become an unauthenticated door-release channel**.
- The project deliberately **does not measure door position or lock state** at this stage.

**Project acceptance criterion:** A properly enclosed controller that responds to authorized Apple Home release requests with one verified, short, normally-open relay pulse; retains the original intercom function; remains OFF on restart/fault; and is only attached to the intercom after a qualified installer identifies a compatible release-button contact interface.
