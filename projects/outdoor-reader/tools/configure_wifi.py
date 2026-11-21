"""Write a private outdoor Wi-Fi profile from a local console."""
import argparse
import getpass
import os
from pathlib import Path
import re
import warnings


def configure(ssid, password):
    ssid, password = ssid.encode("utf-8"), password.encode("utf-8")
    if not 1 <= len(ssid) <= 32 or b"\0" in ssid:
        raise ValueError("SSID must use 1–32 bytes.")
    if password and not (
        8 <= len(password) <= 63 and all(32 <= value <= 126 for value in password)
        or re.fullmatch(rb"[0-9a-fA-F]{64}", password)
    ):
        raise ValueError("Use an 8–63 character Wi-Fi password, a 64-digit hex PSK, or leave it blank for an open network.")
    path = Path(__file__).resolve().parents[1] / "private/reader_wifi.h"
    path.parent.mkdir(exist_ok=True)
    temp = path.with_suffix(".tmp")
    with os.fdopen(os.open(temp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600), "w", encoding="ascii") as output:
        output.write("#pragma once\n#include <array>\n#include <cstdint>\nnamespace reader_wifi {\n")
        for name, data in (("ssid", ssid), ("password", password)):
            values = ", ".join(f"0x{value:02x}" for value in data)
            output.write(f"inline constexpr std::array<uint8_t, {len(data)}> {name}{{{values}}};\n")
        output.write("}\n")
    temp.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ssid", help="Wi-Fi network name; prompts when omitted")
    args = parser.parse_args()
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("error", getpass.GetPassWarning)
            ssid = args.ssid or input("2.4 GHz Wi-Fi network name: ")
            configure(ssid, getpass.getpass("Wi-Fi password (hidden): "))
    except (EOFError, KeyboardInterrupt, ValueError, getpass.GetPassWarning) as error:
        parser.exit(1, f"Wi-Fi profile was not saved. {error}\n")
    print("Private outdoor Wi-Fi profile saved. You can close this window.")


if __name__ == "__main__":
    main()
