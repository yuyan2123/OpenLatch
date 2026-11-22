# Phone setup: Wi-Fi and infrared

Current firmware: **1.5.0**. The English
page supports up to 16 door buttons and 16 setup-access buttons and a ten-minute idle
window. Browsing does not require login; password protection for changes is optional.
IR/HomeKit relay time is now configurable from **1–800 ms**, with a **200 ms**
default. GPIO initialization and the 2 second cooldown remain unchanged.
During the earlier 1.2.3-web-recovery bench test, upload hashes, boot hardware/storage status and the served
page matching the embedded build were verified on COM7. The existing door
and setup buttons were retained during the upgrade.

The firmware embeds a standalone English setup website in flash. It uses no
external scripts, fonts, cloud service or internet connection. After the first
USB firmware upload, routine configuration needs only a phone and power.

In **RFID**, select **Add fob** while the outdoor reader is connected. Tap the
same fob twice within 30 seconds, removing it for at least 500 ms between taps.
Door release pauses during learning. The indoor controller saves up to 16 fobs
immediately, and they survive a restart. Use **Remove** for one fob or **Clear
all fobs** for the whole list. This works before Apple Home Key is set up.
Fobs use a fixed UID on 13.56 MHz NFC‑A tags such as MIFARE Classic and
Ultralight. The reader does not support 125 kHz fobs for this feature. UID-based
fobs can be copied.

Select **Rename** beside any saved infrared button or fob and save its name.
Leave the name blank to restore the numbered label.

The **Home Key** settings configure failed-attempt limits (1–20) and lockout
time (1–3600 seconds), defaulting to 5 attempts and 60 seconds. Select **Save and
restart** to persist the settings. Counts and timers are volatile on the indoor
controller; changing credentials/UID or reconnecting the outdoor reader does not
reset failures. HomeKit and IR remain available during an NFC lockout. The tab
also reports provisioning and reader connection status. The full indoor Home Key
integration and deferred device configuration are described in [Home Key](homekey_setup.md).

## Relay time

Open **Other settings / Door release**, enter **Relay on time (ms)** as a whole
number from 1 to 800, then select **Save and restart**. For example, 500 means
0.5 seconds. HomeKit and infrared use the same duration. The independent timer
still turns the relay off if the main loop stalls; duplicate requests never
extend an active pulse. Saving does not trigger the relay.

The duration is stored in NVS and survives power loss. Version 1 settings load
with 200 ms while preserving Wi-Fi credentials, the hotspot password and the
HomeKit pairing code. The two IR lists and HomeSpan pairings are unchanged.
Saving upgrades the settings record to version 4 with CRC and readback checks. Versions 1–3 retain their existing values and load with password protection disabled; loading alone does not write NVS.
Older HTTP clients may omit `relay_pulse_ms` to retain the current duration.

## First setup

1. Power on the controller. With no saved Wi-Fi credentials, it immediately
   creates **OpenLatch-XXXXXX**. Join that AP with password **DoorSetup-2026**.
2. Open **http://192.168.4.1/** in a regular browser. Browsing does not require login; the hotspot still requires its password. Use a regular browser
   if the automatic captive-portal popup does not work.
3. Enter the **2.4 GHz** network name and password. Optionally choose a private
   **New hotspot password** under **Setup hotspot password** (12 to 63 printable
   ASCII characters). It becomes the AP password after restart; a blank keeps
   the existing password. This is also the current password used when management protection is enabled.
4. Select **Save and restart**. Settings are validated, written to NVS and read
   back before reporting success. The relay stops and requests are inhibited
   while waiting for restart. The page clears password inputs and disables
   controls after success.
5. Rejoin the home Wi-Fi and browse to **http://openlatch-xxxxxx.local/**,
   using the suffix shown in the AP name (lowercase). The page shows this address
   before saving. You can also use the controller IP from your router's client
   list. If the new credentials fail, reconnect to the AP after 30 seconds.
6. In **Remote buttons / Open the door**, select **Add door button**, then follow the two-press prompt.
   New buttons are appended, up to 16. **Cancel learning** retains saved buttons.
   Each list row has **Remove**; **Clear all door buttons** clears this whole list.
   The website shows the HomeKit pairing code needed by Apple's Home app.
7. In **Open setup access**, select **Add setup button** and teach a different
   button with the same two-press process. Learning saves immediately. Its later
   presses reopen the website for ten minutes without a reboot or door pulse.
   Until a setup button is saved, use the boot window to visit the website.
   The two lists are stored independently, with up to 16 buttons in each.
   Cross-action conflicts are rejected; duplicate learning does not consume a slot.
   Use a row's **Remove** or **Clear all setup buttons** to revoke setup buttons.
   Learning either button pauses door release from both HomeKit and IR.

The initial password above applies unless the firmware was built with a private
`DOOR_SETUP_PASSWORD` override. Restarting reopens setup access but does not reset
the saved password. Keep the new password; a forgotten password requires an
onsite firmware/storage recovery. USB IR diagnostics do not reveal it.

## Password protection

Management changes and OTA are password-free by default. Enable **Require a
password for changes and firmware updates** and save to require the current
setup password. The hotspot remains password-protected in both modes.

When protection is enabled, enter **Current setup password** above the tabs
before saving Wi-Fi/relay/NFC settings or changing IR buttons. Changing the
hotspot password or disabling protection also requires the old password;
a new password cannot authorize its own change. OTA has its own current-password
field. A blank new hotspot password keeps the existing password. Passwords are
not stored in browser persistent storage or returned by the API. Read-only status
and activity renewal grant no management authority.

## HTTP request limits

The incremental HTTP ingress checks framing and size before Arduino argument
parsing. Headers are limited to 2,048 bytes (24 fields; 512 bytes per header
line), and ordinary form bodies to 1,024 bytes. Each header/form phase has a
5-second absolute and 2-second idle deadline. OTA alone may stream up to the
app slot capacity, with 15-second idle and 180-second body deadlines.
Each poll reads at most 512 incoming bytes and returns while waiting for data.
Ambiguous lengths, duplicate headers, chunked bodies, unsupported methods and
oversized requests are rejected. Connections close after one request. Parser
errors and disconnects abort an incomplete OTA write without selecting it.

## Availability and recovery

| State | Behavior |
| --- | --- |
| No saved Wi-Fi network | Start a password-protected AP and website immediately. |
| Saved network unavailable | Try it without blocking the control loop; start AP after 30 seconds and retry every 30 seconds. |
| Online within the active window | Offer the website on the LAN; AP is off. |
| Online after ten minutes without activity | Close the website; HomeKit and enrolled IR keep working. |
| Saved setup IR button pressed | Open a fresh ten-minute website window; never pulse the relay. |
| User clicks, types or scrolls in the visible page | Extend the active window to ten minutes. |
| Still offline after ten minutes | Keep AP, DNS and website running until Wi-Fi connects. |
| Wi-Fi connects after the idle deadline | Close AP and website. Press the saved setup button or restart to configure on the LAN. |
| Wi-Fi later drops | Stop HomeKit pulses; after 30 seconds reopen AP recovery, even after the normal deadline. |

Boot and a matched setup IR button open ten-minute windows. Accepted POST
actions and `/api/activity` reset an open window to ten minutes. The page sends
activity only after trusted pointer, keyboard, input, change or scroll events,
at most once per 15 seconds while visible. Status GET requests never extend it;
background tabs pause polling. Rejected requests do not extend access and an
activity request cannot reopen an expired online window. A held/repeated IR
signal does not continuously renew access: release for at least 500 ms before
another press. Offline AP recovery remains available until Wi-Fi connects,
including across a `millis()` wrap. Saving Wi-Fi/access settings deliberately
restarts the controller and starts a new boot window.

The visible countdown runs locally using a monotonic browser clock and the
device's `remaining_ms`, updating once per displayed second. It continues while
a status request is delayed. Successful activity/action responses synchronize
the new deadline; a local countdown never grants access beyond the device policy.
All API calls are serialized so status, activity and user operations do not race
the synchronous server. Each request has a 12-second timeout. Temporary status
failures retry automatically with 1/2/4/8-second backoff, retaining edited fields;
mutations are never automatically replayed. Explicit expiry disables actions,
then probes every five seconds to detect a new IR-opened window without a reload.
Returning to the foreground or restoring network connectivity triggers a status
check, not an activity renewal. While setup is open, Wi-Fi modem sleep is disabled;
closing the window restores the exact previous sleep setting.

"Online" means a Wi-Fi station connection with an assigned IP address. It does
not require internet connectivity. Smartphone warnings about an AP with no
internet are expected; remain connected while configuring.

An unfinished learning session for either button is cancelled if the website
closes; its previous stored button remains intact. Stored IR buttons continue
to work offline and outside the website's setup window. After reopening access,
an already open page reconnects automatically; **Retry** requests an immediate
check. Edited fields are retained. Removal and clearing ask for confirmation. Removal affects only the
selected button; clearing affects only that action's list. A full list disables
adding until a button is removed. Buttons retain their slot IDs when another
is removed; a reused slot gets a fingerprint check to prevent stale deletion.

## Check the iPhone's subnet

Send a `GET` request from the iPhone, for example from a Shortcut, to
`http://openlatch-xxxxxx.local:8080/api/same-subnet`. Use the controller's IPv4
address in the URL if `.local` does not resolve.

The API returns `{"same_subnet":true}` when the requesting IPv4 address matches
the controller's Wi-Fi subnet. A reachable request from another subnet returns
`{"same_subnet":false}`. If the controller is unreachable, the request fails.
The API stays available on Wi-Fi after the setup window closes.

## Network fields and storage

Network names accept at most 32 bytes. UTF-8 names can consume several bytes per
character. Password-protected networks accept 8–63 printable ASCII characters or
a 64-digit hexadecimal PSK. To use an open network, explicitly check **This
network has no password**. An empty password otherwise preserves the password
only for the unchanged SSID; a new SSID requires a password or the open-network
checkbox. Leaving the network name empty selects AP-only setup.

An empty **New hotspot password** preserves the existing AP password. No
current Wi-Fi or setup password is returned by the status API or inserted into
HTML. HomeKit's pairing code is visible while the setup window is open.

Settings use a versioned, CRC32-protected blob in `door_setup`, separate from
the IR `door_ir` namespace, setup button `door_setup_ir` namespace and HomeSpan's pairing data. Wi-Fi changes do not
erase paired controllers or IR keys. With no `secrets.h`, a valid HomeKit pairing
code is generated and saved on first boot. Optional `secrets.h` factory defaults
seed missing settings only; persisted web settings take precedence.

Each IR namespace stores one atomic v2 blob with 16 fixed slots and CRC32.
Old v1 single-button records load into slot 0 without a write; the next successful
addition, removal or clearing upgrades that namespace. Storage verification
failure disables the affected list until repaired or successfully reloaded.

Corrupt or unreadable setup storage falls back to AP recovery with the initial
setup password and no active HomeKit service. Save settings to repair the
record. Hardware/timer faults prevent door release but leave the website
available for diagnosis. A failed save leaves the current runtime unchanged;
an unverified write may still have reached flash, so retry before restarting.

NVS is not encrypted by this application. Use this HTTP setup website only on
the protected AP or a trusted LAN. During an open window, the page and status
need no login. Stored password mode controls management changes. Mutations and activity renewal still require
a per-boot random token and same-origin checks. Host checks reject unrelated domains. There is no HTTP door
release API. When protection is enabled, settings and IR changes require
`X-Setup-Password`, and firmware upload requires `X-OTA-Password`; both validate
the current stored password. Firmware streams into the inactive OTA app slot. See [OTA updates](ota_setup.md) for
the required first USB installation and subsequent browser updates.

## HomeKit port and discovery

The website listens on TCP **80**. HomeSpan's HomeKit Accessory Protocol server
listens on TCP **1201** so both services can coexist. HomeSpan 2.1.8 explicitly
supports `setPortNum(uint16_t)` before `begin()`. Its implementation constructs
the HAP listener with that port and advertises the same value through Bonjour's
`_hap._tcp` service. The Home app discovers that service; users do not enter a
port number. [HomeSpan 2.1.8 API](https://github.com/HomeSpan/HomeSpan/blob/2.1.8/docs/Reference.md),
[listener and mDNS implementation](https://github.com/HomeSpan/HomeSpan/blob/2.1.8/src/HomeSpan.cpp).

The portal owns station attempts through `WiFi.begin()`. HomeSpan's custom
`setWifiBegin()` hook is a no-op to prevent competing reconnect attempts; it
still receives Wi-Fi events and handles HAP normally. The built-in HomeSpan AP
and serial provisioning interfaces remain disabled. No router port forwarding
is needed. LAN firewalls must allow the advertised HAP service and local mDNS.

## Verification limits

`python tools/simulate.py test` exercises the actual portal handlers with fake
network/HTTP/NVS adapters, as well as policy, settings validation, relay safety
and IR learning. `python tools/test_setup_page.py` runs installed Chrome against
a local no-login mock API, checks mobile/desktop layout and interactive flows,
and writes screenshots to `build/`. The latter requires QA-only Playwright in
`.tools/web-qa`; neither test contacts hardware.

`python tools/test_setup_resilience.py` injects slow replies, HTTP 503, truncated
JSON and dropped connections. It checks per-second countdown during a delayed
request, single-request concurrency, automatic recovery, no mutation replay,
and preservation of edits across an expired/reopened window.

Real AP association, DHCP, captive-portal behavior, mDNS, iPhone/HomePod pairing
and coexistence with physical IR pulses still need isolated hardware tests.
The incremental ingress returns while waiting for incoming data; response
writes and flash operations can still briefly delay polling. The independent
relay timer retains its software shutoff deadline.

`python tools/test_password_and_confirmation.py` covers both password modes,
old-password changes, delayed boot health, mismatched images, confirmation
timeout, no upload replay and mobile/landscape layout. Native tests also drive
the production bounded ingress and NFC manager/services through host adapters.
