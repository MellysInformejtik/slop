# Negroni Adreno 730 for Windows ARM64

Status: experimental ACPI, incomplete WDDM miniport and native ARM64 user-mode
transport built; no usable Windows GPU driver has been built or installed.

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
2. Use the Microsoft render-only graphics sample as the model: it reports
   WDDM 1.3 and keeps VidPN sources/targets at zero while providing the full
   render KMD and paired D3D11 UMD. Port that model to ARM64/Adreno, then verify
   the legacy WDDM 1.3 adapter loads on this Windows 11 23H2 build before any
   GPU startup. The existing Basic Display / firmware framebuffer must remain
   the boot and recovery display.
3. Validate firmware loading, power/clock votes, GMU/HFI, SMMU mappings, and a
   bounded CP submit/fence test before exposing render acceleration.
4. Add a Mesa Freedreno D3D10 user-mode driver and a WDDM winsys, then test
   simple clear/copy/draw before allowing DWM to use the GPU.

## WDDM direction

The installed Windows 11 23H2 build supports WDDM 3.1, but a render-only GPU
need not take over display ownership or implement display outputs. Microsoft's
`graphics-driver-samples/render-only-sample` is a working reference that
reports `DXGKDDI_WDDMv1_3`, returns zero VidPN sources and children, and pairs a
render KMD with a D3D11 UMD. Microsoft's WDDM 1.2 docs describe the original
render-only type, while the sample demonstrates the WDDM 1.3 model. This makes
a legacy render-only route a concrete option; whether its KMD/UMD loads and
creates a device on this Windows 11 ARM64 image remains to be tested. The
prototype uses a WDDM 2.7 DDI interface but reports WDDM 1.3 adapter caps,
following the sample. This is only scaffolding: its callback table is
incomplete and does not make it a valid adapter. Keep Basic Display / firmware framebuffer as the recovery and
boot display. The stage-0 KMDF probe is only a resource-contract diagnostic;
MCDM remains compute-only and is not the target.

The real KMD must use `DRIVER_INITIALIZATION_DATA`/`DxgkInitialize` and implement
the render-specific WDDM DDIs, including allocation, paging, context, command
submission, interrupt/fence, reset, and timeout paths. The UMD must speak the
Windows D3D runtime ABI and submit work through that KMD. An early, deliberately
incomplete `DRIVER_INITIALIZATION_DATA` prototype now validates/maps the MMIO
resources through Dxgkrnl and gates its read-only register preflight on clock
and power state. It registers AddDevice/StartDevice/StopDevice/RemoveDevice,
QueryAdapterInfo and Escape; it is not a loadable or usable
WDDM adapter and is not staged or installed. Its DDI interface version is
WDDM 2.7 so it can use the pre-start `WDDMDEVICECAPS` query; that query now
reports WDDM 1.3 adapter caps to match the render-only sample. This is
prototype metadata only and does not implement a validated render-only
adapter. Do not install it. Next port the WDDM 1.3
render-only callback/capability pattern and paired UMD, then establish OS
adapter-creation compatibility before hardware initialization.

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
StopDevice/RemoveDevice release them. The experimental INF now creates that
key value and an offline package is validated below. After package validation, StartDevice has a read-only
power-state preflight: it reads the GCC GPU AHB status, and only if that clock
is on does it read GPUCC state; it reads the GMU counter only when CX and the
GMU clock report on. Every access is range/alignment checked, and this path
does not power the GPU, call TrustZone, or write registers. It does not enable
rendering.

`NegroniGpuPm4.c` encodes bounded A7xx type-7 command and type-4 register-write
packets, including the odd-parity fields confirmed in the XNU CP bring-up. Its
host tests check exact CP_ME_INIT and type-4 headers, parity, payload copies,
and rejection of invalid opcodes/registers and undersized output. It is linked
into the ARM64 prototype but is not yet connected to a command ring or WDDM
submission DDI. Run `test-host.sh` in WSL; it runs the PM4, queue, and firmware
parser suites under ASan/UBSan against the local phone blobs.

`NegroniGpuQueue.c` adds CPU-only ring accounting (single-empty-slot capacity,
wrap-safe writes, and modulo hardware-RPTR reconciliation) plus monotonic fence
issue/completion bookkeeping. The helper is explicitly caller-serialized and
does not access MMIO or synchronize against real Dxgkrnl callbacks yet. Host
tests cover wrap, overrun, invalid/future RPTR, and fence ordering. It is linked
into the prototype but is not yet used by `DxgkDdiSubmitCommand` or an interrupt
routine.

Do not install an A610 driver pack on this phone or change the boot display
path until the recovery path and GPU-off behavior have been tested. Keep
private firmware dumps out of git.

## Current implementation checkpoint

### Native Windows user-mode transport

`NegroniGpuAbi.h/.c` defines a 64-byte, pointer-free v1 query protocol.
`DxgkDdiEscape` accepts only an adapter-level metadata query with zero escape
flags, no device/context handle, an exact size/version/operation and zero
reserved fields. It snapshots and validates the request before changing any
output. It reports the **build target** A730 and whether StartDevice validated
resources/firmware; it does not claim that the physical GPU was identified or
that rendering works. No command execution or MMIO is exposed by this query.

`NegroniGpuWinsys.c` is the first Windows user-mode transport component. It
opens a caller-selected adapter LUID through `D3DKMTOpenAdapterFromLuid`, sends
the query through `D3DKMTEscape`, checks the complete response and closes the
adapter on failure. This is **not yet a Mesa Freedreno winsys or D3D UMD**:
buffer allocation, mappings, submission, fences, Gallium integration and the
Direct3D runtime ABI remain unimplemented. `NgGpuWinsysCanRender` returns false
for the current protocol, including after a successful handshake.

Build and test from WSL:

```sh
bash windows/NegroniGpu/test-host.sh
bash windows/NegroniGpu/test-winsys.sh
bash windows/NegroniGpu/build-winsys.sh
bash windows/NegroniGpu/build-wddm-prototype.sh
```

The native x64 transport test runs on the development PC with **mock D3DKMT
calls**. It exercises rejected opens, failed escapes, malformed replies,
unsupported rendering claims and handle cleanup without talking to a GPU.
The ARM64 transport archive is `out/NegroniGpuWinsys.lib`; the ARM64 diagnostic
client is `out/winsys-probe.exe`. Without arguments the client only lists
adapter LUIDs; with `HIGH_HEX LOW_HEX` it queries the selected prototype.
The incomplete KMD is still not installable, so this client has not completed
a live adapter handshake on the phone.

The client uses PE console subsystem 6.02. A test executable linked with 10.00
failed before its entry point on this PC with `0xc000007b`; relinking the same
objects with 6.02 let the native transport tests pass. This observation is for
the user-mode test executable; it does not establish a kernel-driver loader
failure or lower the target from Windows 11 ARM64.

Next: implement the real WDDM allocation/hardware-context/paging/submission/reset
contracts and an authenticated GPU/SMMU ownership path, then connect a Mesa
backend to those operations. Do not use an Escape to accept unvalidated raw
PM4 streams or physical addresses from user mode.

### Kernel resource and firmware prototype

`uefi/negroni/acpi/GpuSsdt.asl` describes an experimental `QCOM0A73`
platform device and its seven non-overlapping MMIO windows. The ranges match the Negroni
SM8450 DTB and the XNU A730 bring-up sources. `windows/NegroniGpu/NegroniGpuProbe.c`
is a native ARM64 KMDF resource-validation probe: it checks those translated
addresses and lengths without mapping registers, requesting power, or changing
display ownership. `NegroniGpuWddm.c` is an incomplete WDDM miniport prototype
that obtains translated resources using `DxgkCbGetDeviceInformation`, maps them
using `DxgkCbMapMemory`, validates the offline firmware package, and performs a
read-only power-state preflight. It has no render DDIs. Its QueryAdapterInfo
callback returns WDDM 1.3 for the pre-start `WDDMDEVICECAPS` query; it
does not return `DRIVERCAPS` or claim render support. The ARM64 KMD and offline
INF package build, but the package is explicitly marked DO NOT INSTALL and has
not been installed or booted. Interrupts are withheld until the GIC SPI-to-ACPI GSI mapping is
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

### Software device and context ownership

`NegroniGpuObjects.c` implements the CreateDevice/DestroyDevice and
CreateContext/DestroyContext callbacks. A fast mutex protects child accounting;
closing a device retains its storage until its last context is destroyed.
Objects retain runtime handles but no adapter or MMIO pointer. Device creation
requires StartDevice to have completed. Contexts accept node 0, engine mask 1,
and ordinary/system physical-address contexts; GDI, GPUVA, protected and hardware
queue contexts, PASID/process state and private context payloads are rejected.
Failures leave caller output unchanged. There is no hard-coded object count cap.

This is CPU-side ownership only. The context buffer sizes define a future staging
contract; no hardware context, address space, command validation or execution is
created. Rendering remains unadvertised and the driver remains non-installable.
Before adding hardware resources, connect objects to adapter stop/remove and
reset lifetime management; do not retain an unreferenced adapter pointer.

`test-host.sh` runs the production object code using a mock allocator and pthread
locks under ASan/UBSan. It covers allocation failures, flags/engine validation,
unchanged rejection output, child-first and parent-first destruction, and 512
children destroyed by eight threads. ARM64 cross-compilation separately checks
the actual WDK structures and DDI callback signatures. These tests do not run
Dxgkrnl or establish device readiness on Windows.

Contracts: [CreateDevice](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_createdevice),
[CreateContext](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_createcontext).

### GPU address space (SMMU stage-1 page tables)

`NegroniGpuPageTable.c` builds the page tables of the Adreno SMMU context the 3D core uses:
39-bit GPU VAs, three levels, 4 KiB pages, the same descriptors as the XNU port on the phone
(`bsd/dev/negroni_adreno.c` space_map/space_on: inner shareable, AF, nG, unprivileged, optional
read-only, MAIR index 0 = normal non-cacheable) and the TCR/TCR2/MAIR0/SCTLR values the context
bank is given there. Tables come from caller callbacks (the KMD will supply contiguous non-paged
memory and a cache-clean `Publish`); a map that meets a live page or runs out of tables is
refused and undone; unmap reports what it removed; `NgGpuPtTtbr0` gives the context's TTBR0.
It is CPU-only: it does not program the SMMU or invalidate its TLB, and nothing calls it yet.
Host tests (`test-pagetable.c`, in `test-host.sh`) check exact descriptors, rollback, boundary
table counts, argument rejection and leak-free teardown. Linked into the ARM64 prototype.
