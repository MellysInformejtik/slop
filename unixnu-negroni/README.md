# UniXNU-negroni

UniXNU device repository for the OnePlus 10 Pro 5G (negroni, SM8450), copied from
[UniXNU-sheng](https://github.com/UniXNU/UniXNU-sheng) (SM8550, BSD-2-Clause-Patent; see LICENSE).

What changed from sheng:
- `board.toml`:
  - board `negroni`.
  - negroni userdata LBAs.
  - DRAM size from the ABL /memory banks.
  - The SM8550 GPU, PMGR, touch and SMP archives and kernel args were removed.
  - The kit is still named `sheng`, because the copied scripts use `kits/sheng`.
- `configs/negroni_defconfig`: `XNU_SHENG_PMGR=n` and `XNU_NATIVE_ADRENO=n`.
- `scripts/make-negroni-adt.py`: negroni identity strings and CPU type. The GIC, timer and UFS are the same on SM8450.
- Not copied: the sheng submodules NativeNovatekTouch and ShengPreflight.

The firmware is not built by unixnu. It comes from `../uefi/negroni`: the ios-sweet shim plus Mu-Silicium negroniPkg, with UniXNU and DarwinBoot enabled.
CI: `.github/workflows/unixnu-negroni.yml` (macOS) builds `kernel payload`. The outputs are encrypted with the ARTIFACT_KEY secret.
