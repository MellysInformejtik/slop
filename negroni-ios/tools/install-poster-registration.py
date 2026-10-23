#!/usr/bin/env python3
"""Install the iOS 16 wallpaper-provider registration helper into a mounted offline root.

Usage: install-poster-registration.py MOUNTED_ROOT SIGNED_HELPER
Preserves the existing providers, app databases and wallpaper selections. It adds
missing provider entries to launchd's AppExtensions table, including moved staged apps.
"""
import os
import plistlib
import shutil
import struct
import sys
from pathlib import Path


def install(root, helper):
    root, helper = Path(root).resolve(), Path(helper).resolve()
    with helper.open('rb') as binary:
        header = binary.read(8)
    if len(header) != 8 or struct.unpack('<II', header) != (0xfeedfacf, 0x100000c):
        raise ValueError('Helper must be an ARM64 Mach-O executable')
    version = plistlib.loads((root / 'System/Library/CoreServices/SystemVersion.plist').read_bytes())
    if not version['ProductVersion'].startswith('16.'):
        raise ValueError('This installer targets iOS 16 only')
    launchd = root / 'System/Library/xpc/launchd.plist'
    data = plistlib.loads(launchd.read_bytes())
    providers = []
    for tree in ('Applications', 'System/Library'):
        for path in sorted((root / tree).rglob('*.appex')):
            if not path.is_dir() or not (path / 'Info.plist').is_file():
                continue
            info = plistlib.loads((path / 'Info.plist').read_bytes())
            if info.get('NSExtension', {}).get('NSExtensionPointIdentifier') != 'com.apple.posterkit.provider':
                continue
            if not path.resolve().is_relative_to(root):
                raise ValueError(f'Provider escapes image root: {path}')
            executable = info.get('CFBundleExecutable', '')
            identifier = info.get('CFBundleIdentifier', '')
            if not executable or Path(executable).name != executable or not identifier.startswith('com.apple.'):
                raise ValueError(f'Invalid provider metadata: {path}')
            if not (path / executable).is_file() or not (path / executable).resolve().is_relative_to(root):
                raise ValueError(f'Missing provider executable: {path}')
            absolute = '/' + str(path.relative_to(root))
            providers.append({'Path': absolute, 'BundleIdentifier': identifier})
            data.setdefault('AppExtensions', {})[absolute] = {
                '_executablePath': absolute + '/' + executable, '_infoPlist': info,
            }
    if not providers:
        raise ValueError('No wallpaper providers found; root not changed')
    if len({p['BundleIdentifier'] for p in providers}) != len(providers):
        raise ValueError('Duplicate wallpaper provider identifiers')
    destination = root / 'usr/local/libexec/negroni-poster-register'
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(helper, destination)
    os.chmod(destination, 0o755)
    os.chown(destination, 0, 0)
    manifest = root / 'usr/local/share/negroni/poster-providers.plist'
    manifest.parent.mkdir(parents=True, exist_ok=True)
    manifest.write_bytes(plistlib.dumps(providers, fmt=plistlib.FMT_BINARY))
    os.chmod(manifest, 0o644)
    os.chown(manifest, 0, 0)
    data['LaunchDaemons']['/System/Library/LaunchDaemons/com.negroni.poster-registration.plist'] = {
        'Label': 'com.negroni.poster-registration',
        'ProgramArguments': ['/usr/local/libexec/negroni-poster-register', '/usr/local/share/negroni/poster-providers.plist'],
        'RunAtLoad': True, 'UserName': 'mobile',
        'StandardOutPath': '/dev/console', 'StandardErrorPath': '/dev/console',
    }
    backup = launchd.with_name(launchd.name + '.before-poster-registration')
    if not backup.exists():
        shutil.copy2(launchd, backup)
        os.chown(backup, launchd.stat().st_uid, launchd.stat().st_gid)
    launchd.write_bytes(plistlib.dumps(data, fmt=plistlib.FMT_BINARY))
    print(f'Installed registration helper for {len(providers)} providers')
    for provider in providers:
        print(provider['BundleIdentifier'], provider['Path'])


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    install(*sys.argv[1:])
