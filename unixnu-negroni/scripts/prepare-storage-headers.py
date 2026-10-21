#!/usr/bin/env python3
"""Install the IOStorageFamily headers NativeUfs/NativeRamBlock/NativeVirtioBlock compile against.

UniXNU's audit-storage-interface.py makes build/storage-abi/include from refer/IOStorageFamily, but
neither feeds.toml nor the sheng archive list provides that source. This step fetches the same public
Darwin 23 commit and installs only the headers (and TargetConditionals.h from the macOS SDK), so a
fresh tree builds. negroni addition; runs from the build tree like the other archive builders.
"""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
COMMIT = 'e45fbe30a7879e0b16e26dddb78538f1b00e0bff'	# audit-storage-interface.py pin
source = ROOT / 'refer/IOStorageFamily'
if not (source / '.git').is_dir():
    source.mkdir(parents=True, exist_ok=True)
    subprocess.run(['git', 'init', '-q', str(source)], check=True)
    subprocess.run(['git', '-C', str(source), 'fetch', '-q', '--depth', '1',
                    'https://github.com/apple-oss-distributions/IOStorageFamily.git', COMMIT], check=True)
    subprocess.run(['git', '-C', str(source), 'checkout', '-q', 'FETCH_HEAD'], check=True)
revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
if revision != COMMIT:
    raise SystemExit(f'IOStorageFamily at {revision}, expected {COMMIT}')
out = ROOT / 'build/storage-abi/include'
headers = out / 'IOKit/storage'
headers.mkdir(parents=True, exist_ok=True)
count = 0
for path in source.glob('*.h'):
    shutil.copyfile(path, headers / path.name)
    count += 1
sdk = Path(subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip())
shutil.copyfile(sdk / 'usr/include/TargetConditionals.h', out / 'TargetConditionals.h')
print(f'storage headers: {count} from IOStorageFamily {COMMIT[:12]} -> {headers}')
