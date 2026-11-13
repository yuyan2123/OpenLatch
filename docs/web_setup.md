# Phone setup: Wi-Fi and infrared

Firmware 1.2.0 embeds a standalone English setup website in flash. It uses no
external scripts, fonts, cloud service or internet connection. After the first
USB firmware upload, routine configuration needs only a phone and power.

## First setup

1. Power on the controller. With no saved Wi-Fi credentials, it immediately
   creates **DoorRelease-XXXXXX**. Join that AP with password **DoorSetup-2026**.
2. Open **http://192.168.4.1/** in a regular browser. Sign in with username
   **admin** and password **DoorSetup-2026**. An automatic captive-portal popup
   may appear; use the regular browser if its login prompt does not work.
3. Enter the **2.4 GHz** network name and password. Optionally choose a private
   **New setup password** (12–63 printable ASCII characters). It becomes both
   the website login password and the AP password after restart.
4. Select **Save and restart**. Settings are validated, written to NVS and read
   back before reporting success. The relay stops and requests are inhibited
   while waiting for restart. The page clears password inputs and disables
   controls after success.
5. Rejoin the home Wi-Fi and browse to **http://door-release-xxxxxx.local/**,
   using the suffix shown in the AP name (lowercase). The page shows this address
   before saving. You can also use the controller IP from your router's client
   list. If the new credentials fail, reconnect to the AP after 30 seconds.
6. In **Infrared**, select **Learn button**, then follow the two-press prompt.
   **Cancel learning** retains the previous key; **Forget button** removes it.
   The website shows the HomeKit pairing code needed by Apple's Home app.

The initial password above applies unless the firmware was built with a private
`DOOR_SETUP_PASSWORD` override. Restarting reopens setup access but does not reset
the saved password. Keep the new password; a forgotten password requires an
onsite firmware/storage recovery. USB IR diagnostics do not reveal it.

## Availability and recovery

| State | Behavior |
| --- | --- |
| No saved Wi-Fi network | Start a password-protected AP and website immediately. |
| Saved network unavailable | Try it without blocking the control loop; start AP after 30 seconds and retry every 30 seconds. |
| Online within ten minutes of boot | Offer the website on the LAN; AP is off. |
| Online at/after ten minutes | Close the website; HomeKit and enrolled IR keep working. |
| Still offline after ten minutes | Keep AP, DNS and website running until Wi-Fi connects. |
| Wi-Fi connects after the deadline | Close AP and website immediately. Restart to configure again on the LAN. |
| Wi-Fi later drops | Stop HomeKit pulses; after 30 seconds reopen AP recovery, even after the normal deadline. |

The deadline is measured from boot; page views, login, IR learning and network
changes do not extend it. Offline AP recovery is the explicit exception. Once
opened for recovery it remains available until a successful station connection,
including across a `millis()` wrap. Saving network/access settings deliberately
restarts the controller and starts a new boot window.

"Online" means a Wi-Fi station connection with an assigned IP address. It does
not require internet connectivity. Smartphone warnings about an AP with no
internet are expected; remain connected while configuring.

An unfinished IR learning session is cancelled if the website closes. Its
previous stored button remains intact. Stored IR buttons continue to work
offline and outside the website's setup window.

## Network fields and storage

Network names accept at most 32 bytes. UTF-8 names can consume several bytes per
character. Password-protected networks accept 8–63 printable ASCII characters or
a 64-digit hexadecimal PSK. To use an open network, explicitly check **This
network has no password**. An empty password otherwise preserves the password
only for the unchanged SSID; a new SSID requires a password or the open-network
checkbox. Leaving the network name empty selects AP-only setup.

An empty **New setup password** preserves the existing login/AP password. No
current Wi-Fi or setup password is returned by the status API or inserted into
HTML. HomeKit's pairing code is visible only after authentication.

Settings use a versioned, CRC32-protected blob in `door_setup`, separate from
the IR `door_ir` namespace and HomeSpan's pairing data. Wi-Fi changes do not
erase paired controllers or IR keys. With no `secrets.h`, a valid HomeKit pairing
code is generated and saved on first boot. Optional `secrets.h` factory defaults
seed missing settings only; persisted web settings take precedence.

Corrupt or unreadable setup storage falls back to AP recovery with the initial
setup password and no active HomeKit service. Save settings to repair the
record. Hardware/timer faults prevent door release but leave the website
available for diagnosis. A failed save leaves the current runtime unchanged;
an unverified write may still have reached flash, so retry before restarting.

NVS is not encrypted by this application. Use this HTTP setup website only on
the protected AP or a trusted LAN. All routes exposing settings require Digest
authentication; mutations additionally require a per-boot random token and
same-origin checks. Host checks reject unrelated domains. There is no HTTP door
release API, firmware upload route or enabled OTA service.

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
a local Digest-auth mock API, checks mobile/desktop layout and interactive flows,
and writes screenshots to `build/`. The latter requires QA-only Playwright in
`.tools/web-qa`; neither test contacts hardware.

Real AP association, DHCP, captive-portal behavior, mDNS, iPhone/HomePod pairing
and coexistence with physical IR pulses still need isolated hardware tests.
The built-in synchronous WebServer can briefly delay polling while serving
requests; the independent relay timer still enforces its shutoff deadline.
