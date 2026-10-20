# OnePlus 10 Pro 5G ("negroni", SM8450) port

Second board for the ios-sweet XNU port. Hardware facts below come from the
mainline device tree (XEC tree `github.com/Xlie-Electronic-Customs/linux`,
commit `476a4a8`, files `arch/arm64/boot/dts/qcom/sm8450.dtsi` and
`sm8450-oneplus-negroni.dts`). **None of it has been checked on a phone yet.**
The first boot (step 1) is a shim probe that confirms or corrects these values.

## sweet vs negroni

| | sweet (SM7150) | negroni (SM8450) | port impact |
|---|---|---|---|
| CPUs | 6x A55 + 2x A76, MPIDR `core<<8` | 4x A510 + 3x A710 + 1x X2, MPIDR `core<<8` | same PSCI / MPIDR scheme |
| Hypervisor | QHEE | Gunyah | kernel stays at EL1; probe checks GIC/PSCI behaviour |
| GICD / GICR | `0x17a00000` / `0x17a60000` (1 MiB) | `0x17100000` / `0x17180000` (2 MiB) | ADT `reg` |
| GICR stride | `0x20000` (GICv3) | `0x40000` (GICv4, VLPI frames) | patch 0082 + ADT `redistributor-stride` |
| Virtual timer | PPI 3 = INTID 19 | PPI 11 = INTID 27 | ADT `virtual-timer-intid` (patch 0017) |
| Boot framebuffer | `0x9c000000`, 1080x2400 | `0xb8000000` (`splash_region`), 1080x2412 | ADT `sweet-fbcon`; kernel display code still assumes 2400 rows |
| Panel | 1080x2400 | Samsung AMB670YF07 1440x3216 DSC (ABL scans out 1080x2412 per DT) | check on the probe photo |
| Low DDR | free above `0xb0000000` | fragmented; largest free run `0xea000000`-`0x100000000` (352 MiB) | kernel RAM: upper bank via `--dram` once known |
| UFS host | `0x1d84000` | `0x1d84000` | same driver; **off by default** (see below) |
| USB | DWC3 `0xa600000`, QUSB2 PHY `0x88e2000` | DWC3 `0xa600000`, SNPS femto HS PHY `0x88e3000`, QMP combo PHY `0x88e8000` | sweet_usb PHY code must change |
| SPMI | PMIC arbiter v5 (core `0xc440000`) | PMIC arbiter v7 (core `0xc400000`, obsrvr `0xc440000`, chnls `0xc500000`, cnfg `0xc42d000`) | sweet_keys / sweet_spmi rewrite |
| Power key / Vol+ | PM6150 PON | PMK8350 PON `0x1300` (SID 0): pwrkey + resin | new key map |
| Vol- | PM6150L GPIO2 | PM8350 GPIO6 (`0x8800` block) | new key map |
| Touch | Goodix GT9896 @0x5d, QUP1 SE7 `0xa84000` | Synaptics S3908 @0x4b, QUP0 SE4 `0x990000` (`i2c4`); reset GPIO 20, IRQ GPIO 21, 1.8 V enable GPIO 186; TLMM `0xf100000` | new touch protocol (Synaptics TouchComm) |
| Touch power | RPMh L11C | RPMh `pm8350c_l3` (VDD 3.0 V) + `pm8350c_l6` (1.8 V) | new votes |
| RPMh RSC | `0x18220000`, TCS `+0xd00` | `apps_rsc` drv-2 `0x17a20000`, TCS `+0xd00`; cmd-db `0x80860000` | new addresses |
| Battery | static (PM6150 not used) | pmic_glink via ADSP firmware | keep the static IOPMPowerSource |
| restart reason | imem `0x146aa65c` | assumed `0x146aa65c` (downstream waipio `restart_reason@65c`) | **unverified**; probe reset tests it |

## What is in the repo now

- `shim/board.h` + `BOARD=negroni shim/build.sh`: the shim with negroni constants.
  Without an xnu payload it runs a negroni probe: ABL's `/memory` ranges, MIDR,
  current EL, PSCI/SMCCC versions, PSCI CPU_ON for cpu1-7 (MPIDR each), GIC
  distributor/redistributor setup with vtimer and SGI interrupt tests, then the
  read-only UFS probe. It holds the screen for 3 minutes and resets to fastboot.
- `tools/mkadt.py --board negroni [--dram BASE:SIZE] [--ufs]`: the negroni ADT
  (GIC, stride, timer, framebuffer, 8 CPU nodes). The sweet ADTs are byte-identical
  to before (checked for phone, QEMU and `--highmem` trees).
- `xnu/patches/0082-sweet-gicr-stride-from-adt.patch`: redistributor stride read
  from the ADT (default `0x20000`, so sweet is unchanged). Not built by CI yet.

The negroni ADT has **no `/sweet-ufs` node** unless `--ufs` is given. Without
it the kernel never touches the UFS. That matters because the persistent console
log (patch 0047) writes the last 4 MiB of `userdata`, which on negroni is still
Android's data. No permission to use negroni's storage has been given.

## Step 1: shim probe (no kernel)

Needs: unlocked bootloader, `fastboot`, `aarch64-linux-gnu-gcc`, `mkbootimg`, host `gcc`.

1. Stock DTB for ABL's board match. Take `vendor_boot.img` of the installed
   OxygenOS build (from the OTA `payload.bin` with a payload dumper, or
   `adb pull` on a rooted phone), then
   `unpack_bootimg --boot_img vendor_boot.img --out vb` and copy `vb/dtb` to
   `dtb/negroni/dtb` (git-ignored). The compiled mainline
   `sm8450-oneplus-negroni.dtb` should also work: it carries `qcom,msm-id` and `qcom,board-id`.
2. `BOARD=negroni shim/build.sh` builds `shim/out/shim-boot.img` (boot header v2, DTB inside).
3. Phone in fastboot: `fastboot boot shim/out/shim-boot.img`. This is a temporary boot only and flashes nothing.
4. Photograph the screen. Record the `MEM` lines (pfn = base >> 12, size in MiB),
   `CPU1-7` MPIDRs, the `GICD/GICR/VTIMER/SGI1` lines and the UFS lines in `NOTES.md`.

Unknowns this answers:
- Does this ABL accept a header-v2 `fastboot boot` image? If it refuses or
  boots the stock kernel, try header v4. The shim's loader then has to find the
  payload after the vendor ramdisk.
- Is the splash really `0xb8000000`, 1080x2412 a8r8g8b8? Skewed or no text means
  the width or base is wrong.
- Does the boot screen take a 4-frame GICR stride, and do vtimer INTID 27 and SGI interrupts arrive under Gunyah?
- Where is the upper DDR bank? That is needed for `--dram`.
- Does the reset reach fastboot? That tests the imem restart-reason address.

## Step 2: first kernel boot (ramdisk root, no drivers)

After CI has built a kernel with patch 0082:

```
python3 tools/mkadt.py out.adt --board negroni [--dram BASE:SIZE]
python3 tools/mkpayload.py KERNEL out.adt payload.bin --ramdisk SMALL_ROOT.img \
  --cmdline "debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress rd=md0 sweetlog=0 sweetkeys=0 sweettouch=0 sweetusb=0"
BOARD=negroni shim/build.sh payload.bin
```

`sweetkeys`, `sweettouch`, `sweetusb` and `sweetlog` all default to on and
map sweet MMIO addresses, so the `=0` boot-args are **required** on negroni
until those drivers check the board. Goal: kernel console on the framebuffer
through to the root mount. Then
`cpus=8`, then the drivers below.

## Driver work after that (in order)

1. Keys: SPMI arbiter v7 reads of PMK8350 PON (pwrkey, resin) and PM8350 GPIO6.
2. RPMh client at the SM8450 addresses (cmd-db lookup of `ldoc3`/`ldoc6`).
3. Touch: GENI I2C on QUP0 SE4, Synaptics S3908 TouchComm reports (5760x12864
   panel coordinates, 1440x3216 display coordinates in the DT) into `sweet_hid_touch`.
4. USB device mode: same DWC3 and qscratch, SNPS femto PHY instead of QUSB2.
   Check the USB30_PRIM GDSC offset against SM8450 GCC.
5. Display: SweetFramebuffer's fixed 1080x2400 should come from the ADT `sweet-fbcon` node.
6. UFS root: only after the user decides which partition iOS may use on this phone.
