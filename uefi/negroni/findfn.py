#!/usr/bin/env python3
"""Find the code at ADDR of r0q module ORIG in TARGET by masked instruction match. Usage: findfn.py ORIG TARGET ADDR"""
import importlib.util, struct, sys
spec = importlib.util.spec_from_file_location('pp', sys.argv[0].rsplit('/', 1)[0] + '/masklib.py')
pp = importlib.util.module_from_spec(spec); spec.loader.exec_module(pp)
o, t = open(sys.argv[1], 'rb').read(), open(sys.argv[2], 'rb').read()
addr = int(sys.argv[3], 0)
W = lambda b, i: struct.unpack_from('<I', b, i)[0]
tw = [pp.mask(W(t, k)) for k in range(0, len(t) - 3, 4)]
for L in (32, 24, 16, 12, 8, 6):
    pat = [pp.mask(W(o, addr + 4*k)) for k in range(L)]
    hits = [k*4 for k in range(len(tw) - L) if tw[k:k+L] == pat]
    print(L, [hex(h) for h in hits[:6]])
    if len(hits) == 1: break
