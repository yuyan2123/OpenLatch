"""Browser checks for configurable relay timing; uses the local setup API fake."""
import shutil
import threading
from http.server import ThreadingHTTPServer

import test_setup_page as qa


def posts():
    return [data for path, data in qa.REQUESTS if path == "/api/settings"]


def main():
    qa.reset()
    qa.STATE["relay_pulse_ms"] = 575
    server = ThreadingHTTPServer(("127.0.0.1", 0), qa.Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    address = f"http://127.0.0.1:{server.server_port}/"
    errors = []
    try:
        with qa.sync_playwright() as playwright:
            browser = playwright.chromium.launch(
                executable_path=shutil.which("chrome") or "C:/Program Files/Google/Chrome/Application/chrome.exe",
                headless=True)
            page = browser.new_page(viewport={"width": 1280, "height": 900})
            page.on("pageerror", lambda error: errors.append(str(error)))
            page.goto(address)
            qa.wait_js(page, "!document.querySelector('#save-other').disabled")
            page.locator("#tab-infrared").click()
            assert "575 ms" in page.locator("#door-timing").inner_text()
            page.locator("#tab-other").click()
            assert page.get_by_label("Relay on time (ms)").input_value() == "575"
            for width in (320, 390, 768, 1280, 1920):
                page.set_viewport_size({"width": width, "height": 844})
                assert page.evaluate("document.documentElement.scrollWidth <= innerWidth"), width
                assert page.locator("#save-other").evaluate("e => e.clientHeight >= 44 && e.scrollWidth <= e.clientWidth")
            output = qa.ROOT / "build"
            output.mkdir(exist_ok=True)
            page.set_viewport_size({"width": 390, "height": 844})
            page.screenshot(path=str(output / "setup-relay-mobile.png"), full_page=True)
            page.set_viewport_size({"width": 1280, "height": 900})
            page.screenshot(path=str(output / "setup-relay-desktop.png"), full_page=True)
            for value in ("", "0", "801", "1.5"):
                page.locator("#relay-pulse").fill(value)
                page.locator("#save-other").click()
                assert page.locator("#relay-pulse").evaluate("e => !e.validity.valid")
                assert not posts()
            for value in ("1", "800"):
                page.locator("#relay-pulse").fill(value)
                assert page.locator("#relay-pulse").evaluate("e => e.validity.valid")
            page.locator("#relay-pulse").fill("375")
            page.wait_for_timeout(2300)
            assert page.locator("#relay-pulse").input_value() == "375"
            page.locator("#tab-wifi").click()
            assert page.locator("#unsaved-dialog").is_visible()
            page.locator("#unsaved-cancel").click()
            assert page.locator("#page-other").is_visible()
            assert page.locator("#relay-pulse").input_value() == "375"
            page.locator("#tab-wifi").click()
            page.locator("#unsaved-discard").click()
            assert page.locator("#relay-pulse").input_value() == "575"
            # Validation reveals a hidden field before moving focus to it.
            page.evaluate("document.querySelector('#relay-pulse').value='801'")
            page.locator("#save").click()
            assert page.locator("#page-other").is_visible()
            assert page.locator("#relay-pulse").evaluate("e => e === document.activeElement")
            assert not posts()
            page.locator("#relay-pulse").fill("375")
            qa.STATE["ir_learning"] = True
            qa.wait_js(page, "document.querySelector('#save-other').disabled")
            qa.STATE["ir_learning"] = False
            qa.wait_js(page, "!document.querySelector('#save-other').disabled")
            qa.SAVE_FAILURE = True
            page.locator("#save-other").click()
            qa.wait_js(page, "document.querySelector('#notice-text').textContent.includes('Storage unavailable')")
            assert page.locator("#relay-pulse").input_value() == "375"
            assert page.locator("#save-other").is_enabled()
            assert posts()[-1]["relay_pulse_ms"] == ["375"]
            qa.SAVE_FAILURE = False
            page.locator("#tab-infrared").click()
            page.locator("#unsaved-save").click()
            qa.wait_js(page, "document.querySelector('#window').textContent === 'Restarting'")
            assert posts()[-1] == {"ssid": ["Home Network"], "wifi_password": [""],
                                   "setup_password": [""], "password_required": ["0"], "relay_pulse_ms": ["375"],
                                   "nfc_max_failures": ["5"], "nfc_lockout_seconds": ["60"]}
            assert len(posts()) == 2
            assert page.locator("#save-other").is_disabled()
            assert not errors, errors
            browser.close()
        print("PASS: custom relay time, five responsive widths, numeric limits, edit retention, discard, hidden-field focus, learning guard and save/restart.")
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    main()
