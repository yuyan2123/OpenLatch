"""Browser regressions for slow responses, dropped requests and local countdowns."""
import shutil
import threading
import time
from http.server import ThreadingHTTPServer
from pathlib import Path

import test_setup_page as qa


def wait_server(predicate, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.025)
    raise AssertionError("Timed out waiting for mock server state")


def seconds(page):
    value = page.locator("#window").inner_text()
    minutes, seconds = map(int, value.split(":"))
    return minutes * 60 + seconds


def main():
    qa.reset()
    qa.STATUS_FAULTS[:] = [503, "json", "disconnect"]
    server = ThreadingHTTPServer(("127.0.0.1", 0), qa.Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    address = f"http://127.0.0.1:{server.server_port}/"
    chrome = shutil.which("chrome") or "C:/Program Files/Google/Chrome/Application/chrome.exe"
    errors = []
    try:
        with qa.sync_playwright() as playwright:
            browser = playwright.chromium.launch(executable_path=chrome, headless=True)
            context = browser.new_context(viewport={"width": 390, "height": 844})
            page = context.new_page()
            page.on("pageerror", lambda error: errors.append(str(error)))
            page.goto(address)
            qa.wait_js(page, "!document.querySelector('#save').disabled")
            assert not qa.STATUS_FAULTS
            assert not qa.REQUESTS  # Initial reconnects are not user activity.
            assert page.locator("#notice").is_hidden()
            page.locator("#tab-infrared").click()
            qa.wait_js(page, "document.querySelector('#window').textContent === '10:00'")
            page.wait_for_timeout(200)

            # With a response still in flight, the clock must tick every second.
            qa.STATUS_DELAY = 3.2
            wait_server(lambda: qa.IN_FLIGHT == 1)
            first = seconds(page)
            page.wait_for_timeout(1200)
            second = seconds(page)
            page.wait_for_timeout(1200)
            third = seconds(page)
            assert 1 <= first - second <= 2, (first, second, third)
            assert 1 <= second - third <= 2, (first, second, third)

            # Start a POST while a slow poll is outstanding. The browser must
            # wait for the poll, send the POST once, then load its new state.
            page.locator("#learn").click()
            qa.wait_js(page, "document.querySelector('#ir-state').textContent === 'Learning in progress'")
            assert qa.MAX_IN_FLIGHT == 1, qa.MAX_IN_FLIGHT
            assert sum(path == "/api/ir/learn" for path, _ in qa.REQUESTS) == 1
            qa.STATUS_DELAY = 0
            page.locator("#cancel").click()
            qa.wait_js(page, "!document.querySelector('#save').disabled")
            page.locator("#tab-wifi").click()
            page.locator("#ssid").fill("Preserve this network")
            page.locator("#wifi-password").fill("PreserveThisPassword")
            qa.wait_js(page, "document.querySelector('#window').textContent === '10:00'")

            # Multiple temporary failures must recover with no reload or Retry.
            qa.STATUS_FAULTS[:] = [503, "json", "disconnect"]
            qa.wait_js(page, "document.querySelector('#notice-text').textContent.includes('Reconnecting automatically')")
            assert page.locator("#ssid").is_enabled()
            assert page.locator("#save").is_disabled()
            qa.wait_js(page, "!document.querySelector('#save').disabled")
            assert not qa.STATUS_FAULTS
            assert page.locator("#ssid").input_value() == "Preserve this network"
            assert page.locator("#wifi-password").input_value() == "PreserveThisPassword"
            assert page.locator("#notice").is_hidden()
            assert not any(path == "/api/settings" for path, _ in qa.REQUESTS)

            # A real expiry stays closed; reopening via IR (mocked here) is
            # detected automatically and restores the same edited form.
            qa.EXPIRED = True
            qa.wait_js(page, "document.querySelector('#window').textContent === 'Closed'")
            assert page.locator("#learn").is_disabled()
            qa.STATE["token"] = "abcdef1234567890abcdef1234567890"
            qa.EXPIRED = False
            qa.wait_js(page, "!document.querySelector('#save').disabled")
            assert page.locator("#ssid").input_value() == "Preserve this network"
            assert page.locator("#wifi-password").input_value() == "PreserveThisPassword"
            assert qa.MAX_IN_FLIGHT == 1
            assert not errors, errors
            page.screenshot(path=str(qa.ROOT / "build/setup-recovery-mobile.png"), full_page=True)
            browser.close()
        print("PASS: per-second local countdown during slow polls; one API request at a time; initial/temporary failure recovery; no POST replay; edits retained across expiry/reopen.")
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    main()
