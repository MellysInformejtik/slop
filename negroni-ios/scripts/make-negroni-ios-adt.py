#!/usr/bin/env python3
"""Adapt UniXNU payload arguments to the XNU-8796 negroni ADT generator."""
import argparse
import os
from pathlib import Path
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--dram-size', type=lambda v: int(v, 0), required=True)
p.add_argument('--owned-memory-size', type=lambda v: int(v, 0), required=True)
p.add_argument('--unique-chip-id', required=True)
a = p.parse_args()
if a.dram_size != a.owned_memory_size or a.dram_size != 0xc0000000:
    p.error('this profile requires matching 3 GiB DRAM and DarwinBoot windows')
try:
    ecid = int(a.unique_chip_id, 16)
except ValueError:
    p.error('--unique-chip-id must be hexadecimal')
if not 0 < ecid < 1 << 64:
    p.error('--unique-chip-id must fit a nonzero uint64')

# UniXNU copies scripts into its generated tree; locate the original repository
# explicitly there, or infer it when this script is run in the source checkout.
repo = Path(os.environ.get('IOS_SWEET_ROOT', Path(__file__).resolve().parents[2]))
source = repo / 'negroni-ios/tools'
if not (source / 'mkadt.py').is_file():
    p.error('set IOS_SWEET_ROOT to the original ios-sweet checkout')
sys.path.insert(0, str(source))
import mkadt

mkadt.NEGRONI_ECID = ecid
root = mkadt.build(board='negroni', dram=(0x840000000, a.dram_size),
                   ufs=True, darwinboot=True)
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_bytes(mkadt.adt.serialize(root))
print(f'{a.output}: negroni XNU-8796 ADT, UFS enabled, 3 GiB window')
