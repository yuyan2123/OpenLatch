"""Compile ESP32-S firmware; never upload or discover a serial port."""
import argparse
from contextlib import contextmanager
import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


@contextmanager
def tool_directory():
    directory = ROOT / ".tools/platformio"
    directory.mkdir(parents=True, exist_ok=True)
    if os.name != "nt":
        yield directory
        return
    # IDF includes very long third-party filenames. A temporary drive alias
    # avoids MAX_PATH during package extraction without changing Windows settings.
    for letter in reversed("PQRSTUVWXYZ"):
        drive = f"{letter}:"
        if os.path.exists(drive + "\\"):
            continue
        result = subprocess.run(["subst", drive, str(directory)], capture_output=True)
        if result.returncode:
            continue
        try:
            yield Path(drive + "\\")
        finally:
            subprocess.run(["subst", drive, "/D"], check=True)
        return
    raise SystemExit("No free P:-Z: drive alias for the ESP-IDF tool directory")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--require-provisioned", action="store_true")
    args = parser.parse_args()
    if args.require_provisioned and not (ROOT / "private/credentials.h").exists():
        parser.error("Run tools/provision.py with the actual board MAC addresses first")
    local = ROOT / ".tools/python"
    sys.path.insert(0, str(local))
    if importlib.util.find_spec("platformio") is None:
        parser.error("Install: python -m pip install --target .tools/python platformio==6.1.19")
    environment = dict(os.environ)
    environment["PYTHONPATH"] = str(local) + os.pathsep + environment.get("PYTHONPATH", "")
    # All build dependencies are public PyPI packages. Keep an unrelated user
    # extra-index setting from introducing failing mirrors into the local venv.
    environment["PIP_EXTRA_INDEX_URL"] = "https://pypi.org/simple"
    environment["PIP_DISABLE_PIP_VERSION_CHECK"] = "1"
    profile = ROOT / "private/credentials.h"
    fingerprint = hashlib.sha256(profile.read_bytes() if profile.exists() else b"unconfigured").hexdigest()
    state = ROOT / "build/.credentials-state"
    if not state.exists() or state.read_text() != fingerprint:
        # Re-evaluate __has_include and the private include directory after provisioning.
        (ROOT / "firmware/main/CMakeLists.txt").touch()
    with tool_directory() as directory:
        environment["PLATFORMIO_CORE_DIR"] = str(directory)
        result = subprocess.call([sys.executable, "-m", "platformio", "run", "-e", "esp32s"], cwd=ROOT, env=environment)
    if result == 0:
        state.parent.mkdir(exist_ok=True)
        state.write_text(fingerprint)
    return result


if __name__ == "__main__":
    sys.exit(main())
