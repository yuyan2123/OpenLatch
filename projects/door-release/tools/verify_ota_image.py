"""Check and describe a built OTA app; never connect to a device."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
PREFIX = b"OpenLatch/DoorRelease/ESP32-C3/OTA1:"
SUFFIX = bytes((1, 0xa7, 0x4c, 0x42, 0xb9, 0x67))


def verify(directory, test=False):
    directory = Path(directory)
    image = directory / "DoorRelease.ino.bin"
    data = image.read_bytes()
    rows = list(csv.reader(line for line in (directory / "partitions.csv").read_text().splitlines() if line and not line.startswith("#")))
    partitions = {row[0].strip(): row for row in rows}
    for name, offset, size in (("nvs", 0x9000, 0x5000), ("otadata", 0xe000, 0x2000),
                               ("app0", 0x10000, 0x1e0000), ("app1", 0x1f0000, 0x1e0000)):
        row = partitions.get(name)
        if not row or int(row[3], 0) != offset or int(row[4], 0) != size:
            raise ValueError("Unexpected OTA partition layout: " + name)
    if len(data) < 288 or len(data) > 0x1e0000 or data[0] != 0xe9 or struct.unpack_from("<H", data, 12)[0] != 5:
        raise ValueError("Invalid ESP32-C3 app image or OTA capacity exceeded")
    if struct.unpack_from("<I", data, 32)[0] != 0xabcd5432 or data[23] != 1 or hashlib.sha256(data[:-32]).digest() != data[-32:]:
        raise ValueError("App descriptor or appended image SHA-256 is invalid")
    if (PREFIX + (b"T" if test else b"P") + SUFFIX) not in data:
        raise ValueError("Firmware compatibility tag is missing")
    if (PREFIX + (b"P" if test else b"T") + SUFFIX) in data:
        raise ValueError("Ambiguous production/test firmware tag")
    info = (ROOT / "firmware/DoorRelease/FirmwareInfo.h").read_text()
    version = re.search(r'kFirmwareVersion\[\] = "([^"]+)"', info).group(1)
    result = dict(project="DoorRelease", version=version, chip="esp32c3", application=image.name,
                  size=len(data), max_size=0x1e0000, sha256=hashlib.sha256(data).hexdigest(), image_sha256=data[-32:].hex(),
                  test_build=test, ota_upload_allowed=not test)
    (directory / "firmware.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--test-profile", action="store_true")
    args = parser.parse_args()
    result = verify(args.directory, args.test_profile)
    print(f"Verified {result['size']} byte ESP32-C3 OTA app; production upload {'disabled' if args.test_profile else 'enabled'}.")


if __name__ == "__main__":
    main()
