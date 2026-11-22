"""Stage the vendored HomeKey sources as local Arduino libraries."""
from pathlib import Path
import shutil
import hashlib
import urllib.request
import argparse

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT.parents[1] / "third_party/DigitalDoorKey"
LIBRARIES = ROOT / ".tools/vendor-libraries"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-profile", action="store_true")
    args = parser.parse_args()
    if not (UPSTREAM / "include/ddk/session/Session.h").exists():
        raise SystemExit("Missing vendored HomeKey sources: third_party/DigitalDoorKey")
    library = LIBRARIES / "DigitalDoorKey"
    source = library / "src"
    for folder in ("include", "priv"):
        for path in (UPSTREAM / folder).rglob("*.h*"):
            target = source / path.relative_to(UPSTREAM / folder)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
    for path in (UPSTREAM / "src").rglob("*"):
        if path.suffix not in (".c", ".cpp"):
            continue
        relative = path.relative_to(UPSTREAM / "src")
        if relative.parts[0] == "aliro" or path.name.startswith(("Aliro", "Ble")):
            continue
        if path.name in ("GcmSecureChannel.cpp", "BerTlv.cpp"):
            continue
        target = source / "implementation" / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
    (library / "library.properties").write_text(
        "name=DigitalDoorKey\nversion=1.0.0\nauthor=rednblkx\nmaintainer=rednblkx\n"
        "sentence=HomeKey verification from the pinned vendored sources.\n"
        "paragraph=Indoor HomeKey verification.\ncategory=Communication\n"
        "url=https://github.com/rednblkx/DigitalDoorKey\narchitectures=esp32\n", encoding="utf-8")
    protocol = LIBRARIES / "OpenLatchNfc"
    (protocol / "src").mkdir(parents=True, exist_ok=True)
    for name in ("Protocol.h", "LanTransport.h", "TapLed.h"):
        shutil.copy2(ROOT.parent / "outdoor-reader/include" / name, protocol / "src" / name)
    (protocol / "library.properties").write_text(
        "name=OpenLatchNfc\nversion=1.0.0\nauthor=OpenLatch\nmaintainer=OpenLatch\n"
        "sentence=Authenticated NFC transport.\nparagraph=OpenLatch NFC protocol 1.\n"
        "category=Communication\nurl=https://github.com/\narchitectures=esp32\n", encoding="utf-8")
    # Arduino 3.3.0 links TinyCBOR but ships only its broken wrapper header.
    # Restore the matching public 0.6.0 header without changing the SDK.
    support = ROOT / ".tools/homekey-support"
    support.mkdir(parents=True, exist_ok=True)
    cbor = support / "cbor.h"
    digest = "70e99280844c104d391c2cd2d25888b8999a9f3f1b97c5fedc404dc58ae463a4"
    if not cbor.exists() or hashlib.sha256(cbor.read_bytes()).hexdigest() != digest:
        data = urllib.request.urlopen("https://raw.githubusercontent.com/intel/tinycbor/v0.6.0/src/cbor.h", timeout=30).read()
        if hashlib.sha256(data).hexdigest() != digest:
            raise SystemExit("TinyCBOR header checksum mismatch")
        cbor.write_bytes(data)
    (support / "tinycbor-version.h").write_text(
        "#define TINYCBOR_VERSION_MAJOR 0\n#define TINYCBOR_VERSION_MINOR 6\n#define TINYCBOR_VERSION_PATCH 0\n", encoding="utf-8")
    if args.test_profile:
        if (ROOT / "firmware/OpenLatch/nfc_peer.h").exists():
            raise SystemExit("A real NFC profile exists; use the normal provisioned build instead.")
        from import_nfc_peer import header
        fixture = ROOT / ".tools/compile-fixture"
        fixture.mkdir(parents=True, exist_ok=True)
        (fixture / "nfc_peer.h").write_text(header({
            "protocol": "OpenLatch/NFC/1", "indoor_station_mac": "02:00:00:00:00:01",
            "outdoor_station_mac": "02:00:00:00:00:02", "pmk": "01" * 16,
            "lmk": "02" * 16, "hmac_key": "03" * 32, "channels": [1, 6, 11],
        }), encoding="utf-8")
    profile = ROOT / (".tools/compile-fixture/nfc_peer.h" if args.test_profile else "firmware/OpenLatch/nfc_peer.h")
    fingerprint = hashlib.sha256(profile.read_bytes() if profile.exists() else b"unconfigured").hexdigest()
    state = ROOT / "build/.nfc-profile-state"
    if not state.exists() or state.read_text() != fingerprint:
        # __has_include changes must invalidate a previously unconfigured build.
        (ROOT / "firmware/OpenLatch/OpenLatch.ino").touch()
        state.parent.mkdir(exist_ok=True)
        state.write_text(fingerprint)


if __name__ == "__main__":
    main()
