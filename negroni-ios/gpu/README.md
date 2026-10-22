# A730 / iOS 16 backport — work in progress

User target: keep the existing xnu-8796 / iOS 16 system and backport both
NativeAdreno and NativeMetal. No deployable Metal driver exists here yet.

## Source revisions

- UniXNU: fb2bb310564ce66779e472a021881cf7c5764919
- NativeAdreno: 805c7e4bfb7451eb6923f4a14dea8c657b9be7d2
- NativeMetal: 6dcd4cbb69eb5625988706de42aa8ad3009b8ee7
- Local clone: /home/paw/ios-work/UniXNU

`derive-a730-hwinit.py` adapts upstream `scripts/derive-a740-hwinit.py`.
The upstream license is retained in LICENSE.generator. The generated register
table selects the actual `a730_hwcg` and `a730_protect_regs` source tables;
it is not the A740 table renamed. Input hashes are recorded in the header.

Regenerate/check from WSL:

```sh
python3 negroni-ios/gpu/derive-a730-hwinit.py --linux-source /home/paw/ios-work/UniXNU/.cache/linux
python3 negroni-ios/gpu/derive-a730-hwinit.py --linux-source /home/paw/ios-work/UniXNU/.cache/linux --check
clang -x c -std=c11 -fsyntax-only negroni-ios/gpu/NativeAdrenoA730HwInit.h
```

Verified: 54 register definitions, 53 clock-gating entries, 46 protection
writes; deterministic regeneration and C syntax. These are source checks,
not evidence of GPU power-up or rendering. The header is not linked into XNU.

## Remaining integration

### Userspace compatibility patch

`backport-metal.py` applies to the exact UniXNU/NativeMetal revisions above.
It changes all three build layers (plugin, Rust bridge, Turnip) to iOS 16.0
and enables availability errors for the plugin. It removes the 21H461-only
QuartzCore display-object/global writes and the frontend's private stack/object
offset probes. Image-relative caller backtraces remain. Existing publish/client
marker behavior is preserved; this script creates no publish marker.

```sh
python3 negroni-ios/gpu/backport-metal.py --unixnu /home/paw/ios-work/UniXNU --apply
python3 negroni-ios/gpu/backport-metal.py --unixnu /home/paw/ios-work/UniXNU --check
python3 -B negroni-ios/gpu/test_backport_metal.py /home/paw/ios-work/UniXNU
```

Applied and verified locally: three regression tests pass, including rejection
of unrelated changes and wrong source revisions. This is not an Objective-C++
compile or an ABI verification. The current WSL environment has no `xcrun`;
the pinned `.cache/mesa-work` and `.cache/gpu-reference/metal2vulkan` trees are
also absent. Building requires those upstream inputs plus an Apple SDK/toolchain
and the Rust/LLVM versions required by the upstream scripts. No dylib or bundle
was produced. Do not use an existing iOS17-built Turnip library with this patch.

The removed display writes controlled deep framebuffer/compression behavior on
the upstream image. Their removal avoids using invalid iOS17 offsets, but the
existing compression preferences still need validation against iOS16 QuartzCore.
This does not establish successful presentation.

### Kernel and device work still required

1. Separate A730 hardware policy from the A740 NativeAdreno implementation:
   GMU/RSC sequence, memory configuration, firmware, power/clock votes and
   DT-derived SMMU streams. Audit every A740 assumption; do not globally rename
   driver constants or reuse sheng voltage/bandwidth tables.
2. Port the driver and user-client interface to xnu-8796, preserving its 16K
   kernel page-size requirements while auditing GPU/GMU translation granules.
   In particular, `NativeAdrenoCpu.cpp::NativeAdrenoQueryCpu4K` checks CPU TCR,
   kernel and map granules against 4096. Do not merely relax that check: audit
   allocation ownership, cache-attribute changes and CPU mappings separately
   from the GPU's 4K SMMU mappings before admitting 16K CPU pages.
3. On-device validation: power and clocks, authenticated firmware where needed,
   GMU/HFI startup, command processor init, then a bounded submit/fence test.
4. Target A730 in the shader compiler and Turnip path; validate generated
   commands and shader execution before exposing a Metal device.
5. Backport the Metal plugin/bridge to iOS 16. Upstream bridge build explicitly
   sets IPHONEOS_DEPLOYMENT_TARGET=17.0; lowering it alone does not verify API or
   private ABI compatibility. Audit actual imports and device/queue/resource
   interfaces against the iOS 16 libraries before deployment.
6. Validate clear/draw/readback, then IOSurface/CoreAnimation presentation.

Firmware remains private in /home/paw/ios-work/negroni-firmware/gpu; do not
commit its contents. Existing boot images and userdata were not changed.
