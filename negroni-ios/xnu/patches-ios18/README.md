# iOS 18 userland on negroni (kernel side)

`0126-ios18-userland.patch` goes on top of `../patches/0001-0125`. It is a port of Bergamota's (ry-diffusion/Bergamota,
branch ios18) iOS 18.7.8 patches for the same xnu-8796 base, renamed sweet -> negroni (tools/rename-from-sweet.py):

- 0131 arm64e exec (`negroniarm64e=1`: arm64e graded, `ptrauth_disabled=1`, AMFI dyld policy op 0x5a), 0135 dyld pager,
  0136 sandbox container, 0138 mach_ports_lookup, 0139/0143 exclaves stubs, 0140 shared region slide v5,
  0141 FJCVTZS emulation, 0142 hw.optional caps, 0154 watchports 128, 0156 service ports without a Sandbox filter,
  0157/0159 iOS 18 IOSurface replies, 0160 iOS 18 IOMobileFramebuffer swap record (0x514), 0147 containermanagerd
  start delay (`negroniexecdelay`, hooked after exec_extract_strings here).
- Prerequisites from Bergamota 0082-0130: 0099 AMFI named policy, 0106 LS reset, 0116 sandbox extensions,
  0119-0122 IOSurface release/client memory/values/IOAccelerators, 0123 IOKit quiesce (NegroniBusy).
- Not taken: 0097 (frame holds for a delayed vsync copy; our framebuffer copies at swap time), 0132/0133 (Poco F1
  Bluetooth), 0144-0146/0148-0153/0155 (diagnostics), 0158 (their USB BPF).

Not built yet (CI needs a push). Userland: iOS 18.7.8 iPhone11,8 (22H352) root, identity iPhone X (D22) as now.

0128-ios18-physmap-failure-trace.patch adds failure-only caller and bounded
16-frame diagnostics to phystokv, phystokv_range and ml_static_vtop. It preserves
the original range checks and panics; it does not map reserved memory. Look for
`NEGRONI PHYSMAP:` before the panic. Raw frame addresses must be symbolicated
against the exact matching kernel with its slide. Early bootstrap skips stack
walking, and a one-shot atomic guard suppresses nested diagnostics. This is an
investigation build, not a demonstrated fix for the GPU panic.
