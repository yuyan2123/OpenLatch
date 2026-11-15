import argparse
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("provision", ROOT / "tools/provision.py")
provision = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provision)


class ProvisionTests(unittest.TestCase):
    def test_reject_invalid_addresses_and_channels(self):
        for value in ["", "00:00:00:00:00:00", "ff:ff:ff:ff:ff:ff", "01:02:03:04:05:06", "02:03:04:05:06"]:
            with self.assertRaises(argparse.ArgumentTypeError):
                provision.mac(value)
        for value in ["", "0", "14", "1,2,99", "abc"]:
            with self.assertRaises(argparse.ArgumentTypeError):
                provision.channel_mask(value)
        self.assertEqual(provision.channel_mask("1,1,6,11"), 0x421)

    def test_matching_profiles_and_no_accidental_rotation(self):
        indoor = provision.mac("02:11:22:33:44:55")
        outdoor = provision.mac("02:66:77:88:99:aa")
        with tempfile.TemporaryDirectory() as directory:
            paths = provision.generate(directory, indoor, outdoor, 0x421)
            header = paths[0].read_text()
            profile = json.loads(paths[1].read_text())
            self.assertEqual(profile["channels"], [1, 6, 11])
            self.assertEqual(profile["outdoor_station_mac"], "02:66:77:88:99:aa")
            for key, size in [("pmk", 16), ("lmk", 16), ("hmac_key", 32)]:
                value = bytes.fromhex(profile[key])
                self.assertEqual(len(value), size)
                self.assertIn(", ".join(f"0x{x:02x}" for x in value), header)
            self.assertNotEqual(profile["pmk"], profile["lmk"])
            with self.assertRaises(FileExistsError):
                provision.generate(directory, indoor, outdoor, 1)
            self.assertEqual(paths[0].read_text(), header)
            provision.generate(directory, indoor, outdoor, 1, force=True)
            self.assertNotEqual(paths[0].read_text(), header)
        with self.assertRaises(ValueError):
            provision.generate("unused", indoor, indoor, 1)


if __name__ == "__main__":
    unittest.main()
