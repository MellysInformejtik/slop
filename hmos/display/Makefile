# negroni: HarmonyOS NEXT display composer backend (libdisplay_composer_vdi_impl.z.so) on Linux DRM.
# OpenHarmony's DRM composer (drivers_peripheral composer/vdi_base) + the C entry points NEXT's
# composer service dlsym()s (vdi_exports.cpp, generated). Built with the HarmonyOS SDK clang against
# NEXT's own libc++ (namespace __h) and libdrm, so it loads into NEXT's composer_host.
NDK   ?= $(HOME)/hmos-work/tools/cmdline/command-line-tools/sdk/default/openharmony/native
NSDK  ?= $(HOME)/hmos-work/src/next-sdk
OH    ?= $(HOME)/hmos-work/src/oh-display
SECUREC ?= $(HOME)/hmos-work/hbn-src/kernel/platform/hw-securec
OUT   ?= out

CXX = $(NDK)/llvm/bin/clang++
CC  = $(NDK)/llvm/bin/clang
TARGET = --target=aarch64-linux-ohos --sysroot=$(NDK)/sysroot

DP  = $(OH)/drivers_peripheral/display
DI  = $(OH)/drivers_interface/display
INCLUDES = -Icompat -I. \
	-I$(DP)/composer/vdi_base/include -I$(DP)/composer/hdi_service/include \
	-I$(DP)/buffer/vdi_base/include -I$(DP)/buffer/hdi_service/include -I$(DP)/utils/include \
	-I$(OH)/gen -I$(OH)/gen/display -I$(OH)/gen/display/composer -I$(OH)/gen/display/buffer \
	-I$(DI)/composer -I$(DI)/composer/hdifd_parcelable -I$(DI)/buffer -I$(DI)/graphic/common \
	-I$(OH)/drivers_hdf_core/interfaces/inner_api/utils -I$(OH)/drivers_hdf_core/interfaces/inner_api/hdi -I$(OH)/drivers_hdf_core/interfaces/inner_api/ipc -I$(OH)/drivers_hdf_core/interfaces/inner_api/core \
	-I$(OH)/drivers_hdf_core/interfaces/inner_api/osal/shared -I$(OH)/drivers_hdf_core/interfaces/inner_api/osal/uhdf \
	-I$(OH)/commonlibrary_c_utils/base/include -I$(OH)/communication_ipc/interfaces/innerkits/ipc_core/include \
	-I$(OH)/third_party_libdrm -I$(OH)/third_party_libdrm/include/drm \
	-I$(SECUREC)/include -I$(NDK)/sysroot/usr/include/native_window
CXXFLAGS = $(TARGET) -include compat/linux_socket_fix.h -nostdinc++ -isystem $(NSDK)/libcxx -std=c++17 -O2 -fPIC -fvisibility=hidden \
	-Wno-unused-parameter -Wno-unknown-pragmas -Wno-c++11-narrowing $(INCLUDES)
CFLAGS = $(TARGET) -O2 -fPIC -fvisibility=hidden -I$(SECUREC)/include
LDFLAGS = $(TARGET) -shared -nostdlib++ -fuse-ld=lld -Wl,-soname,libdisplay_composer_vdi_impl.z.so \
	-Wl,--no-undefined -L$(NSDK)/lib -lc++ -ldrm

COMPOSER_SRC = $(wildcard $(DP)/composer/vdi_base/src/*.cpp) vdi_exports.cpp
# OpenHarmony's DRM allocator (buffer/vdi_base): the composer maps client buffers through it.
# negroni: renderD128 is the Adreno render node (no dumb buffers); allocate on the display card.
BUFFER_SRC = $(OUT)/patched/display_gralloc_gbm.cpp $(DP)/buffer/vdi_base/src/display_buffer_vdi_impl.cpp $(DP)/buffer/vdi_base/src/hi_gbm.cpp
SECUREC_SRC  = $(wildcard $(SECUREC)/src/*.c)
OBJS = $(patsubst %.cpp,$(OUT)/obj/%.o,$(notdir $(COMPOSER_SRC) $(BUFFER_SRC))) $(patsubst %.c,$(OUT)/obj/sec/%.o,$(notdir $(SECUREC_SRC)))

vpath %.cpp $(DP)/composer/vdi_base/src $(OUT)/patched $(DP)/buffer/vdi_base/src .
vpath %.c $(SECUREC)/src

all: $(OUT)/libdisplay_composer_vdi_impl.z.so $(OUT)/libdisplay_buffer_vdi_impl.z.so

# allocator_host backend: same allocator, own Create/Destroy (buffer_vdi.cpp) instead of vdi_base's
$(OUT)/patched/display_buffer_vdi_impl_nocreate.cpp: $(DP)/buffer/vdi_base/src/display_buffer_vdi_impl.cpp
	@mkdir -p $(dir $@)
	sed 's/extern "C" IDisplayBufferVdi\* CreateDisplayBufferVdi()/static IDisplayBufferVdi* UnusedCreateDisplayBufferVdi()/; s/extern "C" void DestroyDisplayBufferVdi(/static void UnusedDestroyDisplayBufferVdi(/' $< > $@
	grep -q UnusedCreateDisplayBufferVdi $@

BUFVDI_OBJS = $(OUT)/obj/display_gralloc_gbm.o $(OUT)/obj/hi_gbm.o $(OUT)/obj/display_buffer_vdi_impl_nocreate.o \
	$(OUT)/obj/buffer_vdi.o $(patsubst %.c,$(OUT)/obj/sec/%.o,$(notdir $(SECUREC_SRC)))

$(OUT)/libdisplay_buffer_vdi_impl.z.so: $(BUFVDI_OBJS)
	$(CXX) $(subst libdisplay_composer_vdi_impl,libdisplay_buffer_vdi_impl,$(LDFLAGS)) $^ -o $@ -L$(NSDK)/lib -lc++ -ldrm

$(OUT)/patched/display_gralloc_gbm.cpp: $(DP)/buffer/vdi_base/src/display_gralloc_gbm.cpp
	@mkdir -p $(dir $@)
	sed 's|"/dev/dri/renderD128"|"/dev/dri/card0"|' $< > $@
	grep -q '"/dev/dri/card0"' $@

$(OUT)/obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUT)/obj/sec/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/libdisplay_composer_vdi_impl.z.so: $(OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@ -L$(NSDK)/lib -lc++ -ldrm

clean:
	rm -rf $(OUT)
.PHONY: all clean
