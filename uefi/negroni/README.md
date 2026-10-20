# Mu-Silicium UEFI for negroni (work in progress)

`negroniPkg` is a copy of Mu-Silicium's `Platforms/Samsung/r0qPkg` (Galaxy S22, SM8450/WaipioPkg) with these changes:
- OnePlus SMBIOS strings, 1080x2412 framebuffer, and the mainline negroni DTB in `FdtBlob/`.
- The Samsung DXEs and the Samsung `SEC Debug` region at 0x800000000 removed. On negroni that range is free RAM per ABL `/memory`.
- `SecPagePoolCount` 0x1800 (OPLUS uefiplat.cfg).
The memory map otherwise matches negroni's own `uefiplat.cfg` (checked on 2026-10-20).

The binaries come from the phone's `uefi_a` (BOOT.MXF.2.0-00925.2-WAIPIO-1). They are proprietary, so they are not in git.
1. `uefiextract uefi_a.img all` (built from LongSoft/UEFITool).
2. Port the r0q patches (Device-Binaries/r0q/README.md) onto the negroni modules:
   - `portpatch2.py ORIG PATCHED TARGET OUT` ports code patches by masked aarch64 context match.
   - `findfn.py` locates functions.
   Result:
   - ClockDxe 0xedd0, SPMIDxe 0x2e80, UsbConfigDxe 0x51d8: ported by context.
   - PmicDxe 0x3e70/0x3e84: the min-init calls are now nops.
   - UFSDxe 0xcad0: `mov w1,#1; bl 0xe014` (wakeup call; the target was located by hand).
   - TzDxeLA: byte 0xf55c set to 1 (the global accessed via `[x22,#1372]`).
   - UsbMsdDxe: byte 0x40d1 set to 0.
   - **Not ported:** the r0q PmicDxe power-button check at 0x131c8. OnePlus's PmicDxe has no matching function. If UEFI shuts down by itself, look here.
3. `mkbinaries.py Device-Binaries/r0q uefi_a.img.dump Mu-Silicium/Binaries/negroni patched/` mirrors the r0q INFs by FILE_GUID.

Install into Mu-Silicium:
- Copy `negroniPkg` to `Platforms/OnePlus/`.
- Copy `negroni.toml` to `Resources/Configs/`.
- Copy `Silicium-ACPI/Platforms/Samsung/r0q` to `Silicium-ACPI/Platforms/OnePlus/negroni`.
- Then run `python3 build_uefi.py -d negroni`.
