# BlockOS Base Utilities (one command per C file)
# Run from the BlockOS repository root with: make -C cpy/butils
# All executables use BlockOS crt0 + BlockOS libc, and are linked static.
# No host libc, dynamic loader, or Linux PT_INTERP is linked.

BUTILS_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BLOCKOS_ROOT ?= $(abspath $(BUTILS_DIR)/../..)

ifeq ($(origin CC),default)
CC := gcc
endif
AR ?= ar

BUILD_DIR ?= $(BUTILS_DIR)/build
DESTDIR ?=
PREFIX ?= /System/bin
OUT_DIR ?= $(BUILD_DIR)/bin

CPPFLAGS += -I$(BLOCKOS_ROOT)/userspace/libc/include \
            -I$(BLOCKOS_ROOT)/libc/include \
            -D__SYS_readlink=22 -D__SYS_rmdir=72
CFLAGS ?= -O2
BUTILS_CFLAGS := -std=c11 -Wall -Wextra -ffreestanding -fno-pie \
                 -fno-stack-protector -mno-red-zone \
                 -ffunction-sections -fdata-sections

CRT_SRC := $(BLOCKOS_ROOT)/userspace/crt/crt0.S
LIBC_SRC_DIR := $(BLOCKOS_ROOT)/userspace/libc/src

# Keep this list to the source files that compile cleanly against the current
# BlockOS libc headers. The full libc currently has unrelated time/struct
# compatibility errors in other translation units.
LIBC_NAMES := syscall errno unistd unistd_extra fcntl dirent stat stat_extra \
              stdio stdlib string ctype signal pwd grp statvfs strings mman
LIBC_OBJS := $(addprefix $(BUILD_DIR)/libc/,$(addsuffix .o,$(LIBC_NAMES)))
LIBC_A := $(BUILD_DIR)/libblockos.a
CRT_OBJ := $(BUILD_DIR)/crt0.o

COMMAND_SRCS := $(wildcard $(BUTILS_DIR)/commands/*.c)
COMMANDS := $(patsubst $(BUTILS_DIR)/commands/%.c,%,$(COMMAND_SRCS))
COMMAND_BINS := $(addprefix $(OUT_DIR)/,$(COMMANDS))

.PHONY: all clean install list check-config check-syntax inspect
all: check-config $(COMMAND_BINS)

check-config:
	@test -f "$(CRT_SRC)" || { echo "ERROR: BlockOS crt not found: $(CRT_SRC)" >&2; echo "Set BLOCKOS_ROOT=/path/to/Blockos" >&2; exit 1; }
	@test -f "$(BLOCKOS_ROOT)/userspace/libc/include/blockos_syscall.h" || { echo "ERROR: BlockOS libc headers not found under $(BLOCKOS_ROOT)" >&2; exit 1; }
	@test -f "$(LIBC_SRC_DIR)/syscall.c" || { echo "ERROR: BlockOS libc sources not found under $(BLOCKOS_ROOT)" >&2; exit 1; }

$(BUILD_DIR)/commands/%.o: $(BUTILS_DIR)/commands/%.c
	@mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(BUTILS_CFLAGS) -c "$<" -o "$@"

$(BUILD_DIR)/libc/%.o: $(LIBC_SRC_DIR)/%.c
	@mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(BUTILS_CFLAGS) -DF_DUPFD_CLOEXEC=1030 -c "$<" -o "$@"

$(CRT_OBJ): $(CRT_SRC)
	@mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffreestanding -fno-pie -fno-stack-protector -mno-red-zone -c "$<" -o "$@"

$(LIBC_A): $(LIBC_OBJS)
	$(AR) rcs "$@" $(LIBC_OBJS)

$(OUT_DIR)/%: $(BUILD_DIR)/commands/%.o $(CRT_OBJ) $(LIBC_A)
	@mkdir -p "$(@D)"
	$(CC) -nostdlib -static -no-pie -Wl,-e,_start -Wl,--gc-sections -Wl,--build-id=none \
	      $(CRT_OBJ) "$<" $(LIBC_A) -o "$@"

list:
	@printf '%s\n' $(COMMANDS)

check-syntax: check-config
	@set -eu; for f in $(COMMAND_SRCS); do \
	  echo "CHECK $$f"; \
	  $(CC) $(CPPFLAGS) $(CFLAGS) $(BUTILS_CFLAGS) -fsyntax-only "$$f"; \
	done
	@echo "All command sources passed syntax checks."

inspect: all
	@set -eu; for f in $(COMMAND_BINS); do \
	  if readelf -l "$$f" | grep -q 'INTERP'; then \
	    echo "ERROR: dynamic interpreter found in $$f"; exit 1; \
	  fi; \
	done
	@echo "All outputs have no PT_INTERP (no ld.so dependency)."

install: all
	install -d "$(DESTDIR)$(PREFIX)"
	install -m 0755 $(COMMAND_BINS) "$(DESTDIR)$(PREFIX)/"

clean:
	rm -rf "$(BUILD_DIR)"
