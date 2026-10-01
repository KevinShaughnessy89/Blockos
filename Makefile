# ============================================================
# BlockOS GNU-EFI C++ Build System
# x86_64 UEFI
# ============================================================

EFI_INCL       := /usr/include/efi
EFI_INCL_X86   := /usr/include/efi/x86_64

GNU_EFI_LIBDIR := /usr/lib
GNU_EFI_LDS    := $(GNU_EFI_LIBDIR)/elf_x86_64_efi.lds

EFI_CRT        := $(GNU_EFI_LIBDIR)/crt0-efi-x86_64.o
EFI_LIB        := $(GNU_EFI_LIBDIR)/libefi.a
GNU_EFI_LIB    := $(GNU_EFI_LIBDIR)/libgnuefi.a

CXX      := g++
LD       := ld
OBJCOPY  := objcopy
PYTHON   := python3

# ============================================================
# KCONFIG
# ============================================================

KCONFIG_MK      := include/generated/config.mk
KCONFIG_FILES   := $(wildcard Kconfig Kconfig.*)
KCONFIG_TOOL    := scripts/menuconfig.py

# The generated config is included before source selection so the
# configuration actually controls what is compiled.
-include $(KCONFIG_MK)


# ============================================================
# C++ FLAGS
# ============================================================

CXXFLAGS := \
	-fno-exceptions \
	-fno-rtti \
	-fshort-wchar \
	-fPIC \
	-fno-stack-protector \
	-fno-stack-check \
	-mno-red-zone \
	-DGNU_EFI_USE_MS_ABI \
	-mgeneral-regs-only \
	-mno-mmx \
	-mno-sse \
	-mno-sse2 \
	-I. \
	-I$(EFI_INCL) \
	-I$(EFI_INCL_X86) \
	-ffreestanding \
	-O2 \
	-Wall \
	-Wextra \
	-Iarch/86_64x \
	-Ikernel \
	-Idrivers \
	-Ifs \
	-Iexamples \
	-Ilibc/include \
	-fvisibility=hidden \
	-fno-strict-overflow \
	-fno-delete-null-pointer-checks \
	-MMD \
	-MP \
	-Iinclude

# ============================================================
# Floating-point flags for libc/src/math.cpp
#
# x86-64 ABI returns double values through SSE/XMM registers.
# The normal EFI/kernel build disables SSE, so math.cpp gets
# its own compatible floating-point compilation rule.
# ============================================================

MATH_CXXFLAGS := \
	$(filter-out \
		-mgeneral-regs-only \
		-mno-mmx \
		-mno-sse \
		-mno-sse2, \
		$(CXXFLAGS)) \
	-msse \
	-msse2 \
	-mfpmath=sse

# ============================================================
# ASSEMBLY FLAGS
# ============================================================

ASFLAGS := \
	-I. \
	-I$(EFI_INCL) \
	-I$(EFI_INCL_X86) \
	-ffreestanding \
	-MMD \
	-MP

# ============================================================
# LINKER FLAGS
# ============================================================

LDFLAGS := \
	-nostdlib \
	-znocombreloc \
	-T$(GNU_EFI_LDS) \
	-shared \
	-Bsymbolic

# ============================================================
# SOURCE DIRECTORIES
# ============================================================

SRC_DIRS := \
	drivers \
	examples \
	fs \
	kernel \
	libc/src \
	userspace \
	userspace/crt \
	userspace/ldso \
	userspace/libc \
	net

S_SRC_DIRS := \
	drivers \
	examples \
	fs \
	kernel

# ============================================================
# EXCLUDED SOURCES
# ============================================================

EXCLUDED_SRC := \
	kernel/login.cpp \
	kernel/panic.cpp

# Kconfig-selected source exclusions. Keep the historical defaults intact
# while allowing individual subsystems to be turned off.
ifeq ($(CONFIG_DRIVER_DEVICE_MANAGER),n)
EXCLUDED_SRC += drivers/device_manager.cpp
endif
ifeq ($(CONFIG_DRIVER_DMA),n)
EXCLUDED_SRC += drivers/dma.cpp
endif
ifeq ($(CONFIG_DRIVER_PCI),n)
EXCLUDED_SRC += drivers/pci.cpp drivers/pci_config.cpp drivers/pci_msix.cpp drivers/pci_subsystem.cpp drivers/pcie.cpp
endif
ifeq ($(CONFIG_DRIVER_ACPI),n)
EXCLUDED_SRC += drivers/acpi.cpp
endif
ifeq ($(CONFIG_DRIVER_ATA),n)
EXCLUDED_SRC += drivers/ata_pio.cpp
endif
ifeq ($(CONFIG_DRIVER_GRAPHICS_FB),n)
EXCLUDED_SRC += drivers/fb.cpp drivers/backbuffer.cpp
endif
ifeq ($(CONFIG_DRIVER_GRAPHICS_AMD_DCN6),n)
EXCLUDED_SRC += drivers/amd_dcn6.cpp
endif
ifeq ($(CONFIG_DRIVER_PS2),n)
EXCLUDED_SRC += drivers/ps2keyboard.cpp drivers/ps2mouse.cpp
endif
ifeq ($(CONFIG_DRIVER_USB),n)
EXCLUDED_SRC += drivers/usb_hid.cpp drivers/usb_storage.cpp drivers/usb_xhci.cpp
endif
ifeq ($(CONFIG_DRIVER_USB_XHCI),n)
EXCLUDED_SRC += drivers/usb_xhci.cpp
endif
ifeq ($(CONFIG_DRIVER_USB_HID),n)
EXCLUDED_SRC += drivers/usb_hid.cpp
endif
ifeq ($(CONFIG_DRIVER_USB_STORAGE),n)
EXCLUDED_SRC += drivers/usb_storage.cpp
endif
ifeq ($(CONFIG_DRIVER_VIRTIO),n)
EXCLUDED_SRC += drivers/virtio_blk.cpp drivers/virtio_blk_full.cpp drivers/virtio_common.cpp drivers/virtio_common_features.cpp drivers/virtio_common_modern.cpp drivers/virtio_common_state.cpp drivers/virtio_input.cpp drivers/virtio_net_driver.cpp drivers/virtio_net_tx.cpp drivers/virtio_notify.cpp drivers/virtio_pci.cpp drivers/virtio_service.cpp drivers/virtqueue_ops.cpp
endif
ifeq ($(CONFIG_DRIVER_VIRTIO_BLOCK),n)
EXCLUDED_SRC += drivers/virtio_blk.cpp drivers/virtio_blk_full.cpp
endif
ifeq ($(CONFIG_DRIVER_VIRTIO_NET),n)
EXCLUDED_SRC += drivers/virtio_net_driver.cpp drivers/virtio_net_tx.cpp
endif
ifeq ($(CONFIG_DRIVER_VIRTIO_INPUT),n)
EXCLUDED_SRC += drivers/virtio_input.cpp
endif
ifeq ($(CONFIG_DRIVER_NETWORK),n)
EXCLUDED_SRC += drivers/network.cpp drivers/network_checksum_helper.cpp drivers/network_socket.cpp drivers/lwip_adapter.cpp
endif
ifeq ($(CONFIG_DRIVER_DHCP_DNS),n)
EXCLUDED_SRC += drivers/dhcp_dns_stack.cpp
endif
ifeq ($(CONFIG_DRIVER_SOCKET),n)
EXCLUDED_SRC += drivers/network_socket.cpp
endif
ifeq ($(CONFIG_DRIVER_VM),n)
EXCLUDED_SRC += drivers/vm.cpp
endif

# Filesystem source selections. Core VFS plumbing is kept when FS_VFS=y.
ifeq ($(CONFIG_FS_VFS),n)
EXCLUDED_SRC += fs/vfs.cpp fs/vfs_blk_adapter.cpp fs/files.cpp fs/mount.cpp fs/system.cpp fs/autorun.cpp
endif
ifeq ($(CONFIG_FS_EXT2),n)
EXCLUDED_SRC += fs/ext2.cpp
endif
ifeq ($(CONFIG_FS_EXT3),n)
EXCLUDED_SRC += fs/ext3.cpp
endif
ifeq ($(CONFIG_FS_EXT4),n)
EXCLUDED_SRC += fs/ext4.cpp
endif
ifeq ($(CONFIG_FS_FAT32),n)
EXCLUDED_SRC += fs/fat32.cpp
endif
ifeq ($(CONFIG_FS_EXFAT),n)
EXCLUDED_SRC += fs/extfat.cpp
endif
ifeq ($(CONFIG_FS_NTFS),n)
EXCLUDED_SRC += fs/ntfs.cpp
endif
ifeq ($(CONFIG_FS_BTRFS),n)
EXCLUDED_SRC += fs/btrfs.cpp
endif
ifeq ($(CONFIG_FS_JFS),n)
EXCLUDED_SRC += fs/jfs.cpp
endif
ifeq ($(CONFIG_FS_ISO9660),n)
EXCLUDED_SRC += fs/iso9660.cpp
endif
ifeq ($(CONFIG_FS_UDF),n)
EXCLUDED_SRC += fs/udf.cpp
endif
ifeq ($(CONFIG_FS_UFS),n)
EXCLUDED_SRC += fs/ufs.cpp
endif
ifeq ($(CONFIG_FS_UFS2),n)
EXCLUDED_SRC += fs/ufs2.cpp
endif
ifeq ($(CONFIG_FS_RAMFS),n)
EXCLUDED_SRC += fs/ramfs.cpp
endif
ifeq ($(CONFIG_FS_TMPFS),n)
EXCLUDED_SRC += fs/tmpfs.cpp
endif
ifeq ($(CONFIG_FS_SQUASHFS),n)
EXCLUDED_SRC += fs/sqashasfs.cpp
endif
ifeq ($(CONFIG_FS_EROFS),n)
EXCLUDED_SRC += fs/EROFS.cpp
endif
ifeq ($(CONFIG_FS_LUA_ELF),n)
EXCLUDED_SRC += fs/lua_elf.cpp
endif
ifeq ($(CONFIG_FS_PROC),n)
EXCLUDED_SRC += fs/proc.cpp
endif

# libC/userspace selections.
ifeq ($(CONFIG_LIBC),n)
EXCLUDED_SRC += libc/src/*.cpp userspace/libc/*.cpp userspace/crt/*.S userspace/ldso/*.c userspace/ldso/*.S
endif

# ============================================================
# C++ SOURCES
# ============================================================

SRC := $(filter-out $(EXCLUDED_SRC), \
	$(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.cpp)))

SRC += kernel/cmd/cmd_ata.cpp
SRC += kernel/cmd/cmd_forth.cpp

# ============================================================
# ASSEMBLY SOURCES
# ============================================================

S_SRC := $(foreach dir,$(S_SRC_DIRS),$(wildcard $(dir)/*.S))

S_SRC += arch/86_64x/irq_stubs.S
S_SRC += arch/86_64x/user_entry.S
S_SRC += arch/86_64x/syscall_entry.S

# ============================================================
# OBJECT FILES
# ============================================================

OBJ := \
	$(SRC:.cpp=.o) \
	$(S_SRC:.S=.o)

# ============================================================
# DEPENDENCY FILES
# ============================================================

DEP := $(OBJ:.o=.d)

# ============================================================
# BUILD OUTPUT
# ============================================================

BUILD_DIR := build

SO_OUT  := $(BUILD_DIR)/kernel.so
EFI_OUT := $(BUILD_DIR)/BOOTX64.EFI
ESP32_BIN := $(BUILD_DIR)/esp32/blockos-esp32.bin

# ============================================================
# LUA
# ============================================================

LUA_DIR    := ports/lua
LUA_SCRIPT := $(LUA_DIR)/build-lua.sh
LUA_ELF    := $(LUA_DIR)/build/lua

# ============================================================
# DEFAULT
# ============================================================

all: $(KCONFIG_MK)
ifeq ($(CONFIG_ARCH_ESP32),y)
all: $(ESP32_BIN)
else ifeq ($(CONFIG_BOOT_GRUB),y)
all: grub-backend-not-ready
else
all: $(EFI_OUT)
endif

.PHONY: grub-backend-not-ready
grub-backend-not-ready:
	@echo "[ERROR] GRUB/Multiboot2 is selected in Kconfig, but the BlockOS GRUB backend is not implemented yet."
	@echo "[ERROR] Select UEFI / GNU-EFI to build the current x86-64 image."
	exit 2

# ============================================================
# KCONFIG GENERATION
# ============================================================

.PHONY: kconfig defconfig oldconfig

kconfig: menuconfig

defconfig: $(KCONFIG_FILES) $(KCONFIG_TOOL)
	@mkdir -p include/generated
	$(PYTHON) $(KCONFIG_TOOL) --defconfig

oldconfig: $(KCONFIG_FILES) $(KCONFIG_TOOL)
	@mkdir -p include/generated
	$(PYTHON) $(KCONFIG_TOOL) --oldconfig

$(KCONFIG_MK): $(KCONFIG_FILES) $(KCONFIG_TOOL)
	@mkdir -p include/generated
	$(PYTHON) $(KCONFIG_TOOL) --sync

# ============================================================
# FULL STACK
# ============================================================

.PHONY: full-stack

full-stack: host-all windowmaker install-windowmaker-rootfs rootfs-windowmaker-check $(EFI_OUT)

# ============================================================
# ESP32 BUILD
# ============================================================

ifeq ($(CONFIG_ARCH_ESP32),y)

$(ESP32_BIN): $(KCONFIG_MK) arch/esp32/Makefile
	@echo "[BLOCKOS] Building ESP32 image from Kconfig"
	$(MAKE) -f arch/esp32/Makefile all

.PHONY: esp32 esp32-image esp32-flash
esp32 esp32-image: $(ESP32_BIN)

esp32-flash: $(ESP32_BIN)
	$(MAKE) -f arch/esp32/Makefile flash

endif

# ============================================================
# C++ COMPILATION
# ============================================================

%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "[CXX] $<"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ============================================================
# SPECIAL FLOATING-POINT BUILD
# ============================================================

libc/src/math.o: libc/src/math.cpp
	@mkdir -p $(dir $@)
	@echo "[CXX][FP] $<"
	$(CXX) $(MATH_CXXFLAGS) -c $< -o $@

# ============================================================
# ASSEMBLY COMPILATION
# ============================================================

%.o: %.S
	@mkdir -p $(dir $@)
	@echo "[ASM] $<"
	$(CXX) $(ASFLAGS) -c $< -o $@

# ============================================================
# WEAKEN GNU-EFI memcpy / memset
# ============================================================

EFI_LIB_WEAK := $(BUILD_DIR)/libefi-weak.a

$(EFI_LIB_WEAK): $(EFI_LIB)
	@mkdir -p $(BUILD_DIR)
	@echo "[OBJCOPY] weakening memcpy/memset in $(notdir $(EFI_LIB))"
	cp $< $@
	$(OBJCOPY) \
		--weaken-symbol=memcpy \
		--weaken-symbol=memset \
		$@

# ============================================================
# LINK KERNEL.SO
# ============================================================

$(SO_OUT): $(OBJ) $(EFI_LIB_WEAK)
	@mkdir -p $(BUILD_DIR)

	@echo ""
	@echo "=============================================="
	@echo " Linking BlockOS kernel.so"
	@echo "=============================================="

	$(LD) \
		$(LDFLAGS) \
		-L$(GNU_EFI_LIBDIR) \
		$(EFI_CRT) \
		$(OBJ) \
		$(GNU_EFI_LIB) \
		$(EFI_LIB_WEAK) \
		-o $@

	@echo ""
	@echo "[OK] $@"

# ============================================================
# CREATE BOOTX64.EFI
# ============================================================

$(EFI_OUT): $(SO_OUT)
	@mkdir -p $(BUILD_DIR)

	@echo ""
	@echo "=============================================="
	@echo " Creating BOOTX64.EFI"
	@echo "=============================================="

	$(OBJCOPY) \
		-j .text \
		-j .plt \
		-j .init_array \
		-j .rodata \
		-j .ramfs \
		-j .dynstr \
		-j .sdata \
		-j .data \
		-j .dynamic \
		-j .dynsym \
		-j .rel \
		-j .rela \
		-j .rel.* \
		-j .rela.* \
		-j .reloc \
		--subsystem=10 \
		--target=efi-app-x86_64 \
		$(SO_OUT) \
		$(EFI_OUT)

	@echo ""
	@echo "[OK] $@"

# ============================================================
# LUA - OPTIONAL
# ============================================================

.PHONY: lua

lua:
	@set -eu; \
	if [ -z "$$USERLIBC" ]; then \
		echo "[ERROR] USERLIBC is not set."; \
		echo "[ERROR] Lua build skipped."; \
		exit 1; \
	fi; \
	if [ ! -d "$$USERLIBC" ]; then \
		echo "[ERROR] USERLIBC does not exist: $$USERLIBC"; \
		exit 1; \
	fi; \
	if [ ! -f "$(LUA_SCRIPT)" ]; then \
		echo "[ERROR] Missing $(LUA_SCRIPT)"; \
		exit 1; \
	fi; \
	chmod +x "$(LUA_SCRIPT)"; \
	USERLIBC="$$USERLIBC" "$(LUA_SCRIPT)"

# ============================================================
# CHECK GNU-EFI
# ============================================================

.PHONY: check-efi

check-efi:
	@echo "Checking GNU-EFI..."

	@test -f "$(GNU_EFI_LDS)" \
		&& echo "[OK] Linker script: $(GNU_EFI_LDS)" \
		|| echo "[ERROR] Missing: $(GNU_EFI_LDS)"

	@test -f "$(EFI_CRT)" \
		&& echo "[OK] CRT: $(EFI_CRT)" \
		|| echo "[ERROR] Missing: $(EFI_CRT)"

	@test -f "$(EFI_LIB)" \
		&& echo "[OK] libefi: $(EFI_LIB)" \
		|| echo "[ERROR] Missing: $(EFI_LIB)"

	@test -f "$(GNU_EFI_LIB)" \
		&& echo "[OK] libgnuefi: $(GNU_EFI_LIB)" \
		|| echo "[ERROR] Missing: $(GNU_EFI_LIB)"

# ============================================================
# MENUCONFIG
# ============================================================

.PHONY: menuconfig

menuconfig:
	$(PYTHON) scripts/menuconfig.py

# ============================================================
# CLEAN
# ============================================================

.PHONY: clean

clean:
	rm -f $(OBJ)
	rm -f $(DEP)
	rm -rf $(BUILD_DIR)

# ============================================================
# REBUILD
# ============================================================

.PHONY: rebuild

rebuild:
	$(MAKE) clean
	$(MAKE) all

# ============================================================
# RUN
# ============================================================

.PHONY: run

run: all
	sh build_and_run.sh

# ============================================================
# LINUX REPLACEMENT CHECK
# ============================================================

.PHONY: linux-replacement-check release-gate toolchain-bootstrap

linux-replacement-check:
	@mkdir -p build/linux-replacement
	$(CXX) -std=c++17 -Wall -Wextra -Werror -I. \
		kernel/compat/linux_compat_api.cpp \
		drivers/net/e1000.cpp \
		drivers/block/nvme.cpp \
		tests/linux_replacement_host.cpp \
		-o build/linux-replacement-test
	./build/linux-replacement-test
	./scripts/release/release_gate.sh

release-gate: platform-check server-profile-check linux-replacement-check
	@echo "[OK] BlockOS Linux-replacement release gate passed."

toolchain-bootstrap:
	./scripts/toolchain/build-blockos-toolchain.sh

# ============================================================
# HOST TESTS
# ============================================================

.PHONY: host-all browser-host windowmaker-runtime-check \
	install-windowmaker-rootfs windowmaker windowmaker-clean \
	bx11 bx11-api-test network-host-smoke browser-source-check \
	tls-backend-check linux20-plus-check rootfs-windowmaker-check \
	usermode-foundation-check userspace-runtime-check \
	bx11-install windowmaker-bx11 platform-check verify-platform \
	server-profile server-profile-check

host-all: bx11 network-host-smoke browser-host browser-source-check \
	tls-backend-check linux20-plus-check usermode-foundation-check

browser-host:
	$(MAKE) -C ports/browser -j$$(nproc)

windowmaker-runtime-check:
	bash scripts/windowmaker_runtime_check.sh

install-windowmaker-rootfs:
	@set -eu; \
	if [ ! -x build/windowmaker-rootfs/usr/bin/wmaker ]; then \
		echo "[ERROR] build/windowmaker-rootfs/usr/bin/wmaker is missing; build Window Maker first."; \
		exit 1; \
	fi; \
	mkdir -p build/sysroot/usr/bin build/sysroot/usr/lib build/sysroot/usr/share; \
	cp -a build/windowmaker-rootfs/usr/bin/wmaker build/sysroot/usr/bin/; \
	if [ -d build/windowmaker-rootfs/usr/lib ]; then \
		cp -a build/windowmaker-rootfs/usr/lib/. build/sysroot/usr/lib/; \
	fi; \
	if [ -d build/windowmaker-rootfs/usr/share ]; then \
		cp -a build/windowmaker-rootfs/usr/share/. build/sysroot/usr/share/; \
	fi; \
	echo "[OK] Window Maker installed into BlockOS sysroot"

windowmaker:
	BLOCKOS_SYSROOT=$(CURDIR)/build/sysroot JOBS=$$(nproc) ./windowmaker.sh

windowmaker-clean:
	sh ports/windowmaker/scripts/windowmaker-clean.sh

bx11:
	$(MAKE) -C ports/bx11 host-test

bx11-api-test:
	cc -std=c11 \
		-Iuserland/include \
		ports/windowmaker/tests/x11-api-compile.c \
		userland/libx11/xlib_stub.cpp \
		-lstdc++ \
		-o build/x11-api-test

network-host-smoke:
	@mkdir -p build/network-host
	$(CXX) -std=c++17 -Wall -Wextra -Werror -I. \
		kernel/network/tcp/tcp_segment.cpp \
		kernel/network/dns/dns_wire.cpp \
		kernel/network/tls/tls_record.cpp \
		tests/network_host_smoke.cpp \
		-o build/network-host-smoke
	./build/network-host-smoke

browser-source-check:
	test -f apps/blockbrowser/browser.hpp
	test -f apps/blockbrowser/browser.cpp
	test -f ports/browser/Makefile
	@echo "[OK] BlockOS browser source + HTTP/URL parser present"

tls-backend-check:
	test -f ports/tls/mbedtls_adapter.hpp
	test -f ports/tls/mbedtls_adapter.cpp
	test -f ports/tls/README.md
	@echo "[OK] TLS backend integration point present"

linux20-plus-check:
	@echo "[CHECK] Linux 2.0-era core capability coverage"
	test -f kernel/process.cpp
	test -f kernel/scheduler.cpp
	test -f kernel/elf_loader.cpp
	test -f kernel/syscall/syscall_dispatcher.cpp
	test -f kernel/network/tcp/tcp_socket.cpp
	test -f kernel/network/udp/udp_socket.cpp
	test -f kernel/network/ipv4/ipv4_packet.cpp
	test -f fs/ext2.cpp
	test -f fs/ext4.cpp
	test -f drivers/virtio_net_driver.cpp
	test -f drivers/virtio_blk.cpp
	test -f arch/86_64x/user_entry.S
	test -f kernel/io_uring.cpp
	test -f kernel/rcu.cpp
	test -f kernel/livepatch/livepatch.cpp
	test -f ports/bx11/libX11/display.cpp
	test -x windowmaker.sh
	@echo "[OK] BlockOS core capability source audit passed."

rootfs-windowmaker-check:
	bash -n scripts/generate_rootfs.sh
	bash -n ports/windowmaker/scripts/windowmakerdowloader+builder.sh
	@test -f kernel/init/services/gui.service
	@grep -q '^ExecStart=/usr/bin/wmaker$$' kernel/init/services/gui.service
	@grep -q '/etc/services/gui.service' fs/files.cpp
	@grep -q 'vfs_init_from_ramfs();' kernel/kernel.cpp
	@grep -q 'init_system.boot("/etc/services/")' kernel/kernel.cpp
	@echo "BlockOS rootfs + Window Maker autostart integration checks passed."

usermode-foundation-check:
	@test -f kernel/usermode/user_mode.hpp
	@test -f arch/86_64x/user_mode_gdt.cpp
	@test -f arch/86_64x/user_entry.S
	@grep -q 'USER_CODE_SELECTOR = 0x1B' kernel/usermode/user_mode.hpp
	@grep -q 'iretq' arch/86_64x/user_entry.S
	@echo "BlockOS user-mode/Ring3 foundation checks passed."

userspace-runtime-check:
	@echo "[CHECK] userspace runtime layers"
	bash scripts/check_autostart.sh
	bash scripts/windowmaker_runtime_check.sh
	@test -f arch/86_64x/usermode.cpp
	test -f arch/86_64x/user_entry.S
	test -f kernel/syscall/syscall_entry.S
	test -f kernel/syscall/syscall_entry.cpp
	test -f userland/runtime/syscall_runtime.cpp
	@grep -q 'map_user_zero_pages' arch/86_64x/paging.cpp arch/86_64x/paging.hpp
	@grep -q 'blockos_syscall_dispatch_entry' kernel/syscall/syscall_entry.S
	@echo "[OK] userspace runtime checks passed"

bx11-install:
	$(MAKE) -C ports/bx11 clean
	$(MAKE) -C ports/bx11 -j$$(nproc)
	BLOCKOS_SYSROOT=$(CURDIR)/build/sysroot ports/bx11/install-sysroot.sh

windowmaker-bx11: bx11-install
	BLOCKOS_SYSROOT=$(CURDIR)/build/sysroot ./windowmaker.sh

platform-check:
	@echo "[CHECK] BlockOS portable feature layer"
	@$(MAKE) --no-print-directory -C . verify-platform

verify-platform:
	sh scripts/verify_platform_features.sh

server-profile:
	@./scripts/server/build_server_profile.sh
	@./scripts/server/validate_server_profile.sh

server-profile-check: server-profile

# ============================================================
# HEADER DEPENDENCIES
# ============================================================

-include $(DEP)
