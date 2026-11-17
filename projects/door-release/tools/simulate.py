"""Compile/run the SAME C++ controller used by ESP32. No attached device needed."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def compiler():
    if shutil.which("g++"):
        return ["g++"]
    if shutil.which("clang++"):
        return ["clang++"]
    zig = ROOT / ".tools/python/ziglang" / ("zig.exe" if os.name == "nt" else "zig")
    if zig.exists():
        return [str(zig), "c++"]
    raise SystemExit(
        "Install g++/clang++, or run:\n"
        "python -m pip install --target .tools/python ziglang==0.14.1"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["test", "demo", "interactive"], default="demo", nargs="?")
    args = parser.parse_args()
    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    testing = args.mode == "test"
    source = ROOT / ("tests/controller_tests.cpp" if testing else "sim/main.cpp")
    executable = build / (("controller_tests" if testing else "door_sim") + (".exe" if os.name == "nt" else ""))
    subprocess.run(compiler() + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-O1", str(source), "-o", str(executable)], cwd=ROOT, check=True)
    result = subprocess.run([str(executable)] + ([] if testing else ["--" + args.mode]), cwd=ROOT)
    if result.returncode or not testing:
        return result.returncode
    nfc_exe = build / ("nfc_tests.exe" if os.name == "nt" else "nfc_tests")
    subprocess.run(compiler() + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-mismatched-tags", "-O1",
                   "-I", str(ROOT / "tests/fakes"),
                   "-I", str(ROOT.parent / "outdoor-reader/include"),
                   "-I", str(ROOT.parents[1] / "third_party/DigitalDoorKey/include"),
                   str(ROOT / "tests/nfc_tests.cpp"), "-o", str(nfc_exe)], cwd=ROOT, check=True)
    result = subprocess.run([str(nfc_exe)], cwd=ROOT)
    if result.returncode:
        return result.returncode
    ota_exe = build / ("ota_tests.exe" if os.name == "nt" else "ota_tests")
    subprocess.run(compiler() + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-O1",
                   "-I", str(ROOT / "tests/fakes"), str(ROOT / "tests/ota_tests.cpp"), "-o", str(ota_exe)], cwd=ROOT, check=True)
    result = subprocess.run([str(ota_exe)], cwd=ROOT)
    if result.returncode:
        return result.returncode
    driver_exe = build / ("driver_tests.exe" if os.name == "nt" else "driver_tests")
    subprocess.run(compiler() + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-O1",
                   "-I", str(ROOT / "tests/fakes"), str(ROOT / "tests/driver_tests.cpp"),
                   "-o", str(driver_exe)], cwd=ROOT, check=True)
    result = subprocess.run([str(driver_exe)], cwd=ROOT)
    if result.returncode:
        return result.returncode
    ir_exe = build / ("infrared_tests.exe" if os.name == "nt" else "infrared_tests")
    subprocess.run(compiler() + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-O1",
                   "-I", str(ROOT / "tests/fakes"), str(ROOT / "tests/infrared_tests.cpp"),
                   "-o", str(ir_exe)], cwd=ROOT, check=True)
    result = subprocess.run([str(ir_exe)], cwd=ROOT)
    if result.returncode:
        return result.returncode
    setup_exe = build / ("setup_tests.exe" if os.name == "nt" else "setup_tests")
    subprocess.run(compiler() + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-O1",
                   "-I", str(ROOT / "tests/fakes"), str(ROOT / "tests/setup_tests.cpp"),
                   "-o", str(setup_exe)], cwd=ROOT, check=True)
    result = subprocess.run([str(setup_exe)], cwd=ROOT)
    if result.returncode:
        return result.returncode
    portal_exe = build / ("portal_tests.exe" if os.name == "nt" else "portal_tests")
    subprocess.run(compiler() + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-O1",
                   "-I", str(ROOT / "tests/fakes"), str(ROOT / "tests/portal_tests.cpp"),
                   "-o", str(portal_exe)], cwd=ROOT, check=True)
    return subprocess.run([str(portal_exe)], cwd=ROOT).returncode


if __name__ == "__main__":
    sys.exit(main())
