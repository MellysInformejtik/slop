# negroni-ios: iOS 16 (iPhone X userland) on the OnePlus 10 Pro 5G (SM8450)

Standalone XNU port for negroni, booted by Mu-Silicium from internal storage the way Windows is:
no UniXNU and no DarwinBoot.

- `xnu/patches/`: xnu-8796 (macOS 13.5 / iOS 16 generation) patch set, imported from the ios-sweet
  (Redmi Note 10 Pro, SM7150) port with `tools/rename-from-sweet.py` and adapted to SM8450 from here on.
  Machine config `NEGRONI`, board `ARM64_BOARD_CONFIG_NEGRONI`.
- Build: `.github/workflows/negroni-ios.yml` (blacktop/darwin-xnu-build, macOS 14 runner), artifact
  `negroni-ios-kernel` (`kernel.development.negroni`).

Import check (2026-10-21): applied to a clean xnu-8796.141.3, the renamed set gives exactly the sweet
patched tree with sweet -> negroni (only Apple's own "sweet spot" comments differ, untouched by the patches).

Plan:
1. Kernel builds as NEGRONI (this import).
2. SM8450 drivers: UFS (Mu leaves the host up, like ABL did on sweet), SPMI keys (negroni PMK8350 / PM8350),
   framebuffer from Mu's GOP, timer/GIC, PSCI SMP; drop the SM7150 GENI touch.
3. EFI loader started by Mu (kernel + ADT on the boot FAT volume), ADT for negroni.
4. iOS 16 root (iPhone X 20H392) on userdata.

## Boot chain

ABL -> boot shim -> Mu-Silicium (negroniPkg) -> `\EFI\BOOT\BOOTAA64.EFI` on the boot FAT volume =
`loader/NegroniXnuBoot` (built in the Mu tree as negroniPkg/Applications/NegroniXnuBoot) -> reads
`\negroni\payload.bin` (tools/mkpayload.py: kernel + ADT + cmdline, magic `NEGRONIX`), checks that the
kernel window is plain RAM, ExitBootServices, copies kernel/ADT/boot_args (framebuffer from GOP), MMU off,
jumps with x0 = boot_args.

ADT: `tools/mkadt.py OUT --board negroni --ufs --dram BASE:SIZE` (iPhone X /product from the D22AP tree in
research/ipsw-x, not in git).
