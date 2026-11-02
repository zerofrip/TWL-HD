.PHONY: all arm7 arm9 arm9v2 arm11 arm11_ucode rtcom_rom test host-test clean arm11-diag \
	emu-expected emu-validate

all: arm7 arm9 arm9v2 arm11 arm11_ucode

arm7:
	$(MAKE) -C arm7

arm9:
	$(MAKE) -C arm9

arm9v2:
	$(MAKE) -C arm9v2

arm11:
	$(MAKE) -C arm11

# Diagnostic ARM11 3dsx for real-hardware hang bisection. Separate target; the
# normal `arm11` build is untouched. HARDWARE UNVERIFIED (build-verified only);
# this build's purpose is real-hardware bisection. Do not deploy.
arm11-diag:
	$(MAKE) -C arm11 DIAG=1

arm11_ucode:
	$(MAKE) -C arm11_ucode

# Pack the ARM9 ELF with THIS project's custom ARM7 binary.  The default
# `make -C arm9` target still links the stock devkitPro ARM7 for Phase 1/2 users.
rtcom_rom: arm9 arm7
	ndstool -9 arm9/twlhd-arm9.elf -7 arm7/twlhd_arm7.elf -c twlhd-rtcom.nds

# ---------------------------------------------------------------------------
# Host-side tests.
#
# The pure C tests need only a host C compiler. The libnds shim test compiles
# the real devkitPro headers against mocked hardware, so `make test` requires
# DEVKITPRO and DEVKITARM (it fails loudly otherwise). The Python suite also
# runs the v2 port-coverage test, which needs DEVKITPRO.
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
	common/twlhd_geometry_slots.c common/twlhd_attr_pad.c
HOST_V2_COMMON := common/twlhd_v2_parse.c common/twlhd_v2_write.c \
	common/twlhd_gx_record.c common/twlhd_gx_interp.c
HOST_HEADERS := common/twlhd_frame_parse.h common/twlhd_vertex_convert.h \
	common/twlhd_geometry_slots.h common/twlhd_frame_format.h \
	common/twlhd_v2_format.h common/twlhd_v2_parse.h \
	common/twlhd_v2_write.h common/twlhd_gx_record.h \
	common/twlhd_gx_interp.h common/twlhd_attr_pad.h tests/host/tests_host.h
HOST_TESTS := tests/host/build/test_parser \
	tests/host/build/test_converter \
	tests/host/build/test_roundtrip \
	tests/host/build/test_geometry_slots \
	tests/host/build/test_attr_pad \
	tests/host/build/test_v2_parse \
	tests/host/build/test_v2_write \
	tests/host/build/test_gx_record \
	tests/host/build/test_v2_compat \
	tests/host/build/test_gx_interp \
	tests/host/build/test_libnds_shim \
	tests/host/build/test_demo_frame

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

tests/host/build/test_attr_pad: tests/host/test_attr_pad.c \
		$(HOST_COMMON) $(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(HOST_COMMON) $(HOST_LDFLAGS)

tests/host/build/test_v2_parse: tests/host/test_v2_parse.c $(HOST_V2_COMMON) \
		$(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(HOST_V2_COMMON) $(HOST_LDFLAGS)

tests/host/build/test_v2_write: tests/host/test_v2_write.c $(HOST_V2_COMMON) \
		$(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(HOST_V2_COMMON) $(HOST_LDFLAGS)

tests/host/build/test_gx_record: tests/host/test_gx_record.c $(HOST_V2_COMMON) \
		$(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(HOST_V2_COMMON) $(HOST_LDFLAGS)

tests/host/build/test_v2_compat: tests/host/test_v2_compat.c $(HOST_V2_COMMON) \
		$(HOST_COMMON) $(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(HOST_V2_COMMON) $(HOST_COMMON) \
		$(HOST_LDFLAGS)

tests/host/build/test_gx_interp: tests/host/test_gx_interp.c $(HOST_V2_COMMON) \
		$(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(HOST_V2_COMMON) $(HOST_LDFLAGS)

tests/host/build/gx_interp_cli: tests/host/gx_interp_cli.c $(HOST_V2_COMMON) \
		$(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(HOST_V2_COMMON) $(HOST_LDFLAGS)

# The shim test executes real libnds header code on the host with mocked
# hardware. It is UNIT VERIFIED, not a DS run, and needs devkitPro installed.
HOST_CXX ?= g++
SHIM_CFLAGS := -std=c++11 -O1 -g -DARM9 -D__NDS__ -fpermissive \
	-Iarm9v2/include -Icommon -Itests/host \
	-I$(DEVKITPRO)/libnds/include -I$(DEVKITPRO)/calico/include \
	-idirafter $(DEVKITARM)/arm-none-eabi/include

tests/host/build/test_libnds_shim: tests/host/test_libnds_shim.cpp \
		$(HOST_V2_COMMON) $(HOST_HEADERS) \
		arm9v2/include/twlhd_gx_shim.hpp | tests/host/build
	@if [ -z "$(DEVKITPRO)" ]; then \
		echo "error: DEVKITPRO is not set; the libnds shim test needs the devkitPro headers" >&2; \
		exit 1; \
	fi
	@if [ -z "$(DEVKITARM)" ]; then \
		echo "error: DEVKITARM is not set; the libnds shim test needs devkitARM" >&2; \
		exit 1; \
	fi
	$(HOST_CXX) $(SHIM_CFLAGS) -o $@ $< $(HOST_V2_COMMON)

# The demo-frame test compiles the same frame body the DS uses, with the same
# mock-hardware seam as the shim test.
SHIM_CXXFLAGS := -std=c++11 -O1 -g -DARM9 -D__NDS__ -fpermissive \
	-Iarm9v2/include -Iarm9v2/source -Icommon -Itests/host \
	-I$(DEVKITPRO)/libnds/include -I$(DEVKITPRO)/calico/include \
	-idirafter $(DEVKITARM)/arm-none-eabi/include

tests/host/build/test_demo_frame: tests/host/test_demo_frame.cpp \
		$(HOST_V2_COMMON) $(HOST_HEADERS) \
		arm9v2/include/demo_frame.hpp \
		arm9v2/include/twlhd_gx_shim.hpp \
		arm9v2/source/demo_frame.cpp | tests/host/build
	@if [ -z "$(DEVKITPRO)" ]; then \
		echo "error: DEVKITPRO is not set; the demo-frame test needs the devkitPro headers" >&2; \
		exit 1; \
	fi
	@if [ -z "$(DEVKITARM)" ]; then \
		echo "error: DEVKITARM is not set; the demo-frame test needs devkitARM" >&2; \
		exit 1; \
	fi
	$(HOST_CXX) $(SHIM_CXXFLAGS) -o $@ $< $(HOST_V2_COMMON)

# Expected-stream generator for tools/emulator/compare_capture.py. It runs the
# same frame body with the same mock seam as the demo-frame test.
tests/host/build/dump_demo_frame: tests/host/dump_demo_frame.cpp \
		$(HOST_V2_COMMON) $(HOST_HEADERS) \
		arm9v2/include/demo_frame.hpp \
		arm9v2/include/twlhd_gx_shim.hpp \
		arm9v2/source/demo_frame.cpp | tests/host/build
	@if [ -z "$(DEVKITPRO)" ]; then \
		echo "error: DEVKITPRO is not set; the expected-stream generator needs the devkitPro headers" >&2; \
		exit 1; \
	fi
	@if [ -z "$(DEVKITARM)" ]; then \
		echo "error: DEVKITARM is not set; the expected-stream generator needs devkitARM" >&2; \
		exit 1; \
	fi
	$(HOST_CXX) $(SHIM_CXXFLAGS) -o $@ $< $(HOST_V2_COMMON)

# ---------------------------------------------------------------------------
# Emulator validation (opt-in; NEVER part of `all` or `test`).
#
# Both targets need an external, unvendored DeSmuME CLI. `emu-validate` fails
# loudly when DESMUME_CLI is missing instead of silently "passing".
# ---------------------------------------------------------------------------
EMU_DIR := tests/host/build/emu
EMU_ANGLE := 61440

emu-expected: tests/host/build/dump_demo_frame
	@mkdir -p $(EMU_DIR)
	./tests/host/build/dump_demo_frame $(EMU_ANGLE) $(EMU_DIR)/expected_frame.txt

emu-validate:
	@if [ -z "$(DESMUME_CLI)" ]; then \
		echo "error: DESMUME_CLI is not set; point it at a desmume-cli binary (see docs/EMULATOR_VALIDATION.md)" >&2; \
		exit 1; \
	fi
	@if [ ! -x "$(DESMUME_CLI)" ]; then \
		echo "error: DESMUME_CLI=$(DESMUME_CLI) is not executable" >&2; \
		exit 1; \
	fi
	$(MAKE) -C arm9v2 AUTODUMP_FRAME=240
	$(MAKE) emu-expected
	tools/emulator/desmume_capture.sh arm9v2/twlhd-arm9v2-autodump.nds $(EMU_DIR)/capture
	tools/emulator/validate_with_parsers.sh $(EMU_DIR)/capture/twlhd_frame_v2.bin
	python3 tools/emulator/compare_capture.py \
		$(EMU_DIR)/capture/twlhd_frame_v2.bin \
		$(EMU_DIR)/expected_frame.txt $(EMU_ANGLE)

tests/host/build:
	mkdir -p $@

test: host-test tests/host/build/gx_interp_cli
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools

clean:
	$(MAKE) -C arm7 clean
	$(MAKE) -C arm9 clean
	$(MAKE) -C arm9v2 clean
	$(MAKE) -C arm11 clean
	$(MAKE) -C arm11_ucode clean
	rm -rf tests/host/build
	rm -f twlhd-rtcom.nds
