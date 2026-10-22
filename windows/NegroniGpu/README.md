# Negroni Adreno 730 for Windows ARM64

Status: source audit complete; no Windows GPU driver has been built or installed.

Target: Windows 11 ARM64 23H2 (22631.7584) on the OnePlus 10 Pro, SM8450,
Adreno 730. The current Windows install has only the NegroniTouch package
staged; this project does not change the phone or its Windows image.

## Findings

- No ready-to-install Adreno 730 Windows package was found in this checkout or
  the Qualcomm Windows-on-ARM driver pack. The latter has an open request for
  Snapdragon 8 Gen 1, with no linked implementation or pull request.
- A Windows GPU driver needs an ARM64 WDDM kernel-mode display driver and a
  compatible user-mode driver. Android KGSL/Adreno `.so` files are not Windows
  display drivers, and test-signing does not make them loadable.
- The public Topaz WoA project documents a working Adreno 610 WDDM + Mesa
  Freedreno stack, but its hardware target is SM6225/A610 and its repository
  has no root license file. Treat it as an architectural reference only; do
  not copy its driver source without an explicit license.
- This repo already contains Negroni A730 hardware bring-up work under
  `negroni-ios/xnu/patches/0099*` through `0117*`. The handoff records GPU
  power/clock, TrustZone firmware authentication, GMU/HFI, and command
  processor progress. Those results are a hardware reference, not a Windows
  driver.
- The current `uefi/` source has no Negroni GPU ACPI device/resource node. A
  Windows driver will need a safe way to receive and map the SM8450 GPU, GMU,
  GPU-CC, SMMU, and interrupt resources.

## Port milestones

1. Define and review the ACPI resource contract for the SM8450 GPU; keep the
   existing framebuffer boot path available for recovery.
2. Build a test-signed ARM64 **render-only WDDM KMD** that enumerates without
   powering the GPU, then add a guarded hardware-start path using the proven
   Negroni sequence. Leave the system display on its existing Basic Display /
   firmware framebuffer path; do not bind the A730 as the boot display adapter.
3. Validate firmware loading, power/clock votes, GMU/HFI, SMMU mappings, and a
   bounded CP submit/fence test before exposing render acceleration.
4. Add a Mesa Freedreno D3D10 user-mode driver and a WDDM winsys, then test
   simple clear/copy/draw before allowing DWM to use the GPU.

## WDDM direction

The first acceleration target is a render-only WDDM adapter. Windows 11 23H2
supports the required model, and Microsoft documents render-only adapters as
having no VidPN sources/targets. This keeps the A730 separate from the existing
display device and preserves Basic Display as the recovery path. The stage-0
KMDF probe is only a resource-contract diagnostic; it is not the WDDM KMD and
must not be treated as an installable graphics driver. A compute-only MCDM
prototype is not the target because this port needs graphics rendering, not
only compute.

The real KMD must use `DRIVER_INITIALIZATION_DATA`/`DxgkInitialize` and implement
the render-specific WDDM DDIs, including allocation, paging, context, command
submission, interrupt/fence, reset, and timeout paths. The UMD must speak the
Windows D3D runtime ABI and submit work through that KMD. No such KMD or UMD has
been implemented yet; the next coding step is a separate render-only miniport
project, not expansion of the KMDF probe.

Do not install an A610 driver pack on this phone or change the boot display
path until the recovery path and GPU-off behavior have been tested. Keep
private firmware dumps out of git.

## Current implementation checkpoint

`uefi/negroni/acpi/GpuSsdt.asl` describes an experimental `QCOM0A73`
platform device and its eight MMIO windows. The ranges match the Negroni
SM8450 DTB and the XNU A730 bring-up sources. `windows/NegroniGpu/NegroniGpuProbe.c`
is a native ARM64 KMDF resource-validation probe: it checks those translated
addresses and lengths without mapping registers, requesting power, or changing
display ownership. It built successfully with the installed WDK. It is not a
WDDM miniport and provides no graphics acceleration. Its INF is not staged or
installed. Interrupts are withheld until the GIC SPI-to-ACPI GSI mapping is
verified. `GpuSsdt.asl` compiled cleanly with ACPICA iasl 20251212 (191-byte
AML, no diagnostics). The full UEFI build and firmware-table coexistence with
`TouchSsdt` and the existing display path still need validation before boot.

## References

- [Microsoft WDDM operation flow](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-and-later-display-driver-model-operation-flow)
- [Microsoft Windows on Arm driver requirements](https://learn.microsoft.com/en-us/windows/arm/faq)
- [Topaz WoA Adreno 610 implementation](https://github.com/MakrSas/topaz-woa)
- [Qualcomm WoA driver pack](https://github.com/edk2-porting/WOA-Drivers)
- [Qualcomm WoA driver request for Snapdragon 8 Gen 1](https://github.com/edk2-porting/WOA-Drivers/issues/101)
