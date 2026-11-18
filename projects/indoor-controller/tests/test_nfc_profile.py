import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("import_nfc_peer", ROOT / "tools/import_nfc_peer.py")
IMPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORTER)


def profile():
    return dict(protocol="OpenLatch/NFC/1", indoor_station_mac="02:00:00:00:00:01",
                outdoor_station_mac="02:00:00:00:00:02", pmk="12" * 16, lmk="34" * 16,
                hmac_key="56" * 32, channels=[1, 6, 11])


class NfcProfileTests(unittest.TestCase):
    def test_import_and_overwrite_guard(self):
        data = IMPORTER.header(profile())
        self.assertIn("provisioned = true", data)
        self.assertIn("channels = 0x0421", data)
        self.assertIn("0x12,0x12", data)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "peer.json"
            target = Path(directory) / "nfc_peer.h"
            source.write_text(json.dumps(profile()))
            command = [sys.executable, str(ROOT / "tools/import_nfc_peer.py"), str(source), "--output", str(target)]
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
            original = target.read_bytes()
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(target.read_bytes(), original)
            self.assertEqual(subprocess.run(command + ["--force"], capture_output=True).returncode, 0)

    def test_reject_invalid_profiles(self):
        cases = [("protocol", "other"), ("indoor_station_mac", "03:00:00:00:00:01"),
                 ("outdoor_station_mac", "00:00:00:00:00:00"), ("pmk", "00" * 16),
                 ("pmk", None), ("lmk", "short"), ("hmac_key", "gg" * 32),
                 ("channels", []), ("channels", [0]), ("channels", [14]),
                 ("channels", [True]), ("channels", 1)]
        for field, value in cases:
            data = copy.deepcopy(profile()); data[field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                IMPORTER.header(data)
        data = profile(); data["outdoor_station_mac"] = data["indoor_station_mac"]
        with self.assertRaises(ValueError):
            IMPORTER.header(data)
        with self.assertRaises(ValueError):
            IMPORTER.header([])


if __name__ == "__main__":
    unittest.main()
