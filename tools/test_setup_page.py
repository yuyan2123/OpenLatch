"""Exercise the setup page in installed Chrome against a local Digest-auth mock.

Install QA-only dependencies: python -m pip install --target .tools/web-qa playwright
This does not simulate ESP32 radio, HomeKit, or real NVS storage.
"""
from pathlib import Path
import hashlib
import hmac
import json
import shutil
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs
from urllib.request import parse_http_list, parse_keqv_list

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / ".tools/web-qa"))
from playwright.sync_api import sync_playwright

PASSWORD = "DoorSetup-2026"
NONCE = "local-browser-test-nonce"
REALM = "DoorReleaseSetup"
PAGE = (ROOT / "firmware/DoorRelease/web/index.html").read_bytes()
STATE = {}
REQUESTS = []
SAVE_FAILURE = False
EXPIRED = False


def reset():
    global SAVE_FAILURE, EXPIRED
    STATE.clear()
    STATE.update(token="1234567890abcdef1234567890abcdef", ssid="Home Network",
                 connected=True, ap=False, ap_name="DoorRelease-AABBCC", ip="192.168.1.42",
                 hostname="door-release-aabbcc.local", remaining_seconds=580,
                 pairing_code="59263814", default_password=True, settings_ready=True,
                 hardware_ready=True, ir_ready=True, ir_learning=False, ir_enabled=False,
                 ir_protocol="none", ir_address=0, ir_command=0,
                 ir_event="Ready. Start learning to register an unlock button.")
    REQUESTS.clear()
    SAVE_FAILURE = EXPIRED = False


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def reply(self, status, data, content_type="application/json"):
        payload = data if isinstance(data, bytes) else json.dumps(data).encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def authenticate(self):
        auth = self.headers.get("Authorization", "")
        if auth.startswith("Digest "):
            fields = parse_keqv_list(parse_http_list(auth[7:]))
            digest = lambda value: hashlib.md5(value.encode()).hexdigest()
            first = digest("admin:" + REALM + ":" + PASSWORD)
            second = digest(self.command + ":" + fields.get("uri", ""))
            expected = digest(":".join([first, NONCE, fields.get("nc", ""),
                                        fields.get("cnonce", ""), "auth", second]))
            if fields.get("username") == "admin" and hmac.compare_digest(expected, fields.get("response", "")):
                return True
        self.send_response(401)
        self.send_header("WWW-Authenticate", f'Digest realm="{REALM}", nonce="{NONCE}", algorithm=MD5, qop="auth"')
        self.send_header("Content-Length", "0")
        self.end_headers()
        return False

    def do_GET(self):
        if not self.authenticate():
            return
        if self.path == "/":
            self.reply(200, PAGE, "text/html; charset=utf-8")
        elif self.path == "/api/status":
            if EXPIRED:
                self.reply(410, {"message": "Setup window closed. Restart the device to configure again."})
            else:
                self.reply(200, dict(STATE))
        else:
            self.reply(404, {"message": "Not found."})

    def do_POST(self):
        if not self.authenticate():
            return
        body = self.rfile.read(int(self.headers.get("Content-Length", "0"))).decode()
        if self.headers.get("X-Setup-Token") != STATE["token"]:
            self.reply(403, {"message": "Invalid setup token."})
            return
        REQUESTS.append((self.path, parse_qs(body, keep_blank_values=True)))
        if self.path == "/api/settings":
            self.reply(500 if SAVE_FAILURE else 200, {"message": "Storage unavailable. Retry saving." if SAVE_FAILURE else "Settings saved. Restarting now."})
            return
        if self.path == "/api/ir/learn":
            STATE.update(ir_learning=True, ir_enabled=False, ir_event="Learning started. Press the same button twice.")
        elif self.path == "/api/ir/cancel":
            STATE.update(ir_learning=False, ir_event="Learning cancelled.")
        elif self.path == "/api/ir/forget":
            STATE.update(ir_learning=False, ir_enabled=False, ir_event="Button forgotten.")
        else:
            self.reply(404, {"message": "Not found."})
            return
        self.reply(200, {"message": STATE["ir_event"]})


def main():
    global SAVE_FAILURE, EXPIRED
    reset()
    browser_path = shutil.which("chrome") or "C:/Program Files/Google/Chrome/Application/chrome.exe"
    if not Path(browser_path).exists():
        raise SystemExit("Install Chrome to run setup page browser QA.")
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    address = f"http://127.0.0.1:{server.server_port}/"
    output = ROOT / "build"
    output.mkdir(exist_ok=True)
    errors = []
    try:
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch(executable_path=browser_path, headless=True)
            context = browser.new_context(http_credentials={"username": "admin", "password": PASSWORD},
                                          viewport={"width": 1280, "height": 900})
            page = context.new_page()
            page.on("pageerror", lambda error: errors.append(str(error)))
            page.goto(address)
            page.wait_for_function("document.querySelector('#ssid').value === 'Home Network'")
            assert page.locator("#pairing-code").inner_text() == "592-63-814"
            page.screenshot(path=str(output / "setup-desktop.png"), full_page=True)
            page.set_viewport_size({"width": 390, "height": 844})
            assert page.evaluate("document.documentElement.scrollWidth <= innerWidth")
            page.screenshot(path=str(output / "setup-mobile.png"), full_page=True)
            page.locator("#ssid").fill("Edited network")
            page.wait_for_timeout(1200)
            assert page.locator("#ssid").input_value() == "Edited network"
            page.locator("#learn").click()
            page.wait_for_function("document.querySelector('#ir-state').textContent === 'Learning in progress'")
            assert page.locator("#save").is_disabled()
            page.locator("#cancel").click()
            page.wait_for_function("!document.querySelector('#save').disabled")
            page.locator("#learn").click()
            page.wait_for_function("document.querySelector('#ir-state').textContent === 'Learning in progress'")
            STATE.update(ir_learning=False, ir_enabled=True, ir_protocol="NEC", ir_address=0x1234,
                         ir_command=0x45, ir_event="Button learned and saved.")
            page.wait_for_function("document.querySelector('#ir-message').textContent === 'Button learned and saved.'")
            assert page.locator("#ir-state").inner_text() == "NEC / 1234 / 45"
            page.locator("#forget").click()
            page.wait_for_function("document.querySelector('#ir-state').textContent === 'No active unlock button'")
            STATE.update(connected=False, ap=True, remaining_seconds=0)
            page.wait_for_function("document.querySelector('#window').textContent === 'Offline recovery'")
            assert page.locator("#learn").is_enabled()
            page.locator("#wifi-password").fill("password123")
            page.locator("#setup-password").fill("PrivateSetupPassword")
            page.locator("#open-network").check()
            assert page.locator("#wifi-password").is_disabled()
            SAVE_FAILURE = True
            page.locator("#save").click()
            page.wait_for_function("document.querySelector('#notice').textContent.includes('Storage unavailable')")
            assert page.locator("#save").is_enabled()
            assert page.locator("#ssid").input_value() == "Edited network"
            assert REQUESTS[-1][1]["wifi_password"] == [""]
            assert REQUESTS[-1][1]["open_network"] == ["1"]
            page.locator("#open-network").uncheck()
            SAVE_FAILURE = False
            page.locator("#save").click()
            page.wait_for_function("document.querySelector('#window').textContent === 'Restarting'")
            assert page.locator("#save").is_disabled()
            assert page.locator("#wifi-password").input_value() == ""
            assert page.locator("#setup-password").input_value() == ""
            saved = [data for path, data in REQUESTS if path == "/api/settings"][-1]
            assert saved == {"ssid": ["Edited network"], "wifi_password": ["password123"],
                             "setup_password": ["PrivateSetupPassword"]}
            reset()
            page.reload()
            page.wait_for_function("!document.querySelector('#save').disabled")
            EXPIRED = True
            page.wait_for_function("document.querySelector('#window').textContent === 'Closed'")
            assert page.locator("#learn").is_disabled()
            assert page.locator("#notice").inner_text().startswith("Setup window closed")
            assert not errors, errors
            browser.close()
        print("PASS: Chrome Digest login, desktop/mobile layout, editing, IR actions, offline recovery, save failure/restart and expiry.")
        print("Screenshots: build/setup-desktop.png and build/setup-mobile.png")
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    main()
