"""Chrome OTA form checks using synthetic images and the local API fake."""
import shutil
import threading
from http.server import ThreadingHTTPServer
import test_setup_page as qa


def image():
    data = bytearray(512)
    data[0] = 0xe9; data[1] = 2; data[12] = 5; data[23] = 1
    data[32:36] = bytes.fromhex("3254cdab")
    return bytes(data)


def choose(page, data=None, name="DoorRelease.ino.bin"):
    page.locator("#firmware-file").set_input_files(dict(name=name, mimeType="application/octet-stream", buffer=image() if data is None else data))
    page.locator("#firmware-password").fill("DoorSetup-2026")


def main():
    qa.reset()
    server = ThreadingHTTPServer(("127.0.0.1", 0), qa.Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    errors = []
    try:
        with qa.sync_playwright() as playwright:
            browser = playwright.chromium.launch(executable_path=shutil.which("chrome") or "C:/Program Files/Google/Chrome/Application/chrome.exe", headless=True)
            page = browser.new_page(viewport={"width": 390, "height": 844})
            page.on("pageerror", lambda error: errors.append(str(error)))
            page.goto(f"http://127.0.0.1:{server.server_port}/")
            qa.wait_js(page, "!document.querySelector('#save').disabled")
            page.locator("#tab-other").click()
            assert page.locator("#firmware-version").inner_text() == "1.3.0-ota"
            for width in (320, 390, 768, 1280, 1920):
                page.set_viewport_size({"width": width, "height": 844})
                assert page.evaluate("document.documentElement.scrollWidth <= innerWidth"), width
            qa.STATE["ota_supported"] = False
            qa.wait_js(page, "document.querySelector('#firmware-upload').disabled")
            assert "USB" in page.locator("#firmware-status").inner_text()
            qa.STATE["ota_supported"] = True
            qa.wait_js(page, "!document.querySelector('#firmware-upload').disabled")
            choose(page, b"bad")
            page.locator("#firmware-upload").click()
            qa.wait_js(page, "document.querySelector('#firmware-status').classList.contains('error')")
            assert not qa.OTA_REQUESTS
            choose(page, bytes(512))
            page.locator("#firmware-upload").click()
            qa.wait_js(page, "document.querySelector('#firmware-status').textContent.includes('ESP32-C3')")
            assert not qa.OTA_REQUESTS
            choose(page, image(), "wrong.txt")
            page.locator("#firmware-upload").click()
            qa.wait_js(page, "document.querySelector('#firmware-status').textContent.includes('fits')")
            assert not qa.OTA_REQUESTS
            choose(page)
            page.locator("#relay-pulse").fill("350")
            page.locator("#firmware-upload").click()
            assert "Save or discard" in page.locator("#firmware-status").inner_text()
            page.locator("#relay-pulse").fill("200")
            page.locator("#firmware-password").fill("WrongSetupPassword")
            page.locator("#firmware-upload").click()
            qa.wait_js(page, "document.querySelector('#firmware-status').textContent.includes('Incorrect setup password')")
            assert page.locator("#firmware-password").input_value() == ""
            assert not qa.OTA_REQUESTS
            qa.OTA_FAILURE = True
            choose(page)
            page.locator("#firmware-upload").click()
            qa.wait_js(page, "document.querySelector('#firmware-status').textContent.includes('verification failed')")
            assert page.locator("#firmware-upload").is_enabled()
            assert qa.OTA_REQUESTS == [image()]
            assert page.locator("#firmware-file").evaluate("e => e.files.length === 1")
            qa.STATE["ir_learning"] = True
            qa.wait_js(page, "document.querySelector('#firmware-upload').disabled")
            qa.STATE["ir_learning"] = False
            qa.wait_js(page, "!document.querySelector('#firmware-upload').disabled")
            qa.OTA_FAILURE = False; qa.OTA_DELAY = 2
            choose(page)
            page.locator("#firmware-upload").click()
            qa.wait_js(page, "document.querySelector('#firmware-upload').getAttribute('aria-busy') === 'true'")
            assert page.locator("#save").is_disabled()
            assert page.locator("#tab-rfid").is_disabled()
            assert page.locator("#firmware-progress").is_visible()
            qa.wait_js(page, "document.querySelector('#window').textContent === 'Restarting'")
            assert qa.OTA_REQUESTS == [image(), image()]
            assert page.locator("#firmware-progress").get_attribute("value") == "100"
            assert page.locator("#firmware-file").evaluate("e => e.files.length === 0")
            assert page.locator("#firmware-password").input_value() == ""
            assert qa.MAX_IN_FLIGHT == 1, qa.MAX_IN_FLIGHT
            output = qa.ROOT / "build"
            page.set_viewport_size({"width": 390, "height": 844})
            page.screenshot(path=str(output / "setup-ota-mobile.png"), full_page=True)
            page.set_viewport_size({"width": 1280, "height": 900})
            page.screenshot(path=str(output / "setup-ota-desktop.png"), full_page=True)
            assert not errors, errors
            browser.close()
        print("PASS: OTA UI, five widths, USB migration state, image/password checks, dirty settings, failure retry, progress and serialized upload/restart.")
    finally:
        server.shutdown(); server.server_close()


if __name__ == "__main__":
    main()
