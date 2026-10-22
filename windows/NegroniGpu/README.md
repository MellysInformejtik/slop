# Negroni Adreno 730 for Windows ARM64

Status: experimental ACPI source and incomplete WDDM miniport prototype built;
no usable Windows GPU driver has been built or installed.

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
- `uefi/negroni/acpi/GpuSsdt.asl` now declares a Negroni GPU ACPI node and MMIO
  ranges. The ASL compiled, but the complete firmware build, resource
  arbitration/translation, and interrupt mapping have not been validated.

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
Windows D3D runtime ABI and submit work through that KMD. An early, deliberately
incomplete `DRIVER_INITIALIZATION_DATA` prototype now validates/maps the MMIO
resources through Dxgkrnl without accessing registers. It only registers
AddDevice/StartDevice/StopDevice/RemoveDevice; it is not a loadable or usable
WDDM adapter and is not staged or installed. The next KMD work is to implement
query/capability and render DDIs before hardware initialization.

`NegroniGpuFirmware.c` now provides allocation-free parsers for the Qualcomm
GMU block stream and the A730 ZAP ELF32 manifest. It rejects truncated blocks,
out-of-range GMU writes, malformed program-header tables, integer overflow,
and ZAP images larger than the phone's 8 KiB protected region. The parser is
compiled into the ARM64 prototype and has host ASan/UBSan tests using synthetic
valid and malformed input. It also validates the target phone's local firmware
copies in `/home/paw/ios-work/negroni-firmware/gpu`: four GMU blocks, one ZAP
load segment, and the corresponding `.bNN` payload size. These private blobs
remain outside git. `NegroniGpuFirmwareIo.c` reads files relative to a
`FirmwareDirectory` string in the device's PnP hardware key, using the package
path supplied by the INF rather than a hardcoded Driver Store path. StartDevice
validates and retains the GMU image, SQE image, ZAP manifest, and segment files;
StopDevice/RemoveDevice release them. No INF currently creates that key value,
so this is build-only. After package validation, StartDevice has a read-only
power-state preflight: it reads the GCC GPU AHB status, and only if that clock
is on does it read GPUCC state; it reads the GMU counter only when CX and the
GMU clock report on. Every access is range/alignment checked, and this path
does not power the GPU, call TrustZone, or write registers. It does not enable
rendering.

Do not install an A610 driver pack on this phone or change the boot display
path until the recovery path and GPU-off behavior have been tested. Keep
private firmware dumps out of git.

## Current implementation checkpoint

`uefi/negroni/acpi/GpuSsdt.asl` describes an experimental `QCOM0A73`
platform device and its seven non-overlapping MMIO windows. The ranges match the Negroni
SM8450 DTB and the XNU A730 bring-up sources. `windows/NegroniGpu/NegroniGpuProbe.c`
is a native ARM64 KMDF resource-validation probe: it checks those translated
addresses and lengths without mapping registers, requesting power, or changing
display ownership. `NegroniGpuWddm.c` is an incomplete render-only WDDM miniport
prototype that obtains the translated resources using `DxgkCbGetDeviceInformation`
and maps them using `DxgkCbMapMemory`; it performs no register access and has no
render DDIs. Its QueryAdapterInfo callback only returns the supported WDDM 2.7
interface version for the OS's pre-start `WDDMDEVICECAPS` query; it deliberately
does not return `DRIVERCAPS` or claim render support. Both binaries compile for
ARM64 with the installed WDK, but neither
is staged or installed. Interrupts are withheld until the GIC SPI-to-ACPI GSI mapping is
verified. `GpuSsdt.asl` compiled cleanly with ACPICA iasl 20251212 (latest
seven-window AML is validated after the GCC clock aperture was added. The GCC
range (0x00100000/0x1f4200) matches the XNU bring-up map and contains the GPU
clock registers at offsets 0x81004 through 0x81154. The Negroni UEFI register
map lists no other device aperture in this range. GPU-CC (0x03d90000/0xa000) and
CX-misc (0x03d9e000/0x1000) are subranges inside the DTB GMU aperture
0x03d6a000/0x35000 and are accessed as offsets within that single mapping,
avoiding duplicate overlapping PnP descriptors. The full UEFI build and firmware-table coexistence with
`TouchSsdt` and the existing display path still need validation before boot.

## References

- [Microsoft WDDM operation flow](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-and-later-display-driver-model-operation-flow)
- [Microsoft Windows on Arm driver requirements](https://learn.microsoft.com/en-us/windows/arm/faq)
- [Topaz WoA Adreno 610 implementation](https://github.com/MakrSas/topaz-woa)
- [Qualcomm WoA driver pack](https://github.com/edk2-porting/WOA-Drivers)
- [Qualcomm WoA driver request for Snapdragon 8 Gen 1](https://github.com/edk2-porting/WOA-Drivers/issues/101)
