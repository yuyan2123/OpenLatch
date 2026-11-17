"""Browser checks for page switching and unsaved settings; no device required."""
from pathlib import Path
import shutil
import threading
from http.server import ThreadingHTTPServer

import test_setup_page as qa


def selected(page, name):
    qa.wait_js(page, f"document.querySelector('#tab-{name}').getAttribute('aria-selected') === 'true'")
    assert page.locator('[role="tabpanel"]:not([hidden])').count() == 1
    assert page.locator(f"#page-{name}").get_attribute("hidden") is None


def settings_posts():
    return [data for path, data in qa.REQUESTS if path == "/api/settings"]


def main():
    qa.reset()
    server = ThreadingHTTPServer(("127.0.0.1", 0), qa.Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    address = f"http://127.0.0.1:{server.server_port}/"
    browser_path = shutil.which("chrome") or "C:/Program Files/Google/Chrome/Application/chrome.exe"
    output = qa.ROOT / "build"
    output.mkdir(exist_ok=True)
    errors = []
    try:
        with qa.sync_playwright() as playwright:
            browser = playwright.chromium.launch(executable_path=browser_path, headless=True)
            context = browser.new_context(viewport={"width": 1280, "height": 900})
            page = context.new_page()
            page.on("pageerror", lambda error: errors.append(str(error)))
            page.goto(address)
            qa.wait_js(page, "!document.querySelector('#save').disabled")
            selected(page, "wifi")
            assert page.get_by_role("tab").all_text_contents() == ["Wi-Fi", "Infrared", "RFID", "Other settings"]
            for width in (320, 390, 768, 1280, 1920):
                page.set_viewport_size({"width": width, "height": 844})
                for name in ("wifi", "infrared", "rfid", "other"):
                    page.locator(f"#tab-{name}").click()
                    selected(page, name)
                    assert page.evaluate("document.documentElement.scrollWidth <= innerWidth"), (width, name)
                    assert page.evaluate("""[...document.querySelectorAll('button')].filter(b=>b.getClientRects().length).every(b=>b.clientHeight>=44 && b.scrollWidth<=b.clientWidth)"""), (width, name)
            assert page.locator("#pairing-code").is_visible()
            assert page.locator("#pairing-code").inner_text() == "592-63-814"
            page.locator("#tab-wifi").click()
            page.set_viewport_size({"width": 1280, "height": 900})
            page.screenshot(path=str(output / "setup-tabs-desktop.png"), full_page=True)

            # Reverting to the saved value does not prompt. Active tab does not prompt.
            page.locator("#ssid").fill("Temporary")
            page.locator("#tab-wifi").click()
            assert not page.locator("#unsaved-dialog").is_visible()
            page.locator("#ssid").fill("Home Network")
            page.locator("#tab-rfid").click()
            selected(page, "rfid")
            assert page.locator("#rfid-heading").is_visible()
            assert page.locator("#nfc-max-failures").input_value() == "5"
            assert not page.locator("#unsaved-dialog").is_visible()
            page.locator("#tab-wifi").click()

            # Cancel and Escape keep both the form and current selection.
            page.locator("#ssid").fill("Keep these changes")
            page.locator("#tab-infrared").click()
            assert page.locator("#unsaved-dialog").is_visible()
            assert page.locator("#unsaved-cancel").evaluate("element => element === document.activeElement")
            page.locator("#unsaved-cancel").click()
            selected(page, "wifi")
            assert page.locator("#ssid").input_value() == "Keep these changes"
            page.locator("#tab-rfid").click()
            page.keyboard.press("Escape")
            qa.wait_js(page, "!document.querySelector('#unsaved-dialog').open")
            selected(page, "wifi")
            assert page.locator("#ssid").input_value() == "Keep these changes"

            # Discard restores all values, including disabled password fields.
            page.locator("#wifi-password").fill("PasswordToDiscard")
            page.locator("summary", has_text="Setup hotspot password").click()
            page.locator("#setup-password").fill("HotspotToDiscard")
            page.locator("#open-network").check()
            page.locator("#tab-other").click()
            page.locator("#unsaved-discard").click()
            selected(page, "other")
            assert page.locator("#ssid").input_value() == "Home Network"
            assert page.locator("#wifi-password").input_value() == ""
            assert page.locator("#setup-password").input_value() == ""
            assert not page.locator("#open-network").is_checked()
            assert not settings_posts()

            # Each editable field alone is sufficient to protect unsaved data.
            for field, value in [("wifi-password", "Password123"), ("setup-password", "HotspotPassword123"), ("open-network", True)]:
                page.locator("#tab-wifi").click()
                if value is True:
                    page.locator("#" + field).check()
                else:
                    page.locator("#" + field).fill(value)
                page.locator("#tab-infrared").click()
                assert page.locator("#unsaved-dialog").is_visible(), field
                page.locator("#unsaved-discard").click()
                selected(page, "infrared")

            # IR changes still persist immediately and need no separate save.
            page.locator("#learn").click()
            qa.wait_js(page, "document.querySelector('#ir-state').textContent === 'Learning in progress'")
            page.locator("#cancel").click()
            qa.wait_js(page, "!document.querySelector('#learn').disabled")
            page.locator("#tab-other").click()
            selected(page, "other")
            assert not page.locator("#unsaved-dialog").is_visible()

            # Manual tab activation: arrows move focus; Enter activates/asks.
            page.locator("#tab-wifi").click()
            page.locator("#ssid").fill("Keyboard draft")
            page.locator("#tab-wifi").focus()
            page.keyboard.press("ArrowRight")
            assert page.locator("#tab-infrared").evaluate("element => element === document.activeElement")
            selected(page, "wifi")
            page.keyboard.press("Enter")
            assert page.locator("#unsaved-dialog").is_visible()
            page.keyboard.press("Escape")
            qa.wait_js(page, "document.activeElement.id === 'tab-wifi'")
            assert page.locator("#tab-wifi").get_attribute("tabindex") == "0"
            assert page.locator("#tab-infrared").get_attribute("tabindex") == "-1"

            # Failure stays on the source page and keeps input; never auto-replay save.
            qa.SAVE_FAILURE = True
            page.locator("#tab-infrared").click()
            page.locator("#unsaved-save").click()
            qa.wait_js(page, "document.querySelector('#notice-text').textContent.includes('Storage unavailable')")
            selected(page, "wifi")
            assert page.locator("#ssid").input_value() == "Keyboard draft"
            assert len(settings_posts()) == 1
            page.locator("#tab-rfid").click()
            assert page.locator("#unsaved-dialog").is_visible()
            page.locator("#unsaved-cancel").click()
            qa.SAVE_FAILURE = False

            # Native field validation happens before saving or changing page.
            page.locator("#setup-password").fill("short")
            page.locator("#tab-infrared").click()
            page.locator("#unsaved-save").click()
            qa.wait_js(page, "!document.querySelector('#unsaved-dialog').open")
            selected(page, "wifi")
            assert len(settings_posts()) == 1
            assert page.locator("#setup-password").evaluate("element => !element.validity.valid")
            page.locator("#setup-password").fill("")

            # Closed access cannot save, but cancel/discard navigation still works.
            qa.EXPIRED = True
            qa.wait_js(page, "document.querySelector('#window').textContent === 'Closed'")
            page.locator("#tab-other").click()
            assert page.locator("#unsaved-save").is_disabled()
            page.locator("#unsaved-cancel").click()
            qa.EXPIRED = False
            page.locator("#retry").click()
            qa.wait_js(page, "!document.querySelector('#save').disabled")

            # Successful save happens once before selecting the destination page.
            page.set_viewport_size({"width": 390, "height": 844})
            page.locator("#tab-infrared").click()
            page.screenshot(path=str(output / "setup-tabs-unsaved-mobile.png"), full_page=True)
            page.locator("#unsaved-save").click()
            qa.wait_js(page, "document.querySelector('#window').textContent === 'Restarting'")
            selected(page, "infrared")
            assert len(settings_posts()) == 2
            assert settings_posts()[-1] == {"ssid": ["Keyboard draft"], "wifi_password": [""], "setup_password": [""], "relay_pulse_ms": ["200"], "nfc_max_failures": ["5"], "nfc_lockout_seconds": ["60"]}
            assert page.evaluate("!unsavedSettings()")
            page.locator("#tab-wifi").click()
            selected(page, "wifi")
            assert not page.locator("#unsaved-dialog").is_visible()
            assert not errors, errors
            browser.close()
        print("PASS: four tabs/five widths; cancel, Escape, discard, validation, failed/successful save, expiry, keyboard navigation and immediate IR persistence.")
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    main()
