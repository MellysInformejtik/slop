"""Regression checks against the pinned real clone; never modify that clone."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("backport", Path(__file__).with_name("backport-metal.py"))
backport = importlib.util.module_from_spec(spec)
spec.loader.exec_module(backport)
ROOT = Path(sys.argv.pop(1)).resolve()


class BackportTests(unittest.TestCase):
    def test_real_sources_and_idempotence(self):
        plan = backport.prepare(ROOT)
        self.assertEqual(len(plan), 5)
        for path, current, patched in plan:
            self.assertEqual(current, patched, str(path))
        plugin = (ROOT / "drivers/NativeMetal/NativeAdrenoMetalPlugin.mm").read_text()
        frontend = (ROOT / "drivers/NativeMetal/NativeBridgeMetalFrontend.mm").read_text()
        for token in ("vm_write(", "sampleDisplay(", "0x189651efc", "0x1f4919c20"):
            self.assertNotIn(token, plugin)
        for token in ("vm_read_overwrite(", "0x143214", "0x1f491b3c5", "__builtin_frame_address"):
            self.assertNotIn(token, frontend)
        self.assertIn("backtrace(frames,24)", frontend)
        self.assertIn('requestedPath("NativeAdrenoMetal.publish"', plugin)
        self.assertIn("return device = clientDevice(port)", plugin)

    def test_deployment_consistent(self):
        for name in ("build-adreno-metal-plugin.py", "build-native-metal-bridge.py", "build-turnip-darwin.py"):
            source = (ROOT / "scripts" / name).read_text()
            self.assertNotIn("ios17.0", source)
            self.assertNotIn("'17.0'", source)
            self.assertIn("16.0", source)

    def test_unrelated_edits_refused_before_any_write(self):
        # A miniature Git clone exercises the same revision/input validation;
        # source contents come from the exact upstream commits, not mocks.
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            pins = {}
            for sub in backport.PINS:
                repo = root / sub
                repo.mkdir(parents=True, exist_ok=True)
                subprocess.run(["git", "init", "-q", str(repo)], check=True)
                names = (["NativeAdrenoMetalPlugin.mm", "NativeBridgeMetalFrontend.mm"] if sub else
                         ["scripts/build-adreno-metal-plugin.py", "scripts/build-native-metal-bridge.py", "scripts/build-turnip-darwin.py"])
                for name in names:
                    path = repo / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(backport.git(ROOT / sub, "show", f"HEAD:{name}"))
                backport.git(repo, "add", ".")
                backport.git(repo, "-c", "user.name=test", "-c", "user.email=test@example.invalid", "commit", "-qm", "fixture")
                pins[sub] = backport.git(repo, "rev-parse", "HEAD").decode().strip()
            original_pins = backport.PINS
            backport.PINS = pins
            try:
                plan = backport.prepare(root)
                victim = root / "drivers/NativeMetal/NativeBridgeMetalFrontend.mm"
                victim.write_bytes(victim.read_bytes() + b"\n// unrelated user change\n")
                before = {p: p.read_bytes() for p, _, _ in plan}
                with self.assertRaisesRegex(ValueError, "unrelated edits"):
                    backport.prepare(root)
                for path, data in before.items():
                    self.assertEqual(path.read_bytes(), data)
                backport.PINS = {**pins, "": "0" * 40}
                with self.assertRaisesRegex(ValueError, "expected"):
                    backport.prepare(root)
            finally:
                backport.PINS = original_pins


if __name__ == "__main__":
    unittest.main()
