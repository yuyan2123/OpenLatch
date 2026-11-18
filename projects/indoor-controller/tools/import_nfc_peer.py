"""Install the outdoor provisioner's existing profile for the indoor controller."""
import argparse
import json
import os
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def header(profile):
    if not isinstance(profile, dict):
        raise ValueError("The NFC profile must be an object")
    if profile.get("protocol") != "OpenLatch/NFC/1":
        raise ValueError("Unsupported NFC transport profile")
    fields = {}
    for source, target, length in [("indoor_station_mac", "indoorMac", 6),
                                   ("outdoor_station_mac", "outdoorMac", 6),
                                   ("pmk", "pmk", 16), ("lmk", "lmk", 16), ("hmac_key", "hmacKey", 32)]:
        value = profile.get(source, "")
        if not isinstance(value, str):
            raise ValueError("Invalid profile field: " + source)
        raw = value.replace(":", "")
        if not re.fullmatch(r"[0-9a-fA-F]{%d}" % (length * 2), raw):
            raise ValueError("Invalid profile field: " + source)
        fields[target] = bytes.fromhex(raw)
        if not any(fields[target]):
            raise ValueError("Zero profile field: " + source)
    if fields["indoorMac"] == fields["outdoorMac"] or fields["indoorMac"][0] & 1 or fields["outdoorMac"][0] & 1:
        raise ValueError("Distinct unicast station MAC addresses are required")
    channels = profile.get("channels", [])
    if not isinstance(channels, list) or not channels or any(type(n) is not int or n < 1 or n > 13 for n in channels):
        raise ValueError("Invalid channel list")
    result = "#pragma once\n#include <array>\n#include <cstdint>\nnamespace credentials {\ninline constexpr bool provisioned = true;\n"
    for name, value in fields.items():
        result += f"inline constexpr std::array<uint8_t, {len(value)}> {name}{{" + ",".join(f"0x{b:02x}" for b in value) + "};\n"
    result += "inline constexpr uint16_t channels = 0x%04x;\n}\n" % sum(1 << (n - 1) for n in set(channels))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "firmware/OpenLatch/nfc_peer.h")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    try:
        content = header(json.loads(args.profile.read_text(encoding="utf-8")))
        flags = os.O_WRONLY | os.O_CREAT | (os.O_TRUNC if args.force else os.O_EXCL)
        with os.fdopen(os.open(args.output, flags, 0o600), "w", encoding="utf-8", newline="\n") as output:
            output.write(content)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print("Installed private indoor NFC profile. Rebuild the indoor firmware.")


if __name__ == "__main__":
    main()
