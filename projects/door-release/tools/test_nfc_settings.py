"""Exercise RFID settings, validation, status and persistence UI against fake API."""
import shutil
import threading
from http.server import ThreadingHTTPServer
import test_setup_page as qa


def main():
    qa.reset()
    server = ThreadingHTTPServer(("127.0.0.1", 0), qa.Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    errors = []
    try:
        with qa.sync_playwright() as playwright:
            browser = playwright.chromium.launch(
                executable_path=shutil.which("chrome") or "C:/Program Files/Google/Chrome/Application/chrome.exe", headless=True)
            page = browser.new_page(viewport={"width": 390, "height": 844})
            page.on("pageerror", lambda error: errors.append(str(error)))
            page.goto(f"http://127.0.0.1:{server.server_port}/")
            qa.wait_js(page, "!document.querySelector('#save-rfid').disabled")
            page.locator("#tab-rfid").click()
            assert page.locator("#nfc-max-failures").input_value() == "5"
            assert page.locator("#nfc-lockout-seconds").input_value() == "60"
            assert "not configured" in page.locator("#nfc-state").inner_text()
            for width in (320, 390, 768, 1280, 1920):
                page.set_viewport_size({"width": width, "height": 844})
                assert page.evaluate("document.documentElement.scrollWidth <= innerWidth"), width
            qa.STATE.update(nfc_configured=True)
            qa.wait_js(page, "document.querySelector('#nfc-state').textContent.includes('Set up Home Key')")
            qa.STATE.update(nfc_provisioned=True)
            qa.wait_js(page, "document.querySelector('#nfc-state').textContent.includes('Waiting for')")
            qa.STATE.update(nfc_connected=True, nfc_failures=2)
            qa.wait_js(page, "document.querySelector('#nfc-state').textContent.includes('2 failed')")
            qa.STATE.update(nfc_lockout_remaining_ms=45000)
            qa.wait_js(page, "document.querySelector('#nfc-state').textContent.includes('45 seconds')")
            assert page.locator("#save-rfid").is_enabled()
            qa.STATE.update(nfc_lockout_remaining_ms=0)
            for field, values in (("nfc-max-failures", ("", "0", "21", "1.5")),
                                  ("nfc-lockout-seconds", ("", "0", "3601", "1.5"))):
                for value in values:
                    page.locator("#" + field).fill(value)
                    page.locator("#save-rfid").click()
                    assert page.locator("#" + field).evaluate("e => !e.validity.valid")
                    assert not any(path == "/api/settings" for path, _ in qa.REQUESTS)
                page.locator("#" + field).fill("5" if field == "nfc-max-failures" else "60")
            page.locator("#nfc-max-failures").fill("3")
            page.locator("#nfc-lockout-seconds").fill("120")
            page.wait_for_timeout(2200)
            assert page.locator("#nfc-max-failures").input_value() == "3"
            page.locator("#tab-other").click()
            page.locator("#unsaved-cancel").click()
            assert page.locator("#page-rfid").is_visible()
            page.locator("#tab-other").click()
            page.locator("#unsaved-discard").click()
            assert page.locator("#nfc-max-failures").input_value() == "5"
            assert page.locator("#nfc-lockout-seconds").input_value() == "60"
            page.locator("#tab-rfid").click()
            qa.SAVE_FAILURE = True
            page.locator("#nfc-max-failures").fill("3")
            page.locator("#nfc-lockout-seconds").fill("120")
            page.locator("#save-rfid").click()
            qa.wait_js(page, "document.querySelector('#notice-text').textContent.includes('Storage unavailable')")
            assert page.locator("#nfc-max-failures").input_value() == "3"
            qa.SAVE_FAILURE = False
            output = qa.ROOT / "build"; output.mkdir(exist_ok=True)
            page.set_viewport_size({"width": 390, "height": 844})
            page.screenshot(path=str(output / "setup-rfid-mobile.png"), full_page=True)
            page.set_viewport_size({"width": 1280, "height": 900})
            page.screenshot(path=str(output / "setup-rfid-desktop.png"), full_page=True)
            page.locator("#save-rfid").click()
            qa.wait_js(page, "document.querySelector('#window').textContent === 'Restarting'")
            saved = [data for path, data in qa.REQUESTS if path == "/api/settings"][-1]
            assert saved["nfc_max_failures"] == ["3"] and saved["nfc_lockout_seconds"] == ["120"]
            assert page.locator("#save-rfid").is_disabled()
            assert not errors, errors
            browser.close()
        print("PASS: RFID settings/status, five widths, numeric bounds, dirty navigation, failure retention and save/restart.")
    finally:
        server.shutdown(); server.server_close()


if __name__ == "__main__":
    main()
