.PHONY: all arm7 arm9 arm9v2 arm11 arm11_ucode rtcom_rom test test-eq2 test-eq3 test-eq-scenes test-arm9v2-static test-arm9v2-blend test-arm9v2-lit test-eq4-hw-compare test-fit-mapping test-cc3dsfs test-ds-scale-test test-ds-vtxcolor-test test-ds-alpha-test test-ds-edge-test test-eq4o-hw-rows host-test clean arm11-diag test-eq4za-clear-hw test-eq4zc-alpha-sweep test-eq4zd-sweep-hw \
	emu-expected emu-validate emu-validate-tex azahar-host ntr-capture ntr-input

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
HOST_CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -Icommon -Itests/host \
	-Itools/azahar_host
HOST_SAN := $(shell printf 'int main(void){return 0;}\n' | \
	$(HOST_CC) -x c - -fsanitize=address,undefined -o /dev/null \
	>/dev/null 2>&1 && printf '%s' \
	'-fsanitize=address,undefined -fno-omit-frame-pointer')
HOST_CFLAGS += $(HOST_SAN)
HOST_LDFLAGS := $(HOST_SAN)

HOST_COMMON := common/twlhd_frame_parse.c common/twlhd_vertex_convert.c \
	common/twlhd_geometry_slots.c common/twlhd_attr_pad.c
HOST_V2_COMMON := common/twlhd_v2_parse.c common/twlhd_v2_write.c \
	common/twlhd_gx_record.c common/twlhd_gx_interp.c \
	common/twlhd_tex_decode.c common/twlhd_tex_capture.c
HOST_HEADERS := common/twlhd_frame_parse.h common/twlhd_vertex_convert.h \
	common/twlhd_geometry_slots.h common/twlhd_frame_format.h \
	common/twlhd_v2_format.h common/twlhd_v2_parse.h \
	common/twlhd_v2_write.h common/twlhd_gx_record.h \
	common/twlhd_gx_interp.h common/twlhd_tex_decode.h \
	common/twlhd_pica_tex.h \
	common/twlhd_pica_frame.h \
	common/twlhd_attr_pad.h \
	common/twlhd_clipfix.h common/twlhd_seq.h \
	common/twlhd_tex_draw_plan.h \
	common/twlhd_tex_capture.h \
	common/twlhd_tex_source.h \
	tests/host/tests_host.h \
	tools/azahar_host/azahar_host_util.h
HOST_TESTS := tests/host/build/test_parser \
	tests/host/build/test_converter \
	tests/host/build/test_roundtrip \
	tests/host/build/test_geometry_slots \
	tests/host/build/test_attr_pad \
	tests/host/build/test_v2_parse \
	tests/host/build/test_blend_gate \
	tests/host/build/test_v2_write \
	tests/host/build/test_gx_record \
	tests/host/build/test_v2_compat \
	tests/host/build/test_gx_interp \
	tests/host/build/test_tex_decode \
	tests/host/build/test_pica_tex \
	tests/host/build/test_pica_frame \
	tests/host/build/test_tex_draw_plan \
	tests/host/build/test_tex_capture \
	tests/host/build/test_gx_record_tex \
	tests/host/build/test_clipfix \
	tests/host/build/test_seq \
	tests/host/build/test_libnds_shim \
	tests/host/build/test_demo_frame \
	tests/host/build/test_demo_frame_vp \
	tests/host/build/test_demo_frame_c170 \
	tests/host/build/test_demo_frame_c2 \
	tests/host/build/test_demo_frame_s1 \
	tests/host/build/test_demo_frame_s2 \
	tests/host/build/test_demo_frame_s3 \
	tests/host/build/test_demo_frame_s4 \
	tests/host/build/test_demo_frame_s5 \
	tests/host/build/test_blend_config \
	tests/host/build/test_azahar_host_ppm \
	tests/host/build/test_ntr_capture_util \
	tests/host/build/test_ntr_input_util \
	tests/host/build/test_diag_config \
	tests/host/build/test_render_clear \
	tests/host/build/test_render_clear_override \
	tests/host/build/test_blend_gate_wiring

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

tests/host/build/test_blend_gate: tests/host/test_blend_gate.c $(HOST_V2_COMMON) \
		$(HOST_HEADERS) arm11/include/twlhd_blend_gate.h | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -Iarm11/include -o $@ $< $(HOST_V2_COMMON) $(HOST_LDFLAGS)

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

tests/host/build/test_tex_decode: tests/host/test_tex_decode.c \
		$(HOST_V2_COMMON) $(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(HOST_V2_COMMON) $(HOST_LDFLAGS)

tests/host/build/test_clipfix: tests/host/test_clipfix.c common/twlhd_clipfix.c \
		$(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< common/twlhd_clipfix.c $(HOST_LDFLAGS)

tests/host/build/test_pica_tex: tests/host/test_pica_tex.c \
		common/twlhd_pica_tex.c $(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< common/twlhd_pica_tex.c $(HOST_LDFLAGS)

tests/host/build/test_pica_frame: tests/host/test_pica_frame.c \
		common/twlhd_pica_frame.c common/twlhd_pica_tex.c \
		$(HOST_V2_COMMON) $(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< common/twlhd_pica_frame.c \
		common/twlhd_pica_tex.c $(HOST_V2_COMMON) $(HOST_LDFLAGS)

tests/host/build/test_tex_draw_plan: tests/host/test_tex_draw_plan.c \
		common/twlhd_tex_draw_plan.c $(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< common/twlhd_tex_draw_plan.c \
		$(HOST_LDFLAGS)

tests/host/build/test_tex_capture: tests/host/test_tex_capture.c \
		common/twlhd_pica_frame.c common/twlhd_pica_tex.c \
		$(HOST_V2_COMMON) $(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< common/twlhd_pica_frame.c \
		common/twlhd_pica_tex.c $(HOST_V2_COMMON) $(HOST_LDFLAGS)

tests/host/build/test_gx_record_tex: tests/host/test_gx_record_tex.c \
		common/twlhd_tex_source.c common/twlhd_pica_frame.c \
		common/twlhd_pica_tex.c $(HOST_V2_COMMON) $(HOST_HEADERS) | \
		tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< common/twlhd_tex_source.c \
		common/twlhd_pica_frame.c common/twlhd_pica_tex.c \
		$(HOST_V2_COMMON) $(HOST_LDFLAGS)

tests/host/build/test_seq: tests/host/test_seq.c $(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(HOST_LDFLAGS)

tests/host/build/test_azahar_host_ppm: tests/host/test_azahar_host_ppm.c \
		$(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(HOST_LDFLAGS)

tests/host/build/test_ntr_capture_util: tests/host/test_ntr_capture_util.c \
		tools/ntr_capture/ntr_capture_util.h $(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -Itools/ntr_capture -o $@ $< $(HOST_LDFLAGS)

tests/host/build/test_ntr_input_util: tests/host/test_ntr_input_util.c \
		tools/ntr_capture/ntr_input_util.h \
		tools/ntr_capture/ntr_capture_util.h $(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -Itools/ntr_capture -o $@ $< $(HOST_LDFLAGS)

tests/host/build/gx_interp_cli: tests/host/gx_interp_cli.c $(HOST_V2_COMMON) \
		$(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(HOST_V2_COMMON) $(HOST_LDFLAGS)

tests/host/build/tex_decode_cli: tests/host/tex_decode_cli.c $(HOST_V2_COMMON) \
		$(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(HOST_V2_COMMON) $(HOST_LDFLAGS)

tests/host/build/pica_tex_cli: tests/host/pica_tex_cli.c \
		common/twlhd_pica_tex.c $(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< common/twlhd_pica_tex.c $(HOST_LDFLAGS)

tests/host/build/pica_frame_cli: tests/host/pica_frame_cli.c \
		common/twlhd_pica_frame.c common/twlhd_pica_tex.c \
		$(HOST_V2_COMMON) $(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< common/twlhd_pica_frame.c \
		common/twlhd_pica_tex.c $(HOST_V2_COMMON) $(HOST_LDFLAGS)

tests/host/build/tex_capture_cli: tests/host/tex_capture_cli.c \
		common/twlhd_tex_capture.c common/twlhd_v2_parse.c \
		$(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< common/twlhd_tex_capture.c \
		common/twlhd_v2_parse.c $(HOST_LDFLAGS)

# Blend-config test compiles the real libctru GPU enums header on the host and
# asserts the clip-path (alpha blend) and DEBUG-path (opaque) configurations.
# It also structurally checks renderer.c for the expected call-site counts.
tests/host/build/test_blend_config: tests/host/test_blend_config.c \
		$(HOST_HEADERS) arm11/include/twlhd_blend_config.h | tests/host/build
	@if [ -z "$(DEVKITPRO)" ]; then \
		echo "error: DEVKITPRO is not set; the blend-config test needs the libctru headers" >&2; \
		exit 1; \
	fi
	$(HOST_CC) $(HOST_CFLAGS) -Iarm11/include \
		-I$(DEVKITPRO)/libctru/include -o $@ $< $(HOST_LDFLAGS)

# Diagnostic-config test compiles the real libctru GPU enums header on the host
# and asserts the three clip-path diagnostic modes. It also structurally checks
# main.c and renderer.c for the expected call-site counts and literal strings.
tests/host/build/test_diag_config: tests/host/test_diag_config.c \
		$(HOST_HEADERS) arm11/include/twlhd_diag_config.h \
		arm11/source/main.c arm11/source/renderer.c | tests/host/build
	@if [ -z "$(DEVKITPRO)" ]; then \
		echo "error: DEVKITPRO is not set; the diag-config test needs the libctru headers" >&2; \
		exit 1; \
	fi
	$(HOST_CC) $(HOST_CFLAGS) -Iarm11/include \
		-I$(DEVKITPRO)/libctru/include -o $@ $< $(HOST_LDFLAGS)

# Render-clear test: the clear colour is defined once in a freestanding header
# and used at both clear sites in renderer.c.  No libctru headers needed.
tests/host/build/test_render_clear: tests/host/test_render_clear.c \
		$(HOST_HEADERS) arm11/include/twlhd_render_config.h \
		arm11/source/renderer.c | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -Iarm11/include -o $@ $< $(HOST_LDFLAGS)

# Render-clear override test: compile-time override restores the old diagnostic
# colour (EQ-4z).  Same source, different macro definition.
tests/host/build/test_render_clear_override: tests/host/test_render_clear.c \
		$(HOST_HEADERS) arm11/include/twlhd_render_config.h | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -Iarm11/include \
		-DOVERRIDE_TEST -DTWLHD_RENDER_CLEAR_COLOR=0x101018FFu \
		-o $@ $< $(HOST_LDFLAGS)

# Blend-gate wiring test: pure text structural check of arm11/source/main.c.
tests/host/build/test_blend_gate_wiring: tests/host/test_blend_gate_wiring.c \
		$(HOST_HEADERS) | tests/host/build
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(HOST_LDFLAGS)

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

# Same frame body compiled with a non-default viewport, so the test checks the
# packed VIEWPORT word the variant build records (BUILD/UNIT only).
tests/host/build/test_demo_frame_vp: tests/host/test_demo_frame.cpp \
		$(HOST_V2_COMMON) $(HOST_HEADERS) \
		arm9v2/include/demo_frame.hpp \
		arm9v2/include/twlhd_gx_shim.hpp \
		arm9v2/source/demo_frame.cpp | tests/host/build
	@if [ -z "$(DEVKITPRO)" ]; then \
		echo "error: DEVKITPRO is not set; the variant-viewport test needs the devkitPro headers" >&2; \
		exit 1; \
	fi
	@if [ -z "$(DEVKITARM)" ]; then \
		echo "error: DEVKITARM is not set; the variant-viewport test needs devkitARM" >&2; \
		exit 1; \
	fi
	$(HOST_CXX) $(SHIM_CXXFLAGS) -DTWLHD_DEMO_VP_X1=32 -DTWLHD_DEMO_VP_Y1=96 \
		-DTWLHD_DEMO_VP_X2=159 -DTWLHD_DEMO_VP_Y2=191 \
		-DTWLHD_TEST_EXPECT_VP_WORD=0xBF9F6020u -o $@ $< $(HOST_V2_COMMON)

# Same frame body compiled with a multi-cube build, so the test checks the
# recorded per-cube command counts and the interpreter output (BUILD/UNIT only).
tests/host/build/test_demo_frame_c170: tests/host/test_demo_frame.cpp \
		$(HOST_V2_COMMON) $(HOST_HEADERS) \
		arm9v2/include/demo_frame.hpp \
		arm9v2/include/twlhd_gx_shim.hpp \
		arm9v2/source/demo_frame.cpp | tests/host/build
	@if [ -z "$(DEVKITPRO)" ]; then \
		echo "error: DEVKITPRO is not set; the multi-cube test needs the devkitPro headers" >&2; \
		exit 1; \
	fi
	@if [ -z "$(DEVKITARM)" ]; then \
		echo "error: DEVKITARM is not set; the multi-cube test needs devkitARM" >&2; \
		exit 1; \
	fi
	$(HOST_CXX) $(SHIM_CXXFLAGS) -DTWLHD_DEMO_CUBES=170 \
		-DTWLHD_TEST_EXPECT_CUBES=170 -o $@ $< $(HOST_V2_COMMON)

tests/host/build/test_demo_frame_c2: tests/host/test_demo_frame.cpp \
		$(HOST_V2_COMMON) $(HOST_HEADERS) \
		arm9v2/include/demo_frame.hpp \
		arm9v2/include/twlhd_gx_shim.hpp \
		arm9v2/source/demo_frame.cpp | tests/host/build
	@if [ -z "$(DEVKITPRO)" ]; then \
		echo "error: DEVKITPRO is not set; the multi-cube test needs the devkitPro headers" >&2; \
		exit 1; \
	fi
	@if [ -z "$(DEVKITARM)" ]; then \
		echo "error: DEVKITARM is not set; the multi-cube test needs devkitARM" >&2; \
		exit 1; \
	fi
	$(HOST_CXX) $(SHIM_CXXFLAGS) -DTWLHD_DEMO_CUBES=2 \
		-DTWLHD_TEST_EXPECT_CUBES=2 -o $@ $< $(HOST_V2_COMMON)

# Same frame body compiled with each primitive-scene build, so the test checks
# the recorded primitive type/counts and the interpreter output (BUILD/UNIT
# only).
tests/host/build/test_demo_frame_s1: tests/host/test_demo_frame.cpp \
		$(HOST_V2_COMMON) $(HOST_HEADERS) \
		arm9v2/include/demo_frame.hpp \
		arm9v2/include/twlhd_gx_shim.hpp \
		arm9v2/source/demo_frame.cpp | tests/host/build
	@if [ -z "$(DEVKITPRO)" ]; then \
		echo "error: DEVKITPRO is not set; the scene test needs the devkitPro headers" >&2; \
		exit 1; \
	fi
	@if [ -z "$(DEVKITARM)" ]; then \
		echo "error: DEVKITARM is not set; the scene test needs devkitARM" >&2; \
		exit 1; \
	fi
	$(HOST_CXX) $(SHIM_CXXFLAGS) -DTWLHD_DEMO_SCENE=1 \
		-DTWLHD_TEST_EXPECT_SCENE=1 -o $@ $< $(HOST_V2_COMMON)

tests/host/build/test_demo_frame_s2: tests/host/test_demo_frame.cpp \
		$(HOST_V2_COMMON) $(HOST_HEADERS) \
		arm9v2/include/demo_frame.hpp \
		arm9v2/include/twlhd_gx_shim.hpp \
		arm9v2/source/demo_frame.cpp | tests/host/build
	@if [ -z "$(DEVKITPRO)" ]; then \
		echo "error: DEVKITPRO is not set; the scene test needs the devkitPro headers" >&2; \
		exit 1; \
	fi
	@if [ -z "$(DEVKITARM)" ]; then \
		echo "error: DEVKITARM is not set; the scene test needs devkitARM" >&2; \
		exit 1; \
	fi
	$(HOST_CXX) $(SHIM_CXXFLAGS) -DTWLHD_DEMO_SCENE=2 \
		-DTWLHD_TEST_EXPECT_SCENE=2 -o $@ $< $(HOST_V2_COMMON)

tests/host/build/test_demo_frame_s3: tests/host/test_demo_frame.cpp \
		$(HOST_V2_COMMON) $(HOST_HEADERS) \
		arm9v2/include/demo_frame.hpp \
		arm9v2/include/twlhd_gx_shim.hpp \
		arm9v2/source/demo_frame.cpp | tests/host/build
	@if [ -z "$(DEVKITPRO)" ]; then \
		echo "error: DEVKITPRO is not set; the scene test needs the devkitPro headers" >&2; \
		exit 1; \
	fi
	@if [ -z "$(DEVKITARM)" ]; then \
		echo "error: DEVKITARM is not set; the scene test needs devkitARM" >&2; \
		exit 1; \
	fi
	$(HOST_CXX) $(SHIM_CXXFLAGS) -DTWLHD_DEMO_SCENE=3 \
		-DTWLHD_TEST_EXPECT_SCENE=3 -o $@ $< $(HOST_V2_COMMON)

tests/host/build/test_demo_frame_s4: tests/host/test_demo_frame.cpp \
		$(HOST_V2_COMMON) $(HOST_HEADERS) \
		arm9v2/include/demo_frame.hpp \
		arm9v2/include/twlhd_gx_shim.hpp \
		arm9v2/source/demo_frame.cpp | tests/host/build
	@if [ -z "$(DEVKITPRO)" ]; then \
		echo "error: DEVKITPRO is not set; the scene test needs the devkitPro headers" >&2; \
		exit 1; \
	fi
	@if [ -z "$(DEVKITARM)" ]; then \
		echo "error: DEVKITARM is not set; the scene test needs devkitARM" >&2; \
		exit 1; \
	fi
	$(HOST_CXX) $(SHIM_CXXFLAGS) -DTWLHD_DEMO_SCENE=4 \
		-DTWLHD_TEST_EXPECT_SCENE=4 -o $@ $< $(HOST_V2_COMMON)
# Same frame body compiled with the textured scene (build-only; no VRAM run).
tests/host/build/test_demo_frame_s5: tests/host/test_demo_frame.cpp \
		$(HOST_V2_COMMON) $(HOST_HEADERS) \
		common/twlhd_tex_source.c \
		arm9v2/include/demo_frame.hpp \
		arm9v2/include/twlhd_gx_shim.hpp \
		arm9v2/source/demo_frame.cpp | tests/host/build
	@if [ -z "$(DEVKITPRO)" ]; then \
		echo "error: DEVKITPRO is not set; the scene test needs the libnds headers" >&2; \
		exit 1; \
	fi
	@if [ -z "$(DEVKITARM)" ]; then \
		echo "error: DEVKITARM is not set; the scene test needs devkitARM" >&2; \
		exit 1; \
	fi
	$(HOST_CXX) $(SHIM_CXXFLAGS) -DTWLHD_DEMO_SCENE=5 \
		-DTWLHD_TEST_EXPECT_SCENE=5 -o $@ $< $(HOST_V2_COMMON) \
		common/twlhd_tex_source.c


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
# These targets need an external, unvendored DeSmuME CLI. They fail loudly when
# DESMUME_CLI is missing instead of silently "passing".
# ---------------------------------------------------------------------------
EMU_DIR := tests/host/build/emu
EMU_ANGLE := 61440
EMU_ANGLE_TEX := 61440

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

# SCENE=5 textured autodump capture. Separate outdir so `emu-validate`
# artifacts are never clobbered. BUILD/UNIT tooling until the owner records a
# real run (docs/EMULATOR_VALIDATION.md).
emu-validate-tex:
	@if [ -z "$(DESMUME_CLI)" ]; then \
		echo "error: DESMUME_CLI is not set; point it at a desmume-cli binary (see docs/EMULATOR_VALIDATION.md)" >&2; \
		exit 1; \
	fi
	@if [ ! -x "$(DESMUME_CLI)" ]; then \
		echo "error: DESMUME_CLI=$(DESMUME_CLI) is not executable" >&2; \
		exit 1; \
	fi
	$(MAKE) -C arm9v2 SCENE=5 AUTODUMP_FRAME=240
	tools/emulator/desmume_capture.sh arm9v2/twlhd-arm9v2-autodump-s5.nds $(EMU_DIR)/capture-tex
	tools/emulator/validate_with_parsers.sh $(EMU_DIR)/capture-tex/twlhd_frame_v2.bin
	python3 tools/emulator/compare_texture_capture.py \
		$(EMU_DIR)/capture-tex/twlhd_frame_v2.bin $(EMU_ANGLE_TEX)

tests/host/build:
	mkdir -p $@

# ---------------------------------------------------------------------------
# Headless Azahar libretro host (opt-in; NEVER part of `all` or `test`).
#
# The Azahar core stays external and unvendored: it is dlopen()ed at run time.
# Pass LIBRETRO_INCLUDE=/path/to/libretro-common/include to compile against the
# real libretro.h; without it the tool uses its minimal hand-written mirror.
# ---------------------------------------------------------------------------
LIBRETRO_INCLUDE ?=

azahar-host: tools/azahar_host/build/azahar_host

tools/azahar_host/build/azahar_host: tools/azahar_host/azahar_host.c \
		tools/azahar_host/azahar_host_util.h \
		tools/azahar_host/azahar_host_libretro_min.h | tools/azahar_host/build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O2 -D_POSIX_C_SOURCE=200809L \
		-Itools/azahar_host $(if $(LIBRETRO_INCLUDE),-I$(LIBRETRO_INCLUDE)) \
		-o $@ $< -ldl

tools/azahar_host/build:
	mkdir -p $@

# ---------------------------------------------------------------------------
# Headless NTR-HR remote-play capture (opt-in; NEVER part of `all` or `test`).
#
# The binary drives NTR-HR "JPEG Compat (UDP)" remote play and writes PPM
# frames; the console is only contacted when the binary is run, never by this
# target. NTRVIEWER_DIR is the MIT-licensed viewer source tree the protocol and
# bundled jpeg_turbo come from. Its bundled libjpeg-turbo needs nasm (missing
# here) and hard-codes WITH_SIMD=1 without a no-SIMD switch, so the target uses
# the system libjpeg-turbo headers instead; it fails loudly if they are missing.
# Nothing from the viewer is vendored into this repository.
# ---------------------------------------------------------------------------
NTRVIEWER_DIR ?= $(HOME)/3ds/ntrviewer-hr
NTR_CAPTURE_JPEG_CFLAGS := $(shell pkg-config --cflags libjpeg 2>/dev/null)
NTR_CAPTURE_JPEG_LIBS := $(shell pkg-config --libs libjpeg 2>/dev/null)

ntr-capture: tools/ntr_capture/build/ntr_capture

tools/ntr_capture/build/ntr_capture: tools/ntr_capture/ntr_capture.c \
		tools/ntr_capture/ntr_capture_util.h | tools/ntr_capture/build
	@if [ -z "$(NTR_CAPTURE_JPEG_LIBS)" ] || [ ! -f /usr/include/jpeglib.h ]; then \
		echo "error: libjpeg headers not found (need /usr/include/jpeglib.h and -ljpeg);" >&2; \
		echo "       $(NTRVIEWER_DIR)/jpeg_turbo cannot build standalone here: nasm is" >&2; \
		echo "       required for its hard-coded WITH_SIMD=1 build." >&2; \
		exit 1; \
	fi
	@if [ ! -d "$(NTRVIEWER_DIR)/jpeg_turbo" ]; then \
		echo "note: $(NTRVIEWER_DIR)/jpeg_turbo not found; using system libjpeg-turbo" >&2; \
	elif ! command -v nasm >/dev/null 2>&1; then \
		echo "note: $(NTRVIEWER_DIR)/jpeg_turbo needs nasm (missing) for its WITH_SIMD=1 build;" >&2; \
		echo "      using system libjpeg-turbo" >&2; \
	fi
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O2 -D_POSIX_C_SOURCE=200809L \
		-Itools/ntr_capture $(NTR_CAPTURE_JPEG_CFLAGS) \
		-o $@ $< $(NTR_CAPTURE_JPEG_LIBS)

tools/ntr_capture/build:
	mkdir -p $@

# ---------------------------------------------------------------------------
# Headless NTR-HR input redirection (opt-in; NEVER part of `all` or `test`).
#
# Sends 20-byte input_redirection_frame_t datagrams to Rosalina's (Luma3DS)
# input redirection, UDP port 4950; the console is only contacted when the
# binary is run, never by this target. Plain POSIX sockets, no libjpeg.
# ---------------------------------------------------------------------------
ntr-input: tools/ntr_capture/build/ntr_input

tools/ntr_capture/build/ntr_input: tools/ntr_capture/ntr_input.c \
		tools/ntr_capture/ntr_input_util.h \
		tools/ntr_capture/ntr_capture_util.h | tools/ntr_capture/build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O2 -D_POSIX_C_SOURCE=200809L \
		-Itools/ntr_capture -o $@ $<

# EQ-2b suite (tools/eq2_compare.py). Run explicitly; the discover pass in
# `test` sets EQ2_DISCOVER_SKIP so this suite is not executed twice.
test-eq2:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_eq2_compare.py'

# EQ-3b suite (tools/eq3_compare.py). Run explicitly; the discover pass in
# `test` sets EQ3_DISCOVER_SKIP so this suite is not executed twice.
test-eq3:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_eq3_compare.py'

# EQ-2/EQ-3 scene coverage suite (tools/eq_scenes.py). Run explicitly; the
# discover pass in `test` sets EQ_SCENES_DISCOVER_SKIP so this suite is not
# executed twice.
test-eq-scenes:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_eq_scenes.py'

# arm9v2 STATIC_ANGLE Makefile validation (dry-run only). Run explicitly; the
# discover pass in `test` sets ARM9V2_STATIC_DISCOVER_SKIP so it is not
# executed twice.
test-arm9v2-static:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_arm9v2_static_angle.py'

# arm9v2 BLEND Makefile validation (dry-run only). Run explicitly; the discover
# pass in `test` sets ARM9V2_BLEND_DISCOVER_SKIP so it is not executed twice.
test-arm9v2-blend:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_arm9v2_blend.py'

# arm9v2 LIT Makefile validation (dry-run only). Run explicitly; the discover
# pass in `test` sets ARM9V2_LIT_DISCOVER_SKIP so it is not executed twice.
test-arm9v2-lit:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_arm9v2_lit.py'

# EQ-4 hardware-capture comparison suite (tools/eq4_hw_compare.py). Run
# explicitly; the discover pass in `test` sets EQ4_HW_COMPARE_DISCOVER_SKIP so
# it is not executed twice.
test-eq4-hw-compare:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_eq4_hw_compare.py'

# cc3dsfs capture helper suite (tools/cc3dsfs_capture/test_xwd2png.py).
# Discovered explicitly: the aggregate pass below does not descend into
# tools/cc3dsfs_capture (no __init__.py).
test-cc3dsfs:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/cc3dsfs_capture -p 'test_xwd2png.py'

# DS top-screen scale-test suite (tools/ds_scale_test/). Discovered explicitly:
# the aggregate pass below does not descend into tools/ds_scale_test (no
# __init__.py).
test-ds-scale-test:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/ds_scale_test -p 'test_*.py'

# EQ-4g capture-mapping fit suite (tools/ds_scale_test/fit_mapping.py). Run
# explicitly; the discover pass in `test` sets FIT_MAPPING_DISCOVER_SKIP so it
# is not executed twice.
test-fit-mapping:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_fit_mapping.py'

# DS vertex-colour test suite (tools/ds_vtxcolor_test/). Discovered explicitly:
# the aggregate pass below does not descend into tools/ds_vtxcolor_test (no
# __init__.py).
test-ds-vtxcolor-test:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/ds_vtxcolor_test -p 'test_*.py'

# DS polygon-alpha convention test suite (tools/ds_alpha_test/). Discovered
# explicitly: the aggregate pass below does not descend into
# tools/ds_alpha_test (no __init__.py).
test-ds-alpha-test:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/ds_alpha_test -p 'test_*.py'

# DS 3D edge-row origin test suite (tools/ds_edge_test/). Discovered explicitly:
# the aggregate pass below does not descend into tools/ds_edge_test (no
# __init__.py).
test-ds-edge-test:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/ds_edge_test -p 'test_*.py'

# EQ-4o real-hardware row fixtures (tools/eq4o_hw_rows/). Discovered explicitly:
# the aggregate pass below does not descend into tools/eq4o_hw_rows (no
# __init__.py).
test-eq4o-hw-rows:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/eq4o_hw_rows -p 'test_*.py'

# EQ-4w blend-enabled scene-4 hardware measurement (tools/eq4w_blend_hw/).
# Discovered explicitly: the aggregate pass below does not descend into
# tools/eq4w_blend_hw (no __init__.py).
test-eq4w-blend-hw:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/eq4w_blend_hw -p 'test_*.py'

# EQ-4zg partial-viewport fixtures and measurement (tools/eq4_hw_vp/).
# Discovered explicitly: the aggregate pass below does not descend into
# tools/eq4_hw_vp (no __init__.py).
test-eq4zg-vp:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/eq4_hw_vp -p 'test_*.py'

# EQ-4zi DS-mode lighting hardware fixtures (tools/eq4_hw_lit/). Discovered
# explicitly (no __init__.py).
test-eq4zi-lit-hw:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/eq4_hw_lit -p 'test_*.py'

# EQ-4y arm11 blend-gate hardware fixtures (tools/eq4y_gate_hw/). Discovered
# explicitly: the aggregate pass below does not descend into tools/eq4y_gate_hw
# (no __init__.py).
test-eq4y-gate-hw:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/eq4y_gate_hw -p 'test_*.py'

# EQ-4za black-clear hardware fixtures (tools/eq4za_clear_hw/). Discovered
# explicitly: the aggregate pass below does not descend into
# tools/eq4za_clear_hw (no __init__.py).
test-eq4za-clear-hw:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/eq4za_clear_hw -p 'test_*.py'

# EQ-4zb offline blend-residual model (tools/eq4zb_blend_model/). Discovered
# explicitly: the aggregate pass below does not descend into
# tools/eq4zb_blend_model (no __init__.py).
test-eq4zb-blend-model:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/eq4zb_blend_model -p 'test_*.py'

# EQ-4zc arm11 alpha-sweep frame generator, measurer and fixtures
# (tools/eq4zc_alpha_sweep/). Discovered explicitly: the aggregate pass below
# does not descend into tools/eq4zc_alpha_sweep (no __init__.py).
test-eq4zc-alpha-sweep:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/eq4zc_alpha_sweep -p 'test_*.py'

# EQ-4zd real-hardware alpha-sweep crops (tools/eq4zd_sweep_hw/). Discovered
# explicitly: the aggregate pass below does not descend into
# tools/eq4zd_sweep_hw (no __init__.py).
test-eq4zd-sweep-hw:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/eq4zd_sweep_hw -p 'test_*.py'

# EQ-4p presentation-row correction module (tools/azahar_presentation.py). Run
# explicitly; the discover pass in `test` sets AZAHAR_PRESENTATION_DISCOVER_SKIP
# so it is not executed twice.
test-azahar-presentation:
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_azahar_presentation.py'

test: host-test tests/host/build/gx_interp_cli tests/host/build/tex_decode_cli \
	tests/host/build/pica_tex_cli tests/host/build/pica_frame_cli \
	tests/host/build/tex_capture_cli \
	test-eq2 test-eq3 test-eq-scenes test-arm9v2-static test-arm9v2-blend test-arm9v2-lit test-eq4-hw-compare \
	test-fit-mapping test-cc3dsfs test-ds-scale-test test-ds-vtxcolor-test \
	test-ds-alpha-test test-ds-edge-test test-eq4o-hw-rows \
	test-eq4w-blend-hw test-eq4y-gate-hw test-eq4za-clear-hw \
	test-eq4zb-blend-model test-eq4zc-alpha-sweep test-eq4zd-sweep-hw test-eq4zg-vp test-eq4zi-lit-hw \
	test-azahar-presentation
	EQ2_DISCOVER_SKIP=1 EQ3_DISCOVER_SKIP=1 EQ_SCENES_DISCOVER_SKIP=1 \
	ARM9V2_STATIC_DISCOVER_SKIP=1 ARM9V2_BLEND_DISCOVER_SKIP=1 ARM9V2_LIT_DISCOVER_SKIP=1 EQ4_HW_COMPARE_DISCOVER_SKIP=1 \
	FIT_MAPPING_DISCOVER_SKIP=1 AZAHAR_PRESENTATION_DISCOVER_SKIP=1 \
	PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools

clean:
	$(MAKE) -C arm7 clean
	$(MAKE) -C arm9 clean
	$(MAKE) -C arm9v2 clean
	$(MAKE) -C arm11 clean
	$(MAKE) -C arm11_ucode clean
	rm -rf tests/host/build
	rm -rf tools/azahar_host/build
	rm -rf tools/ntr_capture/build
	rm -f twlhd-rtcom.nds
