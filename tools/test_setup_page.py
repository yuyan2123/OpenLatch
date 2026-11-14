"""Exercise the setup page in installed Chrome against a local no-login mock.

Install QA-only dependencies: python -m pip install --target .tools/web-qa playwright
This does not simulate ESP32 radio, HomeKit, or real NVS storage.
"""
from pathlib import Path
import json
import shutil
import sys
import threading
import time
from contextlib import contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / ".tools/web-qa"))
from playwright.sync_api import sync_playwright

TEMPLATE = ROOT / "firmware/DoorRelease/web/index.html"
EXPECTED = TEMPLATE.read_text(encoding="utf-8").replace(
    '@import url("tokens.css");', TEMPLATE.with_name("tokens.css").read_text(encoding="utf-8"))
EMBEDDED = (ROOT / "firmware/DoorRelease/SetupPage.h").read_text(encoding="utf-8")
PAGE = EMBEDDED.split('R"SETUPPAGE(', 1)[1].rsplit(')SETUPPAGE"', 1)[0].encode()
assert PAGE == EXPECTED.encode(), "Regenerate SetupPage.h before browser QA."
STATE = {}
REQUESTS = []
SAVE_FAILURE = False
EXPIRED = False
DEADLINE = 0
STATUS_FAULTS = []
STATUS_DELAY = 0
IN_FLIGHT = MAX_IN_FLIGHT = 0
IO_LOCK = threading.Lock()


def reset():
    global SAVE_FAILURE, EXPIRED, DEADLINE, STATUS_DELAY, MAX_IN_FLIGHT
    STATE.clear()
    STATE.update(token="1234567890abcdef1234567890abcdef", ssid="Home Network",
                 connected=True, ap=False, ap_name="DoorRelease-AABBCC", ip="192.168.1.42",
                 hostname="door-release-aabbcc.local", remaining_seconds=580,
                 pairing_code="59263814", default_password=True, settings_ready=True,
                 hardware_ready=True, ir_ready=True, ir_learning=False, ir_enabled=False,
                 ir_protocol="none", ir_address=0, ir_command=0,
                 ir_event="Ready. Start learning to register an unlock button.",
                 setup_ir_ready=True, setup_ir_learning=False, setup_ir_enabled=False,
                 setup_ir_protocol="none", setup_ir_address=0, setup_ir_command=0,
                 setup_ir_event="Not set. Learn a setup button here during the boot window.",
                 ir_count=0, ir_capacity=16, ir_buttons=[],
                 setup_ir_count=0, setup_ir_capacity=16, setup_ir_buttons=[])
    REQUESTS.clear()
    SAVE_FAILURE = EXPIRED = False
    DEADLINE = time.monotonic() + 580
    STATUS_FAULTS.clear()
    STATUS_DELAY = MAX_IN_FLIGHT = 0


def timing():
    remaining = max(0, round((DEADLINE - time.monotonic()) * 1000))
    return dict(remaining_ms=remaining, remaining_seconds=(remaining + 999)//1000, setup_window_ms=600000)


@contextmanager
def track_request(path):
    global IN_FLIGHT, MAX_IN_FLIGHT
    tracked = path.startswith("/api/")
    if tracked:
        with IO_LOCK:
            IN_FLIGHT += 1
            MAX_IN_FLIGHT = max(MAX_IN_FLIGHT, IN_FLIGHT)
    try:
        yield
    finally:
        if tracked:
            with IO_LOCK:
                IN_FLIGHT -= 1


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def reply(self, status, data, content_type="application/json"):
        global DEADLINE
        if isinstance(data, dict) and 200 <= status < 300 and self.command == "POST":
            DEADLINE = time.monotonic() + 600
            data = dict(data, **timing())
        payload = data if isinstance(data, bytes) else json.dumps(data).encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        try:
            self.wfile.write(payload)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass  # The browser may cancel a request while reloading or closing.

    def do_GET(self):
        with track_request(self.path):
            self.handle_get()

    def handle_get(self):
        if self.path == "/":
            self.reply(200, PAGE, "text/html; charset=utf-8")
        elif self.path == "/tokens.css":
            self.reply(200, (ROOT / "firmware/DoorRelease/web/tokens.css").read_bytes(), "text/css")
        elif self.path == "/api/status":
            if STATUS_DELAY:
                time.sleep(STATUS_DELAY)
            fault = STATUS_FAULTS.pop(0) if STATUS_FAULTS else None
            if fault == "disconnect":
                self.close_connection = True
                return
            if fault == "json":
                self.reply(200, b'{"interrupted":')
                return
            if fault:
                self.reply(fault, {"message": "Temporary device error."})
                return
            if EXPIRED:
                self.reply(410, {"message": "Setup window closed. Press the learned setup button or restart, then reload."})
            else:
                self.reply(200, dict(STATE, **timing()))
        else:
            self.reply(404, {"message": "Not found."})

    def do_POST(self):
        with track_request(self.path):
            self.handle_post()

    def handle_post(self):
        if EXPIRED:
            self.reply(410, {"message": "Setup window closed. Press the learned setup button or restart, then reload."})
            return
        body = self.rfile.read(int(self.headers.get("Content-Length", "0"))).decode()
        if self.headers.get("X-Setup-Token") != STATE["token"]:
            self.reply(403, {"message": "Invalid setup token."})
            return
        arguments = parse_qs(body, keep_blank_values=True)
        REQUESTS.append((self.path, arguments))
        if self.path == "/api/activity":
            STATE["remaining_seconds"] = 600
            self.reply(200, {"message": "Setup access extended."})
            return
        if self.path == "/api/settings":
            self.reply(500 if SAVE_FAILURE else 200, {"message": "Storage unavailable. Retry saving." if SAVE_FAILURE else "Settings saved. Restarting now."})
            return
        if self.path in ("/api/ir/remove", "/api/setup-ir/remove"):
            prefix = "ir_" if self.path == "/api/ir/remove" else "setup_ir_"
            slot = int(arguments["id"][0])
            saved = next((button for button in STATE[prefix+"buttons"] if button["id"] == slot), None)
            if not saved or saved["key"] != arguments["key"][0]:
                self.reply(409, {"message": "Button changed. Refresh the list and try again."})
                return
            STATE[prefix+"buttons"] = [button for button in STATE[prefix+"buttons"] if button["id"] != slot]
            STATE[prefix+"count"] = len(STATE[prefix+"buttons"])
            STATE[prefix+"enabled"] = bool(STATE[prefix+"count"])
            STATE[prefix+"event"] = "Button removed. Other saved buttons retained."
            self.reply(200, {"message": STATE[prefix+"event"]})
            return
        if self.path == "/api/ir/learn":
            STATE.update(ir_learning=True, ir_enabled=False, ir_event="Learning started. Press the same button twice.")
        elif self.path == "/api/ir/cancel":
            STATE.update(ir_learning=False, ir_event="Learning cancelled.")
        elif self.path == "/api/ir/forget":
            STATE.update(ir_learning=False, ir_enabled=False, ir_count=0, ir_buttons=[], ir_event="All door buttons cleared.")
        elif self.path == "/api/setup-ir/learn":
            STATE.update(setup_ir_learning=True, setup_ir_enabled=False, setup_ir_event="Press the setup button twice.")
        elif self.path == "/api/setup-ir/cancel":
            STATE.update(setup_ir_learning=False, setup_ir_event="Learning cancelled.")
        elif self.path == "/api/setup-ir/forget":
            STATE.update(setup_ir_learning=False, setup_ir_enabled=False, setup_ir_count=0, setup_ir_buttons=[], setup_ir_event="All setup buttons cleared.")
        else:
            self.reply(404, {"message": "Not found."})
            return
        self.reply(200, {"message": STATE["ir_event"]})


def wait_js(page, expression):
    # Playwright wait_for_function uses eval inside the page, which the device
    # CSP correctly rejects. DevTools evaluation does not weaken that policy.
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if page.evaluate(expression):
            return
        page.wait_for_timeout(100)
    raise AssertionError("Timed out waiting for: " + expression)


def saved_button(slot, command, address=0x1234):
    return dict(id=slot, protocol="NEC", address=address, command=command, bits=32,
                key=f"NEC:{address}:{command}:32")


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
            context = browser.new_context(viewport={"width": 1280, "height": 900})
            page = context.new_page()
            page.on("pageerror", lambda error: errors.append(str(error)))
            page.on("dialog", lambda dialog: dialog.accept())
            page.goto(address)
            wait_js(page, "document.querySelector('#ssid').value === 'Home Network'")
            assert page.locator("#pairing-code").inner_text() == "592-63-814"
            assert page.locator("#setup-ir-state").inner_text() == "No setup button saved"
            # An unattended tab must not keep the access window alive.
            page.wait_for_timeout(2300)
            assert not any(path == "/api/activity" for path, _ in REQUESTS)
            page.screenshot(path=str(output / "setup-desktop.png"), full_page=True)
            for width in (320, 390, 768, 1280, 1920):
                page.set_viewport_size({"width": width, "height": 844})
                assert page.evaluate("document.documentElement.scrollWidth <= innerWidth"), width
                assert page.evaluate("""[...document.querySelectorAll('button')].filter(b=>b.getClientRects().length).every(b=>b.clientHeight>=44 && b.scrollWidth<=b.clientWidth)"""), width
            page.set_viewport_size({"width": 390, "height": 844})
            page.screenshot(path=str(output / "setup-mobile.png"), full_page=True)
            page.locator("#ssid").fill("Edited network")
            wait_js(page, "document.querySelector('#window').textContent === '10:00'")
            assert any(path == "/api/activity" for path, _ in REQUESTS)
            page.wait_for_timeout(1200)
            assert page.locator("#ssid").input_value() == "Edited network"
            page.locator("#learn").click()
            wait_js(page, "document.querySelector('#ir-state').textContent === 'Learning in progress'")
            assert page.locator("#save").is_disabled()
            page.locator("#cancel").click()
            wait_js(page, "!document.querySelector('#save').disabled")
            page.locator("#learn").click()
            wait_js(page, "document.querySelector('#ir-state').textContent === 'Learning in progress'")
            STATE.update(ir_learning=False, ir_enabled=True, ir_protocol="NEC", ir_address=0x1234,
                         ir_command=0x45, ir_event="Button learned and saved.",
                         ir_count=2, ir_buttons=[saved_button(0, 0x45), saved_button(1, 0x46)])
            wait_js(page, "document.querySelector('#ir-message').textContent === 'Button learned and saved.'")
            assert page.locator("#ir-state").inner_text() == "2 of 16 buttons saved"
            assert page.locator("#ir-buttons li").count() == 2
            page.get_by_role("button", name="Remove door button 2", exact=True).click()
            wait_js(page, "document.querySelectorAll('#ir-buttons li').length === 1")
            assert STATE["ir_buttons"] == [saved_button(0, 0x45)]
            removal = [data for path, data in REQUESTS if path == "/api/ir/remove"][-1]
            assert removal == {"id": ["1"], "key": ["NEC:4660:70:32"]}
            page.locator("#forget").click()
            wait_js(page, "document.querySelector('#ir-state').textContent === 'No active unlock button'")
            page.locator("#setup-learn").click()
            wait_js(page, "document.querySelector('#setup-ir-state').textContent === 'Learning in progress'")
            assert page.locator("#learn").is_disabled()
            assert page.locator("#save").is_disabled()
            assert page.locator("#setup-cancel").is_enabled()
            page.locator("#setup-cancel").click()
            wait_js(page, "document.querySelector('#setup-learn').disabled === false")
            page.locator("#setup-learn").click()
            wait_js(page, "document.querySelector('#setup-ir-state').textContent === 'Learning in progress'")
            STATE.update(setup_ir_learning=False, setup_ir_enabled=True, setup_ir_protocol="NEC",
                         setup_ir_address=0x1234, setup_ir_command=0x46,
                         setup_ir_event="Setup button saved.", setup_ir_count=2,
                         setup_ir_buttons=[saved_button(0, 0x47), saved_button(2, 0x48)])
            wait_js(page, "document.querySelector('#setup-ir-state').textContent === '2 of 16 buttons saved'")
            STATE.update(ir_count=2, ir_enabled=True, ir_event="Door buttons saved.",
                         ir_buttons=[saved_button(0, 0x45), saved_button(1, 0x46)])
            wait_js(page, "document.querySelectorAll('#ir-buttons li').length === 2")
            page.set_viewport_size({"width": 1280, "height": 900})
            page.screenshot(path=str(output / "setup-multi-desktop.png"), full_page=True)
            page.set_viewport_size({"width": 390, "height": 844})
            page.screenshot(path=str(output / "setup-multi-mobile.png"), full_page=True)
            page.get_by_role("button", name="Remove setup button 3", exact=True).click()
            wait_js(page, "document.querySelectorAll('#setup-ir-buttons li').length === 1")
            assert STATE["setup_ir_buttons"] == [saved_button(0, 0x47)]
            # Full lists remain readable and removable on the smallest screen.
            STATE.update(setup_ir_count=16, setup_ir_buttons=[saved_button(i, 0x50+i) for i in range(16)])
            wait_js(page, "document.querySelectorAll('#setup-ir-buttons li').length === 16")
            assert page.locator("#setup-learn").is_disabled()
            assert "List full" in page.locator("#setup-ir-state").inner_text()
            page.set_viewport_size({"width": 320, "height": 844})
            assert page.evaluate("document.documentElement.scrollWidth <= innerWidth")
            assert page.get_by_role("button", name="Remove setup button 16", exact=True).is_enabled()
            page.set_viewport_size({"width": 390, "height": 844})
            page.locator("#setup-forget").click()
            wait_js(page, "document.querySelector('#setup-ir-state').textContent === 'No setup button saved'")
            STATE.update(connected=False, ap=True, remaining_seconds=0)
            wait_js(page, "document.querySelector('#window').textContent === 'Offline recovery'")
            assert page.locator("#learn").is_enabled()
            page.locator("#wifi-password").fill("password123")
            page.locator("summary", has_text="Setup hotspot password").click()
            page.locator("#setup-password").fill("PrivateSetupPassword")
            page.locator("#open-network").check()
            assert page.locator("#wifi-password").is_disabled()
            SAVE_FAILURE = True
            page.locator("#save").click()
            wait_js(page, "document.querySelector('#notice').textContent.includes('Storage unavailable')")
            assert page.locator("#save").is_enabled()
            assert page.locator("#ssid").input_value() == "Edited network"
            assert REQUESTS[-1][1]["wifi_password"] == [""]
            assert REQUESTS[-1][1]["open_network"] == ["1"]
            page.locator("#open-network").uncheck()
            SAVE_FAILURE = False
            page.locator("#save").click()
            wait_js(page, "document.querySelector('#window').textContent === 'Restarting'")
            assert page.locator("#save").is_disabled()
            assert page.locator("#wifi-password").input_value() == ""
            assert page.locator("#setup-password").input_value() == ""
            saved = [data for path, data in REQUESTS if path == "/api/settings"][-1]
            assert saved == {"ssid": ["Edited network"], "wifi_password": ["password123"],
                             "setup_password": ["PrivateSetupPassword"]}
            reset()
            page.reload()
            wait_js(page, "!document.querySelector('#save').disabled")
            page.locator("#ssid").fill("Keep this unsaved edit")
            EXPIRED = True
            wait_js(page, "document.querySelector('#window').textContent === 'Closed'")
            assert page.locator("#learn").is_disabled()
            assert page.locator("#notice").inner_text().startswith("Setup window closed")
            # Retry works after a fresh setup-button opening; no login is required.
            EXPIRED = False
            page.locator("#retry").click()
            wait_js(page, "!document.querySelector('#save').disabled")
            assert page.locator("#ssid").input_value() == "Keep this unsaved edit"
            assert not errors, errors
            browser.close()
        print("PASS: Chrome without credentials; five responsive widths, multi-button lists/removal, full-capacity mobile layout, activity renewal, recovery and save/restart.")
        print("Screenshots: build/setup-desktop.png and build/setup-mobile.png")
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    main()
