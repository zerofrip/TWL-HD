.PHONY: all arm7 arm9 arm11 arm11_ucode rtcom_rom test host-test clean

all: arm7 arm9 arm11 arm11_ucode

arm7:
	$(MAKE) -C arm7

arm9:
	$(MAKE) -C arm9

arm11:
	$(MAKE) -C arm11

arm11_ucode:
	$(MAKE) -C arm11_ucode

# Pack the ARM9 ELF with THIS project's custom ARM7 binary.  The default
# `make -C arm9` target still links the stock devkitPro ARM7 for Phase 1/2 users.
rtcom_rom: arm9 arm7
	ndstool -9 arm9/twlhd-arm9.elf -7 arm7/twlhd_arm7.elf -c twlhd-rtcom.nds

# ---------------------------------------------------------------------------
# Host-side tests (no devkitPro toolchain required).
# ---------------------------------------------------------------------------
HOST_CC ?= cc
HOST_CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -Icommon -Itests/host
HOST_SAN := $(shell printf 'int main(void){return 0;}\n' | \
	$(HOST_CC) -x c - -fsanitize=address,undefined -o /dev/null \
	>/dev/null 2>&1 && printf '%s' \
	'-fsanitize=address,undefined -fno-omit-frame-pointer')
HOST_CFLAGS += $(HOST_SAN)
HOST_LDFLAGS := $(HOST_SAN)

HOST_COMMON := common/twlhd_frame_parse.c common/twlhd_vertex_convert.c \
	common/twlhd_geometry_slots.c
HOST_HEADERS := common/twlhd_frame_parse.h common/twlhd_vertex_convert.h \
	common/twlhd_geometry_slots.h common/twlhd_frame_format.h \
	tests/host/tests_host.h
HOST_TESTS := tests/host/build/test_parser \
	tests/host/build/test_converter \
	tests/host/build/test_roundtrip \
	tests/host/build/test_geometry_slots

host-test: $(HOST_TESTS)
	@set -e; for test in $(HOST_TESTS); do \
		echo "== $$test"; ./$$test; \
	done

tests/host/build/test_parser: tests/host/test_parser.c $(HOST_COMMON) \
		$(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(HOST_COMMON) $(HOST_LDFLAGS)

tests/host/build/test_converter: tests/host/test_converter.c $(HOST_COMMON) \
		$(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(HOST_COMMON) $(HOST_LDFLAGS)

tests/host/build/test_roundtrip: tests/host/test_roundtrip.c $(HOST_COMMON) \
		$(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(HOST_COMMON) $(HOST_LDFLAGS)

tests/host/build/test_geometry_slots: tests/host/test_geometry_slots.c \
		$(HOST_COMMON) $(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(HOST_COMMON) $(HOST_LDFLAGS)

tests/host/build:
	mkdir -p $@

test: host-test
	PYTHONDONTWRITEBYTECODE=1 python3 -I -m unittest discover -s tools

clean:
	$(MAKE) -C arm7 clean
	$(MAKE) -C arm9 clean
	$(MAKE) -C arm11 clean
	$(MAKE) -C arm11_ucode clean
	rm -rf tests/host/build
	rm -f twlhd-rtcom.nds
