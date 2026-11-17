"""Check password modes and require a healthy new boot before reporting OTA success."""
import shutil
import threading
from http.server import ThreadingHTTPServer
import test_setup_page as qa
from test_ota_page import image


def open_page(browser, address, protected=False):
    qa.reset()
    qa.STATE["password_required"] = protected
    page = browser.new_page(viewport={"width": 375, "height": 844}, reduced_motion="reduce")
    page.goto(address)
    qa.wait_js(page, "!document.querySelector('#save').disabled")
    return page


def upload(page):
    page.locator("#tab-other").click()
    page.locator("#firmware-file").set_input_files(dict(name="DoorRelease.ino.bin", mimeType="application/octet-stream", buffer=image()))
    page.locator("#firmware-upload").click()


def main():
    server = ThreadingHTTPServer(("127.0.0.1", 0), qa.Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    address = f"http://127.0.0.1:{server.server_port}/"
    try:
        with qa.sync_playwright() as playwright:
            browser = playwright.chromium.launch(executable_path=shutil.which("chrome") or "C:/Program Files/Google/Chrome/Application/chrome.exe", headless=True)
            page = open_page(browser, address)
            assert page.locator("#setup-authorization").is_hidden()
            upload(page)
            qa.wait_js(page, "document.querySelector('#firmware-status').textContent.includes('Update complete')")
            assert qa.OTA_AUTH_HEADERS == [None]
            assert page.locator("#firmware-password-field").is_hidden()
            assert page.locator("#firmware-progress").get_attribute("value") == "100"
            page.close()

            page = open_page(browser, address, True)
            assert page.locator("#setup-authorization").is_visible()
            page.get_by_text("Setup hotspot password", exact=True).click()
            page.locator("#setup-password").fill("ReplacementPassword")
            page.locator("#password-required").uncheck()
            page.locator("#save").click()
            qa.wait_js(page, "document.querySelector('#notice-text').textContent.includes('Incorrect current')")
            assert page.locator("#setup-password").input_value() == "ReplacementPassword"
            page.locator("#current-password").fill("ReplacementPassword")
            page.locator("#save").click()
            qa.wait_js(page, "!document.querySelector('#save').disabled")
            assert not [entry for entry in qa.REQUESTS if entry[0] == "/api/settings"]
            page.locator("#current-password").fill("DoorSetup-2026")
            page.locator("#save").click()
            qa.wait_js(page, "document.querySelector('#window').textContent === 'Restarting'")
            saved = [body for path, body in qa.REQUESTS if path == "/api/settings"][-1]
            assert saved["password_required"] == ["0"]
            assert saved["setup_password"] == ["ReplacementPassword"]
            assert "current_password" not in saved
            page.close()

            page = open_page(browser, address)
            qa.OTA_BOOT_MATCH = False
            upload(page)
            qa.wait_js(page, "document.querySelector('#firmware-status').textContent.includes('not confirmed')")
            assert "Update complete" not in page.locator("#firmware-status").inner_text()
            assert page.locator("#firmware-progress").get_attribute("value") != "100"
            assert len(qa.OTA_REQUESTS) == 1
            page.close()

            page = open_page(browser, address)
            qa.OTA_REBOOT = False
            upload(page)
            qa.wait_js(page, "document.querySelector('#window').textContent === 'Restarting'")
            page.evaluate("const original=Date.now; Date.now=()=>original()+100000")
            qa.wait_js(page, "document.querySelector('#firmware-status').textContent.includes('could not be confirmed')")
            assert len(qa.OTA_REQUESTS) == 1
            page.close()

            page = open_page(browser, address)
            qa.OTA_HEALTHY = False
            upload(page)
            qa.wait_js(page, "document.querySelector('#window').textContent === 'Restarting'")
            qa.wait_js(page, "document.querySelector('#firmware-status').textContent.includes('checking the new firmware')")
            assert "Update complete" not in page.locator("#firmware-status").inner_text()
            qa.OTA_HEALTHY = True
            qa.OTA_HEALTH_AT = 0
            qa.wait_js(page, "document.querySelector('#firmware-status').textContent.includes('Update complete')")
            for width, height in ((375, 844), (844, 390)):
                page.set_viewport_size({"width": width, "height": height})
                assert page.evaluate("document.documentElement.scrollWidth <= innerWidth")
            assert page.locator("#firmware-form :user-invalid").count() == 0
            page.set_viewport_size({"width": 375, "height": 844})
            page.screenshot(path=str(qa.ROOT / "build/setup-verified-ota-mobile.png"), full_page=True)
            page.close()
            browser.close()
        print("PASS: password-free OTA, old-password changes, healthy boot confirmation, mismatched image, no reboot/timeout and no upload replay.")
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    main()
