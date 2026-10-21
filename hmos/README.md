# HarmonyOS NEXT on negroni (OnePlus 10 Pro, SM8450)

NEXT 6.1 userland (Pura 70 firmware, not in git) on a mainline Linux EFI kernel booted by Mu.

- `native/firststage.c`: initramfs init. Mounts userdata (ext4 with the NEXT EROFS images), selinuxfs,
  loop-mounts the images, binds `/hmos/override` (and boot-image overrides from `/override`), logs kmsg
  to `/hmos/log/boot.log` and USB ACM (replayed from the file when the PC opens the port), execs
  NEXT's `init --second-stage`.
- `display/`: replacement display backends for NEXT's composer_host and allocator_host (Huawei's are
  Kirin-only). OpenHarmony's DRM composer and allocator (drivers_peripheral vdi_base), built with the
  HarmonyOS SDK clang against NEXT's own libc++ (`std::__h`) and libdrm; `gen_exports.py` makes the C
  entry points NEXT's composer service dlsym()s; `buffer_vdi.cpp` adds NEXT's extra
  `CloneDmaBufferHandle` vtable slot. Logs go to the kernel log as `NEGRONI-DISP:`.
- `readcom.ps1`: capture the USB serial log on Windows (COM3).
