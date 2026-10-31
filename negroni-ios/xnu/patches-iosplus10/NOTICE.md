# Origin of these patches

`02xx`-`04xx` and the copies of 0046, 0056 and 0061 are from iOSPlus10
(https://github.com/akshaynexus/iOSPlus10, commit 146e889, MIT License, Copyright (c) 2026
iOSPlus10 contributors), `patches/xnu` and `external/slop`. Code those patches name as
modelled on "oosexgang xnu-kernel" / "oosexgang userland" comes from oosexgang's SM7325 port
(github.com/oosexgang, MIT / APSL 2.0 as stated in each patch).

`0500-ios18-userland.patch` is our `../patches-ios18/0126-ios18-userland.patch` regenerated on
top of this series (their sandbox extension and client-memory code kept; iOS 18 lock result
layout, swap record translation, surface values, `sandbox_get_container_expected` added).
`../patches-ios18/0127` (same stub as 0201) and `0128` (already in the series) are not used;
`0129-ios18-metal-abi` is not ported yet.

Order: `../patches/0001-0126` (0127-negroni-gpu-show dropped, it collides with 0230), these
files (they replace 0046/0056/0061), then 0500.
