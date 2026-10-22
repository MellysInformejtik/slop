#!/usr/bin/env python3
"""Apply the iOS 16 userspace compatibility changes to the pinned UniXNU clone.

This does not install or publish a Metal device. Kernel/A730 bring-up is separate.
Reads pristine inputs from Git, refuses unrelated edits, and is idempotent.
"""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import subprocess

PINS = {
    "": "fb2bb310564ce66779e472a021881cf7c5764919",
    "drivers/NativeMetal": "6dcd4cbb69eb5625988706de42aa8ad3009b8ee7",
}


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args])


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"Expected exactly one source anchor: {old!r}")
    return text.replace(old, new, 1)


def transform(name, source):
    if name == "NativeAdrenoMetalPlugin.mm":
        begin = source.index("// Reads of the render server's own state")
        end = source.index("// Whether NativeAdrenoMetal.clients", begin)
        source = source[:begin] + (
            "// iOS 16: no 21H461 QuartzCore object offsets or global-address writes.\n"
            "// Keep the monitor hook; presentation must be validated on the target OS.\n"
            "static void reportDisplayState(Gpu &gpu) {(void)gpu;}\n"
        ) + source[end:]
    elif name == "NativeBridgeMetalFrontend.mm":
        begin = source.index("// Which of QuartzCore's paths asked for a pipeline")
        end = source.index("// What a creation cost beyond its wall time", begin)
        source = source[:begin] + '''// Image-independent caller diagnostics for iOS 16. No private object/stack offsets.
static std::string quartzCoreCallers() {
 void *frames[24];const int n=backtrace(frames,24);
 std::string out="qc";char item[32];unsigned kept=0;
 for(int i=0;i<n && kept<8;i++) {
  Dl_info info{};
  if(!dladdr(frames[i],&info) || !info.dli_fname || !strstr(info.dli_fname,"/QuartzCore"))continue;
  snprintf(item,sizeof item," %lx",(unsigned long)((uintptr_t)frames[i]-(uintptr_t)info.dli_fbase));
  out+=item;++kept;
 }
 return out;
}
''' + source[end:]
    elif name == "build-adreno-metal-plugin.py":
        source = replace_once(source, "'arm64-apple-ios17.0'", "'arm64-apple-ios16.0'")
        source = replace_once(source, "MinimumOSVersion='17.0'", "MinimumOSVersion='16.0'")
        source = source.replace("ARM64/iOS 17:", "ARM64/iOS 16 (experimental backport):")
        # Fail on accidentally imported post-iOS16 API, even when building with a newer SDK.
        source = replace_once(source, "'-fstack-protector-strong']",
                              "'-fstack-protector-strong', '-Werror=unguarded-availability-new']")
    elif name == "build-native-metal-bridge.py":
        source = replace_once(source, "IPHONEOS_DEPLOYMENT_TARGET='17.0'",
                              "IPHONEOS_DEPLOYMENT_TARGET='16.0'")
        source = source.replace("arm64-apple-ios17.0", "arm64-apple-ios16.0")
    elif name == "build-turnip-darwin.py":
        source = replace_once(source, "'arm64-apple-ios17.0'", "'arm64-apple-ios16.0'")
        source = source.replace("ARM64/iOS 17 library", "ARM64/iOS 16 library")
    else:
        raise ValueError(name)
    if name.endswith(".py"):
        ast.parse(source, filename=name)
    return source


def prepare(root):
    for sub, pin in PINS.items():
        actual = git(root / sub, "rev-parse", "HEAD").decode().strip()
        if actual != pin:
            raise ValueError(f"{sub or 'UniXNU'}: expected {pin}, got {actual}")
    plan = []
    for sub, names in {
        "": ["scripts/build-adreno-metal-plugin.py", "scripts/build-native-metal-bridge.py",
             "scripts/build-turnip-darwin.py"],
        "drivers/NativeMetal": ["NativeAdrenoMetalPlugin.mm", "NativeBridgeMetalFrontend.mm"],
    }.items():
        for name in names:
            original = git(root / sub, "show", f"HEAD:{name}")
            patched = transform(Path(name).name, original.decode()).encode()
            path = root / sub / name
            current = path.read_bytes()
            if current not in (original, patched):
                raise ValueError(f"Refusing to overwrite unrelated edits: {path}")
            plan.append((path, current, patched))
    return plan


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--unixnu", type=Path, required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--apply", action="store_true")
    mode.add_argument("--check", action="store_true")
    args = parser.parse_args()
    plan = prepare(args.unixnu.resolve())  # Validate every input before writing any file.
    if args.check and any(current != patched for _, current, patched in plan):
        raise SystemExit("Backport not applied; run with --apply first")
    if args.apply:
        for path, current, patched in plan:
            if current != patched:
                path.write_bytes(patched)
    print(json.dumps({"target": "iOS 16.0", "status": "source checks passed",
        "compiled": False, "files": {str(path.relative_to(args.unixnu.resolve())):
            hashlib.sha256(patched).hexdigest() for path, _, patched in plan}}, indent=2))


if __name__ == "__main__":
    main()
