"""Generate transport credentials offline. Never generates HomeKey credentials."""
import argparse
import json
from pathlib import Path
import re
import secrets

ROOT = Path(__file__).resolve().parents[1]


def mac(value):
    if not re.fullmatch(r"[0-9a-fA-F]{2}(:[0-9a-fA-F]{2}){5}", value):
        raise argparse.ArgumentTypeError("Use six colon-separated hex bytes")
    data = bytes.fromhex(value.replace(":", ""))
    if not any(data) or data[0] & 1:
        raise argparse.ArgumentTypeError("A nonzero unicast station MAC is required")
    return data


def channel_mask(value):
    try:
        channels = [int(item) for item in value.split(",")]
        if not channels or any(item < 1 or item > 13 for item in channels):
            raise ValueError
        return sum(1 << (item - 1) for item in set(channels))
    except ValueError as error:
        raise argparse.ArgumentTypeError("Channels must be comma-separated integers from 1 to 13") from error


def generate(destination, indoor, outdoor, channels, force=False):
    if indoor == outdoor:
        raise ValueError("Indoor and outdoor MAC addresses must differ")
    destination = Path(destination)
    files = [destination / "credentials.h", destination / "indoor-peer.json"]
    if not force and any(path.exists() for path in files):
        raise FileExistsError("Credentials already exist; --force explicitly rotates both profiles")
    pmk, lmk, key = secrets.token_bytes(16), secrets.token_bytes(16), secrets.token_bytes(32)

    def array(name, data):
        values = ", ".join(f"0x{byte:02x}" for byte in data)
        return f"inline constexpr std::array<uint8_t, {len(data)}> {name}{{{values}}};\n"

    header = "#pragma once\n#include <array>\n#include <cstdint>\nnamespace credentials {\n"
    header += "inline constexpr bool provisioned = true;\n"
    for name, data in [("indoorMac", indoor), ("outdoorMac", outdoor), ("pmk", pmk), ("lmk", lmk), ("hmacKey", key)]:
        header += array(name, data)
    header += f"inline constexpr uint16_t channels = 0x{channels:04x};\n}}\n"
    profile = {
        "protocol": "OpenLatch/NFC/1",
        "indoor_station_mac": indoor.hex(":"), "outdoor_station_mac": outdoor.hex(":"),
        "pmk": pmk.hex(), "lmk": lmk.hex(), "hmac_key": key.hex(),
        "channels": [n for n in range(1, 14) if channels & (1 << (n - 1))],
        "note": "Transport keys only. Never treat a reader message as a HomeKey authentication result.",
    }
    destination.mkdir(parents=True, exist_ok=True)
    for path, data in zip(files, [header, json.dumps(profile, indent=2) + "\n"]):
        # Restrictive permissions on systems that implement POSIX file modes.
        import os
        flags = os.O_WRONLY | os.O_CREAT | (os.O_TRUNC if force else os.O_EXCL)
        with os.fdopen(os.open(path, flags, 0o600), "w", encoding="utf-8", newline="\n") as output:
            output.write(data)
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--indoor-mac", required=True, type=mac)
    parser.add_argument("--outdoor-mac", required=True, type=mac)
    parser.add_argument("--channels", default="1,2,3,4,5,6,7,8,9,10,11", type=channel_mask)
    parser.add_argument("--output", type=Path, default=ROOT / "private")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    try:
        files = generate(args.output, args.indoor_mac, args.outdoor_mac, args.channels, args.force)
    except (ValueError, FileExistsError) as error:
        parser.error(str(error))
    for path in files:
        print(f"Created {path}")
    print("Keep both files private. The JSON profile is for the future indoor implementation.")


if __name__ == "__main__":
    main()
