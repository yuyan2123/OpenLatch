"""Run native transport/session and offline provisioning tests without hardware."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def compiler():
    for name in ("g++", "clang++"):
        if shutil.which(name):
            return [name]
    suffix = "zig.exe" if os.name == "nt" else "zig"
    for directory in (ROOT / ".tools/python/ziglang", ROOT.parent / "door-release/.tools/python/ziglang"):
        executable = directory / suffix
        if executable.exists():
            return [str(executable), "c++"]
    raise SystemExit("Install g++/clang++, or: python -m pip install --target .tools/python ziglang==0.14.1")


def main():
    output = ROOT / "build"
    output.mkdir(exist_ok=True)
    binary = output / ("reader_tests.exe" if os.name == "nt" else "reader_tests")
    libraries = ["-lbcrypt"] if os.name == "nt" else ["-lcrypto"]
    subprocess.run(compiler() + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-O1",
                   "-I", str(ROOT / "include"), str(ROOT / "tests/reader_tests.cpp"),
                   "-o", str(binary)] + libraries, check=True, cwd=ROOT)
    subprocess.run([str(binary)], check=True, cwd=ROOT)
    subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", "test_*.py"], check=True, cwd=ROOT)


if __name__ == "__main__":
    main()
