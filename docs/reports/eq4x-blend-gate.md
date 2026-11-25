<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4x — blend gate: carry `DISP3DCNT` bit 3 to the ARM11 renderer

**Status:** implementation complete. Emulator verification was run (§3-§6); the console run in §7 is a **PLAN** that was not executed.

## 1. Design choice

**Choice:** renderer-side flag (EQ-4u §6.1 option 1). The blend-enable state is
already recorded in the v2 RSTA stream (`0x04000060`, bit 3). A small pure header
`arm11/include/twlhd_blend_gate.h` derives the flag from the last RSTA write at
or before the frame's first `BEGIN_VTXS`. `arm11/source/main.c` passes the result
to the renderer via `renderer_set_blend_enabled()`, and `arm11/source/renderer.c`
selects `twlhd_blend_config_alpha()` (bit set / no entry) vs
`twlhd_blend_config_opaque()` (bit clear) for diagnostic mode 0.

**Why "at or before first draw" rather than per-BEGIN:** the renderer draws the
whole clip frame in one `C3D_DrawArrays` call (`renderer.c:1587-1591`), so a
per-BEGIN flag cannot be applied without splitting the draw path into batches —
a much larger change. Mid-frame blend toggles are UNVERIFIED (see §7).

**Default:** blend **enabled** when no `0x04000060` RSTA entry exists. Every
existing fixture/golden render (eq2/eq3, eq4f scene 5, eq4r scenes) was produced
by the previous unconditional alpha blending; a frame that carries no evidence of
the bit being clear must keep that behaviour. SOURCE-READ: the default is set in
`arm11/include/twlhd_blend_gate.h:32-35`.

## 2. Source changes

SOURCE-READ — new pure header (`arm11/include/twlhd_blend_gate.h`):

```
#define TWLHD_BLEND_GATE_ADDR 0x04000060u
#define TWLHD_BLEND_GATE_BIT  (1u << 3)

typedef struct {
    bool seen;
    bool enabled;
} TwlhdBlendGate;
```

`twlhd_blend_gate_observe()` ignores non-`0x04000060` addresses, ignores writes
after `first_draw_index`, and applies the last qualifying write. The first-draw
scan uses `TWLHD_V2_CMD_BEGIN_VTXS` (`common/twlhd_v2_format.h:124`) and masks
each `cmd_word` with `TWLHD_V2_CMD_ID_MASK` (`common/twlhd_v2_format.h:90`), so
`RAW_FIFO_BIT` entries mask to 0 and cannot be mistaken for a BEGIN.

SOURCE-READ — renderer wiring (`arm11/source/renderer.c`):

```
static int g_blend_enabled = 1;
...
__attribute__((noinline, used)) void renderer_set_blend_enabled(int enabled) {
    g_blend_enabled = enabled ? 1 : 0;
    (void)renderer_get_blend_enabled();
}
...
TwlhdAlphaTestConfig at = twlhd_alpha_test_disabled();
...
if (diag->mode == 0) {
    blend = g_blend_enabled ? twlhd_blend_config_alpha()
                            : twlhd_blend_config_opaque();
    if (!g_blend_enabled) {
        at = twlhd_alpha_test_gt_zero();
    }
} else {
    blend = diag->blend;
}
...
C3D_AlphaTest(at.enable, at.func, at.ref);
```

Diag modes 1/2 keep the table (mode 1 is already opaque, mode 2 const-alpha);
the DEBUG path at `renderer.c:1190` keeps `twlhd_blend_config_opaque()` and
disabled alpha test. The alpha-test override is applied only in diag mode 0
when blending is disabled, so the blend-enabled path and modes 1/2 are
unaffected.

SOURCE-READ — alpha-test helper (`arm11/include/twlhd_blend_config.h`):

```
typedef struct {
    bool enable;
    GPU_TESTFUNC func;
    int ref;
} TwlhdAlphaTestConfig;

static inline TwlhdAlphaTestConfig twlhd_alpha_test_disabled(void) {
    TwlhdAlphaTestConfig cfg = {false, GPU_ALWAYS, 0};
    return cfg;
}

static inline TwlhdAlphaTestConfig twlhd_alpha_test_gt_zero(void) {
    TwlhdAlphaTestConfig cfg = {true, GPU_GREATER, 0};
    return cfg;
}
```

PICA passes a fragment when `alpha > ref`; `GPU_GREATER` with `ref = 0`
discards exactly alpha==0 fragments and passes every alpha>0 fragment unchanged.
Mode 0's TexEnv alpha source is `GPU_PRIMARY_COLOR`
(`twlhd_diag_config.h:56`), so quad3's fragment alpha equals its vertex alpha
(0) and is discarded.

SOURCE-READ — main.c wiring (`arm11/source/main.c`):

```
#include "twlhd_blend_gate.h"
...
if (pending_builtin) {
    int result = renderer_use_builtin_triangle();

    if (result == TWLHD_RENDER_OK) {
        set_builtin_scene();
        renderer_set_blend_enabled(
            twlhd_blend_gate_default().enabled);
        ...
    }
}
...
if (pending_is_v2) {
    ...
    renderer_set_blend_enabled(
        twlhd_blend_gate_from_view(&g_v2view));
} else {
    ...
    renderer_set_blend_enabled(
        twlhd_blend_gate_default().enabled);
}
```

The v2 gate is set only after `renderer_set_clip_scene` / textured staging
succeeds, so a staging failure does not leave the new gate on the old scene. The
builtin-triangle and SD FRAME paths explicitly restore the default
(`twlhd_blend_gate_default().enabled`) because they carry no RSTA stream.

SOURCE-READ — updated structural pins
(`tests/host/test_blend_config.c:108-115`, `:143-178`,
`tests/host/test_diag_config.c:239-246`) now expect one
`twlhd_blend_config_alpha(` and one `twlhd_blend_config_opaque(` call inside
`draw_clip_internal`, plus one `twlhd_alpha_test_gt_zero(` call gated by
`!g_blend_enabled`.

## 3. Host tests

### 3.1 New blend-gate unit test

FACT:

```
$ ./tests/host/build/test_blend_gate
ok   blend-gate/default
ok   blend-gate/bit-set
ok   blend-gate/bit-clear
ok   blend-gate/last-write-wins
ok   blend-gate/after-first-draw-ignored
ok   blend-gate/no-entry-default
ok   blend-gate/other-addresses-ignored
ok   blend-gate/only-bit-3-counts
ok   blend-gate/first-draw-scan-no-begin
9 test(s) passed
```

Cases:
- default: `{seen=0, enabled=1}`;
- bit set (`0x18`): enabled;
- bit clear (`0x10`): opaque;
- last write wins (both orders, both pre-first-draw);
- write after first draw ignored;
- no RSTA entry → default enabled;
- other addresses (`0x04000061`, `0x04000350`) ignored;
- only bit 3 counts (`0x04`, `0x10`, `0xF7` clear; `0x08` set);
- first-draw scan returns `gxcm_count` when there is no BEGIN.

### 3.2 Structural count tests

FACT:

```
$ ./tests/host/build/test_blend_config
ok   blend-config/alpha
ok   blend-config/opaque
ok   blend-config/alpha-test-disabled
ok   blend-config/alpha-test-gt-zero
ok   blend-config/renderer-call-sites
5 test(s) passed

$ ./tests/host/build/test_diag_config
ok   diag-config/mode-0-normal
ok   diag-config/mode-1-alpha-as-gray
ok   diag-config/mode-2-const-alpha-half
ok   diag-config/mode-separation
ok   diag-config/cycle
ok   diag-config/source-files/main
ok   diag-config/source-files/renderer
7 test(s) passed

$ ./tests/host/build/test_blend_gate_wiring
ok   blend-gate-wiring/call-sites
1 test(s) passed
```

`test_blend_gate_wiring` asserts exactly three
`renderer_set_blend_enabled(` call sites in `arm11/source/main.c` and verifies
that the builtin-commit, SD FRAME, and SD v2 regions each contain one. A
negative self-check overwrites every occurrence in an in-memory copy and asserts
the mutated copy reports zero, so the test cannot pass vacuously.

### 3.3 Full `make test`

FACT (tail; extra run — the original `tail -12` log was not preserved, so the
command was re-run):

```
$ make test 2>&1 | tail -n 13
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
ssssssss.....sssssssssssssssssssss..........................................................................................................usage: compare_texture_capture.py <captured.bin> [angle]
usage: compare_texture_capture.py <captured.bin> [angle]
error: angle must be an integer
........ssssssssssssssssssssssssssss...ssssssssssssssssssssssssssssssssssssssssssssssss...........................................................................................................................................................................................................
----------------------------------------------------------------------
Ran 430 tests in 91.913s

OK (skipped=105)
/tmp/tmp71z0c52y/twlhd_frame_v2_tex_over.bin
vertex_count=36
max_clip_deviation=0.007324
colour_exactness=PASS
```

No existing test was weakened or deleted; `common/`, `tools/gx_interp_oracle.py`,
`arm9v2/`, `arm7/`, and `tools/azahar_host/` were not modified.

The `make test` target (`Makefile:668-674`) already includes `host-test`,
`test-eq-scenes`, `test-arm9v2-static`, `test-arm9v2-blend`, `test-eq4-hw-compare`,
`test-fit-mapping`, `test-cc3dsfs`, `test-ds-scale-test`,
`test-ds-vtxcolor-test`, `test-ds-alpha-test`, `test-ds-edge-test`,
`test-eq4o-hw-rows`, `test-eq4w-blend-hw`, and `test-azahar-presentation`
(among others). The quoted §3.3 tail confirms `test_eq_scenes.py` ran inside the
final `unittest discover -s tools` pass.

### 3.4 Emulator check targets outside `make test`

FACT — `emu-validate` (not part of `make test`; `DESMUME_CLI=<redacted>/desmume-build/cli/desmume-cli`):

```
$ (DESMUME_CLI=<redacted>/desmume-build/cli/desmume-cli make emu-validate 2>&1; echo "exit=$?") | tail -n 15
+ cp tests/host/build/emu/capture/fat/twlhd_frame_v2.bin tests/host/build/emu/capture/twlhd_frame_v2.bin
+ echo 'capture: tests/host/build/emu/capture/twlhd_frame_v2.bin'
capture: tests/host/build/emu/capture/twlhd_frame_v2.bin
tools/emulator/validate_with_parsers.sh tests/host/build/emu/capture/twlhd_frame_v2.bin
+ gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I<redacted>/common /tmp/tmp.e6GgqkLKSK/v2_dump_cli.c /tmp/tmp.e6GgqkLKSK/twlhd_v2_parse.c -o /tmp/tmp.e6GgqkLKSK/v2_dump_cli
+ /tmp/tmp.e6GgqkLKSK/v2_dump_cli tests/host/build/emu/capture/twlhd_frame_v2.bin
+ python3 /tmp/tmp.e6GgqkLKSK/twlhd_v2_dump.py validate tests/host/build/emu/capture/twlhd_frame_v2.bin
+ python3 /tmp/tmp.e6GgqkLKSK/twlhd_v2_dump.py stats tests/host/build/emu/capture/twlhd_frame_v2.bin
+ set +x
parsers: OK (entries=124 rsta=4)
python3 tools/emulator/compare_capture.py \
	tests/host/build/emu/capture/twlhd_frame_v2.bin \
	tests/host/build/emu/expected_frame.txt 61440
PASS: 124 GXCM + 4 RSTA entries match for angle 61440
exit=0
```

FACT — `emu-validate-tex` (not part of `make test`; `DESMUME_CLI=<redacted>/desmume-build/cli/desmume-cli`):

```
$ (DESMUME_CLI=<redacted>/desmume-build/cli/desmume-cli make emu-validate-tex 2>&1; echo "exit=$?") | tail -n 15
+ cp tests/host/build/emu/capture-tex/fat/twlhd_frame_v2.bin tests/host/build/emu/capture-tex/twlhd_frame_v2.bin
+ echo 'capture: tests/host/build/emu/capture-tex/twlhd_frame_v2.bin'
capture: tests/host/build/emu/capture-tex/twlhd_frame_v2.bin
tools/emulator/validate_with_parsers.sh tests/host/build/emu/capture-tex/twlhd_frame_v2.bin
+ gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I<redacted>/common /tmp/tmp.y8eM09S7hU/v2_dump_cli.c /tmp/tmp.y8eM09S7hU/twlhd_v2_parse.c -o /tmp/tmp.y8eM09S7hU/v2_dump_cli
+ /tmp/tmp.y8eM09S7hU/v2_dump_cli tests/host/build/emu/capture-tex/twlhd_frame_v2.bin
+ python3 /tmp/tmp.y8eM09S7hU/twlhd_v2_dump.py validate tests/host/build/emu/capture-tex/twlhd_frame_v2.bin
+ python3 /tmp/tmp.y8eM09S7hU/twlhd_v2_dump.py stats tests/host/build/emu/capture-tex/twlhd_frame_v2.bin
+ set +x
parsers: OK (entries=78 rsta=5)
python3 tools/emulator/compare_texture_capture.py \
	tests/host/build/emu/capture-tex/twlhd_frame_v2.bin 61440
PASS: SCENE=5 texture 0x0C000000/0x00000000, 32 texel + 32 palette bytes for angle 61440
exit=0
```

Build-only or opt-in targets that remain outside `make test`:
`emu-expected`, `azahar-host`, `ntr-capture`, `ntr-input`.

## 4. ROM and 3DSX builds

All ROMs were built from the worktree; the 3DSX files were built once from
their respective sources. Scratch outputs live under
`/tmp/opencode/eq4x-verify/` and are not committed. (The two scene-4 ROMs were
built twice because the first builds were removed by `make clean` between
variants; the final hashes match the first builds.)

**Disclosure:** after editing `arm11/source/main.c`, the NEW 3DSX was rebuilt and
the NEW Azahar renders in §5.3/§5.5/§6 were re-run once each. The OLD 3DSX and
OLD renders were not rebuilt.

FACT — Azahar host:

```
$ make azahar-host
mkdir -p tools/azahar_host/build
cc -std=c11 -Wall -Wextra -Werror -O2 -D_POSIX_C_SOURCE=200809L \
	-Itools/azahar_host  \
	-o tools/azahar_host/build/azahar_host tools/azahar_host/azahar_host.c -ldl
```

FACT — scene 4 non-blend ROM:

```
$ make -C arm9v2 clean && make -C arm9v2 SCENE=4 STATIC_ANGLE=30 AUTODUMP_FRAME=240
...
built ... twlhd-arm9v2-autodump-s4-st30.nds
$ sha256sum arm9v2/twlhd-arm9v2-autodump-s4-st30.nds
0f63a52595c3774aa1b7400b4c112c3062c08a6dd788f28939f14bd962629ff7  arm9v2/twlhd-arm9v2-autodump-s4-st30.nds
```

FACT — scene 4 blend ROM:

```
$ make -C arm9v2 clean && make -C arm9v2 SCENE=4 STATIC_ANGLE=30 AUTODUMP_FRAME=240 BLEND=1
...
built ... twlhd-arm9v2-autodump-s4-bl-st30.nds
$ sha256sum arm9v2/twlhd-arm9v2-autodump-s4-bl-st30.nds
64f60afcddc39748affb33d69e19411d21f4137e09a105d3c7901a1044956d8a  arm9v2/twlhd-arm9v2-autodump-s4-bl-st30.nds
```

FACT — scene 5 regression ROM:

```
$ make -C arm9v2 clean && make -C arm9v2 SCENE=5 STATIC_ANGLE=30 AUTODUMP_FRAME=240
...
built ... twlhd-arm9v2-autodump-s5-st30.nds
$ sha256sum arm9v2/twlhd-arm9v2-autodump-s5-st30.nds
30337896a415a433376f8478db1ebe973ce401f4bb657d9d9b878bb27ff2063e  arm9v2/twlhd-arm9v2-autodump-s5-st30.nds
```

FACT — OLD 3DSX from a clean `main` checkout:

```
$ git rev-parse main
eda28c51eb6beb186130f2d68d6dd6f7c07b8711
$ make -C <redacted-main-checkout>/arm11 -B
...
built ... twlhd-arm11.3dsx
$ sha256sum <redacted-main-checkout>/arm11/twlhd-arm11.3dsx /tmp/opencode/eq4x-verify/old.3dsx
c078f7be0ae4b526df4ea38dde82a3fc49c54f9faeee0a6e130916bc997c730c  <redacted-main-checkout>/arm11/twlhd-arm11.3dsx
c078f7be0ae4b526df4ea38dde82a3fc49c54f9faeee0a6e130916bc997c730c  /tmp/opencode/eq4x-verify/old.3dsx
```

FACT — NEW 3DSX from this worktree (post-fix):

```
$ git rev-parse HEAD
a0eea169fe6fae37ee8f4fc41e2bcc558495ff04
$ make -C arm11 -B
...
built ... twlhd-arm11.3dsx
$ sha256sum /tmp/opencode/eq4x-verify/new.3dsx
2dc7d386d611582e067a2ed1aecffbadbad517625b18f4b9ecf5adf5d3d87693  /tmp/opencode/eq4x-verify/new.3dsx
```

The `cp` commands that copied the 3DSX files into `/tmp/opencode/eq4x-verify/`
were not recorded in shell history or logs. For OLD, a fresh read-only
`sha256sum` of the build output and the copied file shows the same hash. For
NEW, the worktree build output `arm11/twlhd-arm11.3dsx` is no longer present, so
only the copied file hash is shown; it matches the previously quoted value.

The build-ID line embeds the short git hash (`arm11/Makefile:75-84`); the OLD/NEW
hashes above are valid only for the stated HEADs and one build each.

SOURCE-READ — scene 4 blend off vs on:

```
$ sed -n '350,356p' arm9v2/source/demo_frame.cpp
    glEnable(GL_ANTIALIAS);
#ifdef TWLHD_DEMO_BLEND
    /* Falsification build (EQ-4u §6.3): enable DS alpha blending. */
    glEnable(GL_BLEND);
#endif
```

The non-blend ROM therefore writes only `GL_ANTIALIAS` to `DISP3DCNT`; the blend
ROM additionally sets `GL_BLEND`. SOURCE-READ: `glEnable` is `GFX_CONTROL |= bits`
(`$DEVKITPRO/libnds/include/nds/arm9/videoGL.h:186-188`), so the recorded value is
`0x10` (ANTIALIAS only) for (a) and `0x18` (ANTIALIAS | BLEND) for (b).

SOURCE-READ — scene 5 does not enable blending:

```
$ sed -n '388,393p' arm9v2/source/demo_frame.cpp
#if TWLHD_DEMO_SCENE == 5
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE);
#else
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE);
#endif
```

Scene 5 has no `glEnable(GL_BLEND)` and every polygon is `POLY_ALPHA(31)`, so
alpha is 1.0 and `SRC_ALPHA/ONE_MINUS_SRC_ALPHA` is byte-identical to `ONE/ZERO`.

## 5. Emulator verification

### 5.1 Captures

FACT — DeSmuME captures (each once, `DESMUME_CLI=<redacted>/desmume-build/cli/desmume-cli`):

```
$ DESMUME_CLI=<redacted>/desmume-build/cli/desmume-cli \
    tools/emulator/desmume_capture.sh arm9v2/twlhd-arm9v2-autodump-s4-st30.nds \
    /tmp/opencode/eq4x-verify/cap-s4 2>&1 | tail -3
+ cp /tmp/opencode/eq4x-verify/cap-s4/fat/twlhd_frame_v2.bin /tmp/opencode/eq4x-verify/cap-s4/twlhd_frame_v2.bin
+ echo 'capture: /tmp/opencode/eq4x-verify/cap-s4/twlhd_frame_v2.bin'
capture: /tmp/opencode/eq4x-verify/cap-s4/twlhd_frame_v2.bin

$ DESMUME_CLI=<redacted>/desmume-build/cli/desmume-cli \
    tools/emulator/desmume_capture.sh arm9v2/twlhd-arm9v2-autodump-s4-bl-st30.nds \
    /tmp/opencode/eq4x-verify/cap-s4bl 2>&1 | tail -3
+ cp /tmp/opencode/eq4x-verify/cap-s4bl/fat/twlhd_frame_v2.bin /tmp/opencode/eq4x-verify/cap-s4bl/twlhd_frame_v2.bin
+ echo 'capture: /tmp/opencode/eq4x-verify/cap-s4bl/twlhd_frame_v2.bin'
capture: /tmp/opencode/eq4x-verify/cap-s4bl/twlhd_frame_v2.bin

$ DESMUME_CLI=<redacted>/desmume-build/cli/desmume-cli \
    tools/emulator/desmume_capture.sh arm9v2/twlhd-arm9v2-autodump-s5-st30.nds \
    /tmp/opencode/eq4x-verify/cap-s5 2>&1 | tail -3
+ cp /tmp/opencode/eq4x-verify/cap-s5/fat/twlhd_frame_v2.bin /tmp/opencode/eq4x-verify/cap-s5/twlhd_frame_v2.bin
+ echo 'capture: /tmp/opencode/eq4x-verify/cap-s5/twlhd_frame_v2.bin'
capture: /tmp/opencode/eq4x-verify/cap-s5/twlhd_frame_v2.bin
```

### 5.2 RSTA evidence

FACT — dump of the `0x04000060` RSTA entries:

```
$ python3 - <<'EOF'
import sys
sys.path.insert(0, 'tools')
from twlhd_v2_dump import parse

def dump(path):
    with open(path, 'rb') as f:
        data = f.read()
    view = parse(data)
    print(f'{path}: rsta_count={len(view.rsta)} gxcm_count={len(view.gxcm)}')
    found = False
    for addr, value, gx_index, width in view.rsta:
        if addr == 0x04000060:
            found = True
            bit3 = (value >> 3) & 1
            print(f'  addr=0x{addr:08x} value=0x{value:08x} bit3={bit3} gx_index={gx_index} width={width}')
    if not found:
        print('  (no 0x04000060 entries)')

dump('/tmp/opencode/eq4x-verify/cap-s4/twlhd_frame_v2.bin')
dump('/tmp/opencode/eq4x-verify/cap-s4bl/twlhd_frame_v2.bin')
dump('/tmp/opencode/eq4x-verify/cap-s5/twlhd_frame_v2.bin')
EOF
/tmp/opencode/eq4x-verify/cap-s4/twlhd_frame_v2.bin: rsta_count=4 gxcm_count=121
  addr=0x04000060 value=0x00000010 bit3=0 gx_index=0 width=2
/tmp/opencode/eq4x-verify/cap-s4bl/twlhd_frame_v2.bin: rsta_count=5 gxcm_count=121
  addr=0x04000060 value=0x00000018 bit3=1 gx_index=0 width=2
  addr=0x04000060 value=0x00000018 bit3=1 gx_index=0 width=2
/tmp/opencode/eq4x-verify/cap-s5/twlhd_frame_v2.bin: rsta_count=5 gxcm_count=78
  addr=0x04000060 value=0x00000011 bit3=0 gx_index=0 width=2
  addr=0x04000060 value=0x00000011 bit3=0 gx_index=62 width=2
```

Scene 4 (a): bit 3 clear (`0x10`). Scene 4 (b): bit 3 set (`0x18`). Scene 5: bit 3
clear (`0x11`), confirming the source read in §4.

### 5.3 Azahar renders

FACT — render commands and completion (each frame rendered once per 3DSX with
`--core <redacted>/azahar_libretro.so`):

```
$ tools/azahar_host/build/azahar_host \
    --core <redacted>/azahar_libretro.so \
    --game /tmp/opencode/eq4x-verify/old.3dsx \
    --frames 200 --press A:60-63 --shot 199 \
    --out /tmp/opencode/eq4x-verify/s4-old \
    --sd-root /tmp/opencode/eq4x-verify/sd-s4 2>&1 | tail -2
[azahar_host] wrote /tmp/opencode/eq4x-verify/s4-old_199.ppm (400x480)
[azahar_host] wrote /tmp/opencode/eq4x-verify/s4-old_last.ppm (400x480)

$ tools/azahar_host/build/azahar_host \
    --core <redacted>/azahar_libretro.so \
    --game /tmp/opencode/eq4x-verify/new.3dsx \
    --frames 200 --press A:60-63 --shot 199 \
    --out /tmp/opencode/eq4x-verify/s4-new \
    --sd-root /tmp/opencode/eq4x-verify/sd-s4 2>&1 | tail -2
[azahar_host] wrote /tmp/opencode/eq4x-verify/s4-new_199.ppm (400x480)
[azahar_host] wrote /tmp/opencode/eq4x-verify/s4-new_last.ppm (400x480)

$ tools/azahar_host/build/azahar_host \
    --core <redacted>/azahar_libretro.so \
    --game /tmp/opencode/eq4x-verify/old.3dsx \
    --frames 200 --press A:60-63 --shot 199 \
    --out /tmp/opencode/eq4x-verify/s4bl-old \
    --sd-root /tmp/opencode/eq4x-verify/sd-s4bl 2>&1 | tail -2
[azahar_host] wrote /tmp/opencode/eq4x-verify/s4bl-old_199.ppm (400x480)
[azahar_host] wrote /tmp/opencode/eq4x-verify/s4bl-old_last.ppm (400x480)

$ tools/azahar_host/build/azahar_host \
    --core <redacted>/azahar_libretro.so \
    --game /tmp/opencode/eq4x-verify/new.3dsx \
    --frames 200 --press A:60-63 --shot 199 \
    --out /tmp/opencode/eq4x-verify/s4bl-new \
    --sd-root /tmp/opencode/eq4x-verify/sd-s4bl 2>&1 | tail -2
[azahar_host] wrote /tmp/opencode/eq4x-verify/s4bl-new_199.ppm (400x480)
[azahar_host] wrote /tmp/opencode/eq4x-verify/s4bl-new_last.ppm (400x480)
```

FACT — full 400×480 byte comparison of OLD(b) vs NEW(b):

```
$ cmp /tmp/opencode/eq4x-verify/s4bl-old_199.ppm /tmp/opencode/eq4x-verify/s4bl-new_199.ppm
/tmp/opencode/eq4x-verify/s4bl-old_199.ppm /tmp/opencode/eq4x-verify/s4bl-new_199.ppm differ: byte 308737, line 4
$ echo $?
1
$ sha256sum /tmp/opencode/eq4x-verify/s4bl-old_199.ppm /tmp/opencode/eq4x-verify/s4bl-new_199.ppm
47c732c07688dfdf896419b27cafd08ab5e506d2f4ef30b20b65dd9f69dff353  /tmp/opencode/eq4x-verify/s4bl-old_199.ppm
97f2213a04a4f059ad20617f29a895ff297daf604c931d0678620dae7e45fcdf  /tmp/opencode/eq4x-verify/s4bl-new_199.ppm
```

The full PPMs differ because the bottom-screen build-ID line embeds the short
git hash (`eda28c51` for OLD, `a0eea16` for NEW). The top 400×240 render areas
are byte-identical (see §5.5).

### 5.4 Crop to 400×240 for measurement

FACT:

```
$ python3 - <<'EOF'
from PIL import Image
for name in ['s4-old','s4-new','s4bl-old','s4bl-new']:
    ppm = f'/tmp/opencode/eq4x-verify/{name}_199.ppm'
    png = f'/tmp/opencode/eq4x-verify/{name}_top400x240.png'
    with open(ppm, 'rb') as f:
        header = b''
        while b'\n' not in header or header.count(b'\n') < 3:
            header += f.read(1)
        parts = header.split()
        w, h = int(parts[1]), int(parts[2])
        data = f.read()
    img = Image.frombytes('RGB', (w, h), data)
    top = img.crop((0, 0, 400, 240))
    top.save(png)
    print(f'{png} {top.size}')
EOF
/tmp/opencode/eq4x-verify/s4-old_top400x240.png (400, 240)
/tmp/opencode/eq4x-verify/s4-new_top400x240.png (400, 240)
/tmp/opencode/eq4x-verify/s4bl-old_top400x240.png (400, 240)
/tmp/opencode/eq4x-verify/s4bl-new_top400x240.png (400, 240)
```

### 5.5 Per-quad colours (`measure_blend_hw.py`)

FACT — OLD render of non-blend frame (a) (still unconditionally blending):

```
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py /tmp/opencode/eq4x-verify/s4-old_top400x240.png
tool=measure_blend_hw
basis=one console, one capture session; scene=4 static_angle=30
image=400x240
capture_mapping=scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
projection=perspective(70,1.333333,0.1,100) look_at=0,0,4 rotate_x=2730 rotate_y=2730 scale=2 viewport=0,0,255,191
derivation=clip=P*LookAt*Rx*Ry*Scale(2); ndc=clip.xy/clip.w; ds_x=(ndc_x+1)*128; ds_y_top=(1-ndc_y)*96; capture_x=40+1.25*ds_x; capture_y=1.25*ds_y_top
quad0.source_rgb=255,0,0 alpha=31
quad0.rect=150.885,125.766,193.268,162.786
quad0.interior=153,128,191,161
quad0.majority_rgb=255,0,0
quad0.source_channel=0
quad0.source_value=255
quad0.measured=255
quad0.blended=no
quad1.source_rgb=0,255,0 alpha=16
quad1.rect=206.478,112.166,238.177,144.043
quad1.interior=208,114,236,142
quad1.majority_rgb=7,142,11
quad1.source_channel=1
quad1.source_value=255
quad1.measured=142
quad1.blended=yes
quad2.source_rgb=0,0,255 alpha=4
quad2.rect=143.928,89.476,192.503,130.364
quad2.interior=146,91,191,128
quad2.majority_rgb=13,13,60
quad2.source_channel=2
quad2.source_value=255
quad2.measured=60
quad2.blended=yes
quad3.source_rgb=255,255,255 alpha=0
quad3.rect=207.183,79.642,242.251,114.336
quad3.interior=209,82,240,112
quad3.majority_rgb=16,16,24
quad3.classification=none
quad3.bright_rule=max_rgb_gt_60
quad3.region=207,80,242,114 region_pixels=1260
quad3.bright_pixels=5
quad3.bright_bbox=234,113,237,114
quad3.interior=209,82,240,112
quad3.interior_pixels=992
quad3.interior_bright_pixels=0
census_top=16
census=(16, 16, 24) 92965
census=(13, 13, 60) 1411
census=(255, 0, 0) 981
census=(7, 142, 11) 642
census=(0, 0, 0) 1
```

FACT — NEW render of non-blend frame (a) (gate clear → opaque), post-fix
(build HEAD `a0eea169fe6fae37ee8f4fc41e2bcc558495ff04`):

```
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py /tmp/opencode/eq4x-verify/s4-new_top400x240.png
tool=measure_blend_hw
basis=one console, one capture session; scene=4 static_angle=30
image=400x240
capture_mapping=scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
projection=perspective(70,1.333333,0.1,100) look_at=0,0,4 rotate_x=2730 rotate_y=2730 scale=2 viewport=0,0,255,191
derivation=clip=P*LookAt*Rx*Ry*Scale(2); ndc=clip.xy/clip.w; ds_x=(ndc_x+1)*128; ds_y_top=(1-ndc_y)*96; capture_x=40+1.25*ds_x; capture_y=1.25*ds_y_top
quad0.source_rgb=255,0,0 alpha=31
quad0.rect=150.885,125.766,193.268,162.786
quad0.interior=153,128,191,161
quad0.majority_rgb=255,0,0
quad0.source_channel=0
quad0.source_value=255
quad0.measured=255
quad0.blended=no
quad1.source_rgb=0,255,0 alpha=16
quad1.rect=206.478,112.166,238.177,144.043
quad1.interior=208,114,236,142
quad1.majority_rgb=0,255,0
quad1.source_channel=1
quad1.source_value=255
quad1.measured=255
quad1.blended=no
quad2.source_rgb=0,0,255 alpha=4
quad2.rect=143.928,89.476,192.503,130.364
quad2.interior=146,91,191,128
quad2.majority_rgb=0,0,255
quad2.source_channel=2
quad2.source_value=255
quad2.measured=255
quad2.blended=no
quad3.source_rgb=255,255,255 alpha=0
quad3.rect=207.183,79.642,242.251,114.336
quad3.interior=209,82,240,112
quad3.majority_rgb=16,16,24
quad3.classification=none
quad3.bright_rule=max_rgb_gt_60
quad3.region=207,80,242,114 region_pixels=1260
quad3.bright_pixels=5
quad3.bright_bbox=234,113,237,114
quad3.interior=209,82,240,112
quad3.interior_pixels=992
quad3.interior_bright_pixels=0
census_top=16
census=(16, 16, 24) 92965
census=(0, 0, 255) 1411
census=(255, 0, 0) 981
census=(0, 255, 0) 642
census=(0, 0, 0) 1
```

ACCEPTANCE: NEW(a) quad1 `(0,255,0)` and quad2 `(0,0,255)` are `blended=no`,
and quad3's alpha-0 interior is invisible (`interior_bright_pixels=0`),
matching the opaque hardware capture (non-blend fixture). NEW(a) differs from
OLD(a): quad1 goes 142 -> 255 and quad2 goes 60 -> 255, both now `blended=no`.

FACT — OLD render of blend frame (b):

```
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py /tmp/opencode/eq4x-verify/s4bl-old_top400x240.png
tool=measure_blend_hw
basis=one console, one capture session; scene=4 static_angle=30
image=400x240
capture_mapping=scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
projection=perspective(70,1.333333,0.1,100) look_at=0,0,4 rotate_x=2730 rotate_y=2730 scale=2 viewport=0,0,255,191
derivation=clip=P*LookAt*Rx*Ry*Scale(2); ndc=clip.xy/clip.w; ds_x=(ndc_x+1)*128; ds_y_top=(1-ndc_y)*96; capture_x=40+1.25*ds_x; capture_y=1.25*ds_y_top
quad0.source_rgb=255,0,0 alpha=31
quad0.rect=150.885,125.766,193.268,162.786
quad0.interior=153,128,191,161
quad0.majority_rgb=255,0,0
quad0.source_channel=0
quad0.source_value=255
quad0.measured=255
quad0.blended=no
quad1.source_rgb=0,255,0 alpha=16
quad1.rect=206.478,112.166,238.177,144.043
quad1.interior=208,114,236,142
quad1.majority_rgb=7,142,11
quad1.source_channel=1
quad1.source_value=255
quad1.measured=142
quad1.blended=yes
quad2.source_rgb=0,0,255 alpha=4
quad2.rect=143.928,89.476,192.503,130.364
quad2.interior=146,91,191,128
quad2.majority_rgb=13,13,60
quad2.source_channel=2
quad2.source_value=255
quad2.measured=60
quad2.blended=yes
quad3.source_rgb=255,255,255 alpha=0
quad3.rect=207.183,79.642,242.251,114.336
quad3.interior=209,82,240,112
quad3.majority_rgb=16,16,24
quad3.classification=none
quad3.bright_rule=max_rgb_gt_60
quad3.region=207,80,242,114 region_pixels=1260
quad3.bright_pixels=5
quad3.bright_bbox=234,113,237,114
quad3.interior=209,82,240,112
quad3.interior_pixels=992
quad3.interior_bright_pixels=0
census_top=16
census=(16, 16, 24) 92965
census=(13, 13, 60) 1411
census=(255, 0, 0) 981
census=(7, 142, 11) 642
census=(0, 0, 0) 1
```

FACT — NEW render of blend frame (b) (top crop byte-identical to OLD(b), so the
numbers are the same as OLD(b) above):

```
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py /tmp/opencode/eq4x-verify/s4bl-new_top400x240.png
tool=measure_blend_hw
basis=one console, one capture session; scene=4 static_angle=30
image=400x240
capture_mapping=scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
projection=perspective(70,1.333333,0.1,100) look_at=0,0,4 rotate_x=2730 rotate_y=2730 scale=2 viewport=0,0,255,191
derivation=clip=P*LookAt*Rx*Ry*Scale(2); ndc=clip.xy/clip.w; ds_x=(ndc_x+1)*128; ds_y_top=(1-ndc_y)*96; capture_x=40+1.25*ds_x; capture_y=1.25*ds_y_top
quad0.source_rgb=255,0,0 alpha=31
quad0.rect=150.885,125.766,193.268,162.786
quad0.interior=153,128,191,161
quad0.majority_rgb=255,0,0
quad0.source_channel=0
quad0.source_value=255
quad0.measured=255
quad0.blended=no
quad1.source_rgb=0,255,0 alpha=16
quad1.rect=206.478,112.166,238.177,144.043
quad1.interior=208,114,236,142
quad1.majority_rgb=7,142,11
quad1.source_channel=1
quad1.source_value=255
quad1.measured=142
quad1.blended=yes
quad2.source_rgb=0,0,255 alpha=4
quad2.rect=143.928,89.476,192.503,130.364
quad2.interior=146,91,191,128
quad2.majority_rgb=13,13,60
quad2.source_channel=2
quad2.source_value=255
quad2.measured=60
quad2.blended=yes
quad3.source_rgb=255,255,255 alpha=0
quad3.rect=207.183,79.642,242.251,114.336
quad3.interior=209,82,240,112
quad3.majority_rgb=16,16,24
quad3.classification=none
quad3.bright_rule=max_rgb_gt_60
quad3.region=207,80,242,114 region_pixels=1260
quad3.bright_pixels=5
quad3.bright_bbox=234,113,237,114
quad3.interior=209,82,240,112
quad3.interior_pixels=992
quad3.interior_bright_pixels=0
census_top=16
census=(16, 16, 24) 92965
census=(13, 13, 60) 1411
census=(255, 0, 0) 981
census=(7, 142, 11) 642
census=(0, 0, 0) 1
```

ACCEPTANCE: NEW(b) top-cropped render is byte-identical to OLD(b) and dims the
alpha-16/4 quads over the PICA clear colour (`census_top=(16,16,24)`), exactly
as before. The full 400x480 PPMs differ only in the bottom-screen build-ID line
because OLD(b) was built at HEAD `eda28c51eb6beb186130f2d68d6dd6f7c07b8711` and
NEW(b) at `a0eea169fe6fae37ee8f4fc41e2bcc558495ff04`.

FACT — top-cropped byte identity of NEW(b) vs OLD(b):

```
$ python3 - <<'EOF'
from PIL import Image
for n in ('s4bl-old', 's4bl-new'):
    ppm = f'/tmp/opencode/eq4x-verify/{n}_199.ppm'
    out = f'/tmp/opencode/eq4x-verify/{n}_top400x240.ppm'
    with open(ppm, 'rb') as f:
        header = b''
        while b'\n' not in header or header.count(b'\n') < 3:
            header += f.read(1)
        parts = header.split()
        w, h = int(parts[1]), int(parts[2])
        data = f.read()
    img = Image.frombytes('RGB', (w, h), data)
    top = img.crop((0, 0, 400, 240))
    top.save(out, 'PPM')
    print(f'{out} {top.size}')
EOF
/tmp/opencode/eq4x-verify/s4bl-old_top400x240.ppm (400, 240)
/tmp/opencode/eq4x-verify/s4bl-new_top400x240.ppm (400, 240)
$ cmp /tmp/opencode/eq4x-verify/s4bl-old_top400x240.ppm /tmp/opencode/eq4x-verify/s4bl-new_top400x240.ppm
$ echo $?
0
$ sha256sum /tmp/opencode/eq4x-verify/s4bl-old_top400x240.ppm /tmp/opencode/eq4x-verify/s4bl-new_top400x240.ppm
51baf44b5ff55ce4e3cefd2f8b43391eacac121242eb5d6b95cc1840e6e34d28  /tmp/opencode/eq4x-verify/s4bl-old_top400x240.ppm
51baf44b5ff55ce4e3cefd2f8b43391eacac121242eb5d6b95cc1840e6e34d28  /tmp/opencode/eq4x-verify/s4bl-new_top400x240.ppm
```

### 5.6 Hardware fixture comparison (`eq4_hw_compare.py`)

FACT — OLD(a) vs non-blend hardware fixture
(build HEAD `eda28c51eb6beb186130f2d68d6dd6f7c07b8711`):

```
$ python3 tools/eq4_hw_compare.py \
    --capture tools/fixtures/eq4_hw_scenes/s4_a_top400x240.png \
    --reference /tmp/opencode/eq4x-verify/s4-old_199.ppm 2>&1; echo "exit=$?"
presentation-row correction: applied (row-0 wrap signature detected)
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=15.9668
ds.mean_abs_g=16.7612
ds.mean_abs_b=26.9291
ds.max_error=235.0000
ds.frac_within=0.0148
ds.frac_within_flat=1.0000
ds.frac_within_edge=0.0042
ds.flat_pixels=523
ds.edge_pixels=48629
capture.mean_abs_r=15.9577
capture.mean_abs_g=16.8496
capture.mean_abs_b=27.0526
capture.max_error=239.0000
capture.frac_within=0.0133
capture.frac_within_flat=1.0000
capture.frac_within_edge=0.0026
capture.flat_pixels=821
capture.edge_pixels=75979
ref_nonbg_pixels=1940
ref_bbox_left=83
ref_bbox_top=72
ref_bbox_right=157
ref_bbox_bottom=128
capture_nonbg_pixels=2479
capture_bbox_left=82
capture_bbox_top=62
capture_bbox_right=161
capture_bbox_bottom=130
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=1
class_white_capture_centroid_x=140.0000
class_white_capture_centroid_y=88.0000
class_red_ref_area=625
class_red_ref_centroid_x=105.3536
class_red_ref_centroid_y=114.1264
class_red_capture_area=634
class_red_capture_centroid_x=105.2145
class_red_capture_centroid_y=114.0268
class_green_ref_area=0
class_green_ref_centroid_x=0.0000
class_green_ref_centroid_y=0.0000
class_green_capture_area=443
class_green_capture_centroid_x=144.5192
class_green_capture_centroid_y=101.1038
class_blue_ref_area=0
class_blue_ref_centroid_x=0.0000
class_blue_ref_centroid_y=0.0000
class_blue_capture_area=962
class_blue_capture_centroid_x=102.3576
class_blue_capture_centroid_y=86.3919
class_yellow_ref_area=0
class_yellow_ref_centroid_x=0.0000
class_yellow_ref_centroid_y=0.0000
class_yellow_capture_area=0
class_yellow_capture_centroid_x=0.0000
class_yellow_capture_centroid_y=0.0000
bbox_dev_left=1
bbox_dev_top=10
bbox_dev_right=4
bbox_dev_bottom=2
verdict=FAIL
reason=bbox-top deviates 10 DS px (tol 3); bbox-right deviates 4 DS px (tol 3)
exit=1
```

FACT — hash check of the OLD(a) and OLD(b) renders (read-only re-check, this
round):

```
$ sha256sum /tmp/opencode/eq4x-verify/s4-old_199.ppm /tmp/opencode/eq4x-verify/s4bl-old_199.ppm
47c732c07688dfdf896419b27cafd08ab5e506d2f4ef30b20b65dd9f69dff353  /tmp/opencode/eq4x-verify/s4-old_199.ppm
47c732c07688dfdf896419b27cafd08ab5e506d2f4ef30b20b65dd9f69dff353  /tmp/opencode/eq4x-verify/s4bl-old_199.ppm
```

Provenance: `s4-old_199.ppm` was produced by the §5.3 OLD(a) render command
(`--game /tmp/opencode/eq4x-verify/old.3dsx --out /tmp/opencode/eq4x-verify/s4-old
--sd-root /tmp/opencode/eq4x-verify/sd-s4`) and has not been overwritten or
re-rendered since then. The OLD 3dsx ignores the blend gate, so OLD(a) and
OLD(b) renders of this static scene are expected to be identical; the quoted
hashes confirm they are byte-identical. The §5.5 OLD(a) and OLD(b)
`measure_blend_hw.py` outputs also show the same blended quad values (green
142, blue 60), consistent with this.

The first quoted OLD(a) block (repair round 2, `git show 6fef720`) showed
`ds.mean_abs_g=15.6673` / `ds.mean_abs_b=23.3460`, which are the NEW(a) values,
not an OLD render measurement. Repair round 3 re-ran the same command on the
existing `s4-old_199.ppm` and produced the values above (`ds.mean_abs_b=26.9291`).
The round-3 note therefore misstated the cause: the file did not change; the
earlier block was a copy-paste error. The quoted block above is the round-3
re-run on the existing `s4-old_199.ppm`.


FACT — NEW(a) vs non-blend hardware fixture, post-fix
(build HEAD `a0eea169fe6fae37ee8f4fc41e2bcc558495ff04`):

```
$ python3 tools/eq4_hw_compare.py \
    --capture tools/fixtures/eq4_hw_scenes/s4_a_top400x240.png \
    --reference /tmp/opencode/eq4x-verify/s4-new_199.ppm 2>&1; echo "exit=$?"
presentation-row correction: applied (row-0 wrap signature detected)
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=15.6693
ds.mean_abs_g=15.6673
ds.mean_abs_b=23.3460
ds.max_error=235.0000
ds.frac_within=0.0396
ds.frac_within_flat=0.9982
ds.frac_within_edge=0.0065
ds.flat_pixels=1640
ds.edge_pixels=47512
capture.mean_abs_r=15.6601
capture.mean_abs_g=15.7059
capture.mean_abs_b=23.4214
capture.max_error=239.0000
capture.frac_within=0.0379
capture.frac_within_flat=0.9985
capture.frac_within_edge=0.0044
capture.flat_pixels=2588
capture.edge_pixels=74212
ref_nonbg_pixels=1940
ref_bbox_left=83
ref_bbox_top=72
ref_bbox_right=157
ref_bbox_bottom=128
capture_nonbg_pixels=2479
capture_bbox_left=82
capture_bbox_top=62
capture_bbox_right=161
capture_bbox_bottom=130
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=1
class_white_capture_centroid_x=140.0000
class_white_capture_centroid_y=88.0000
class_red_ref_area=625
class_red_ref_centroid_x=105.3536
class_red_ref_centroid_y=114.1264
class_red_capture_area=634
class_red_capture_centroid_x=105.2145
class_red_capture_centroid_y=114.0268
class_green_ref_area=412
class_green_ref_centroid_x=144.3374
class_green_ref_centroid_y=101.2985
class_green_capture_area=443
class_green_capture_centroid_x=144.5192
class_green_capture_centroid_y=101.1038
class_blue_ref_area=903
class_blue_ref_centroid_x=102.2769
class_blue_ref_centroid_y=86.4463
class_blue_capture_area=962
class_blue_capture_centroid_x=102.3576
class_blue_capture_centroid_y=86.3919
class_yellow_ref_area=0
class_yellow_ref_centroid_x=0.0000
class_yellow_ref_centroid_y=0.0000
class_yellow_capture_area=0
class_yellow_capture_centroid_x=0.0000
class_yellow_capture_centroid_y=0.0000
bbox_dev_left=1
bbox_dev_top=10
bbox_dev_right=4
bbox_dev_bottom=2
verdict=FAIL
reason=bbox-top deviates 10 DS px (tol 3); bbox-right deviates 4 DS px (tol 3)
exit=1
```

Extra run: this comparison was run twice in total (repair round 2 with
`| tail -n 25` and repair round 3 without); the quoted block is the latest run.
The round-2 NEW(a) block showed the same `ds.mean_abs` values.


FACT — OLD(b) vs blend hardware fixture
(build HEAD `eda28c51eb6beb186130f2d68d6dd6f7c07b8711`):

```
$ python3 tools/eq4_hw_compare.py \
    --capture tools/eq4w_blend_hw/hw_s4bl_a.png \
    --reference /tmp/opencode/eq4x-verify/s4bl-old_199.ppm 2>&1; echo "exit=$?"
presentation-row correction: applied (row-0 wrap signature detected)
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=15.9668
ds.mean_abs_g=15.8522
ds.mean_abs_b=23.6745
ds.max_error=211.0000
ds.frac_within=0.0225
ds.frac_within_flat=1.0000
ds.frac_within_edge=0.0120
ds.flat_pixels=523
ds.edge_pixels=48629
capture.mean_abs_r=15.9577
capture.mean_abs_g=15.9079
capture.mean_abs_b=23.7362
capture.max_error=239.0000
capture.frac_within=0.0226
capture.frac_within_flat=1.0000
capture.frac_within_edge=0.0120
capture.flat_pixels=821
capture.edge_pixels=75979
ref_nonbg_pixels=1940
ref_bbox_left=83
ref_bbox_top=72
ref_bbox_right=157
ref_bbox_bottom=128
capture_nonbg_pixels=2313
capture_bbox_left=83
capture_bbox_top=62
capture_bbox_right=161
capture_bbox_bottom=130
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=1
class_white_capture_centroid_x=140.0000
class_white_capture_centroid_y=88.0000
class_red_ref_area=625
class_red_ref_centroid_x=105.3536
class_red_ref_centroid_y=114.1264
class_red_capture_area=634
class_red_capture_centroid_x=105.2145
class_red_capture_centroid_y=114.0268
class_green_ref_area=0
class_green_ref_centroid_x=0.0000
class_green_ref_centroid_y=0.0000
class_green_capture_area=0
class_green_capture_centroid_x=0.0000
class_green_capture_centroid_y=0.0000
class_blue_ref_area=0
class_blue_ref_centroid_x=0.0000
class_blue_ref_centroid_y=0.0000
class_blue_capture_area=0
class_blue_capture_centroid_x=0.0000
class_blue_capture_centroid_y=0.0000
class_yellow_ref_area=0
class_yellow_ref_centroid_x=0.0000
class_yellow_ref_centroid_y=0.0000
class_yellow_capture_area=0
class_yellow_capture_centroid_x=0.0000
class_yellow_capture_centroid_y=0.0000
bbox_dev_left=0
bbox_dev_top=10
bbox_dev_right=4
bbox_dev_bottom=2
verdict=FAIL
reason=bbox-top deviates 10 DS px (tol 3); bbox-right deviates 4 DS px (tol 3)
exit=1
```

Extra run: this comparison was run twice in total (repair round 2 with
`| tail -n 25` and repair round 3 without); the quoted block is the latest run.
The round-2 OLD(b) block showed the same `ds.mean_abs` values.


FACT — NEW(b) vs blend hardware fixture, post-fix
(build HEAD `a0eea169fe6fae37ee8f4fc41e2bcc558495ff04`):

```
$ python3 tools/eq4_hw_compare.py \
    --capture tools/eq4w_blend_hw/hw_s4bl_a.png \
    --reference /tmp/opencode/eq4x-verify/s4bl-new_199.ppm 2>&1; echo "exit=$?"
presentation-row correction: applied (row-0 wrap signature detected)
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=15.9668
ds.mean_abs_g=15.8522
ds.mean_abs_b=23.6745
ds.max_error=211.0000
ds.frac_within=0.0225
ds.frac_within_flat=1.0000
ds.frac_within_edge=0.0120
ds.flat_pixels=523
ds.edge_pixels=48629
capture.mean_abs_r=15.9577
capture.mean_abs_g=15.9079
capture.mean_abs_b=23.7362
capture.max_error=239.0000
capture.frac_within=0.0226
capture.frac_within_flat=1.0000
capture.frac_within_edge=0.0120
capture.flat_pixels=821
capture.edge_pixels=75979
ref_nonbg_pixels=1940
ref_bbox_left=83
ref_bbox_top=72
ref_bbox_right=157
ref_bbox_bottom=128
capture_nonbg_pixels=2313
capture_bbox_left=83
capture_bbox_top=62
capture_bbox_right=161
capture_bbox_bottom=130
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=1
class_white_capture_centroid_x=140.0000
class_white_capture_centroid_y=88.0000
class_red_ref_area=625
class_red_ref_centroid_x=105.3536
class_red_ref_centroid_y=114.1264
class_red_capture_area=634
class_red_capture_centroid_x=105.2145
class_red_capture_centroid_y=114.0268
class_green_ref_area=0
class_green_ref_centroid_x=0.0000
class_green_ref_centroid_y=0.0000
class_green_capture_area=0
class_green_capture_centroid_x=0.0000
class_green_capture_centroid_y=0.0000
class_blue_ref_area=0
class_blue_ref_centroid_x=0.0000
class_blue_ref_centroid_y=0.0000
class_blue_capture_area=0
class_blue_capture_centroid_x=0.0000
class_blue_capture_centroid_y=0.0000
class_yellow_ref_area=0
class_yellow_ref_centroid_x=0.0000
class_yellow_ref_centroid_y=0.0000
class_yellow_capture_area=0
class_yellow_capture_centroid_x=0.0000
class_yellow_capture_centroid_y=0.0000
bbox_dev_left=0
bbox_dev_top=10
bbox_dev_right=4
bbox_dev_bottom=2
verdict=FAIL
reason=bbox-top deviates 10 DS px (tol 3); bbox-right deviates 4 DS px (tol 3)
exit=1
```

Extra run: this comparison was run twice in total (repair round 2 with
`| tail -n 25` and repair round 3 without); the quoted block is the latest run.
The round-2 NEW(b) block showed the same `ds.mean_abs` values.


The `eq4_hw_compare.py` verdict is **FAIL** for both OLD and NEW renders against
the same fixtures, with the same `bbox-top` 10 DS px / `bbox-right` 4 DS px
deviation. The failure therefore predates the EQ-4x change and is not evidence
that the gate wiring introduced the offset. The acceptance signal for (a) is the
quad colour change measured in §5.5 (alpha-16/4 quads now `blended=no` and
alpha-0 quad invisible); for (b) it is the byte-identical NEW/OLD top-cropped
render and the unchanged dimmed quad colours.

## 6. Scene 5 regression

FACT — RSTA premise already shown in §5.2: scene 5 writes `0x04000060 = 0x11`
(bit 3 clear).

FACT — render and byte identity (post-fix build HEAD
`a0eea169fe6fae37ee8f4fc41e2bcc558495ff04`; OLD 3DSX at
`eda28c51eb6beb186130f2d68d6dd6f7c07b8711`, NEW 3DSX at
`a0eea169fe6fae37ee8f4fc41e2bcc558495ff04`):

```
$ tools/azahar_host/build/azahar_host \
    --core <redacted>/azahar_libretro.so \
    --game /tmp/opencode/eq4x-verify/old.3dsx \
    --frames 200 --press A:60-63 --shot 199 \
    --out /tmp/opencode/eq4x-verify/s5-old \
    --sd-root /tmp/opencode/eq4x-verify/sd-s5 2>&1 | tail -2
[azahar_host] wrote /tmp/opencode/eq4x-verify/s5-old_199.ppm (400x480)
[azahar_host] wrote /tmp/opencode/eq4x-verify/s5-old_last.ppm (400x480)

$ tools/azahar_host/build/azahar_host \
    --core <redacted>/azahar_libretro.so \
    --game /tmp/opencode/eq4x-verify/new.3dsx \
    --frames 200 --press A:60-63 --shot 199 \
    --out /tmp/opencode/eq4x-verify/s5-new \
    --sd-root /tmp/opencode/eq4x-verify/sd-s5 2>&1 | tail -2
[azahar_host] wrote /tmp/opencode/eq4x-verify/s5-new_199.ppm (400x480)
[azahar_host] wrote /tmp/opencode/eq4x-verify/s5-new_last.ppm (400x480)

$ # Full 400x480 PPMs differ only in the bottom-screen build-ID line (different HEADs).
$ # Compare the top 400x240 render area:
$ python3 - <<'EOF'
from PIL import Image
for n in ('s5-old', 's5-new'):
    ppm = f'/tmp/opencode/eq4x-verify/{n}_199.ppm'
    out = f'/tmp/opencode/eq4x-verify/{n}_top400x240.ppm'
    with open(ppm, 'rb') as f:
        header = b''
        while b'\n' not in header or header.count(b'\n') < 3:
            header += f.read(1)
        parts = header.split()
        w, h = int(parts[1]), int(parts[2])
        data = f.read()
    img = Image.frombytes('RGB', (w, h), data)
    top = img.crop((0, 0, 400, 240))
    top.save(out, 'PPM')
    print(f'{out} {top.size}')
EOF
/tmp/opencode/eq4x-verify/s5-old_top400x240.ppm (400, 240)
/tmp/opencode/eq4x-verify/s5-new_top400x240.ppm (400, 240)
$ cmp /tmp/opencode/eq4x-verify/s5-old_top400x240.ppm /tmp/opencode/eq4x-verify/s5-new_top400x240.ppm
$ echo $?
0
$ sha256sum /tmp/opencode/eq4x-verify/s5-old_top400x240.ppm /tmp/opencode/eq4x-verify/s5-new_top400x240.ppm
959f4bd21176758c6ac60e994319e731942c5b2b2931a212df7e35efe1a91e2e  /tmp/opencode/eq4x-verify/s5-old_top400x240.ppm
959f4bd21176758c6ac60e994319e731942c5b2b2931a212df7e35efe1a91e2e  /tmp/opencode/eq4x-verify/s5-new_top400x240.ppm
```

ACCEPTANCE: scene-5 OLD vs NEW top-cropped render is byte-identical, as expected
because every polygon is `POLY_ALPHA(31)` and the gate sees bit 3 clear. The
alpha-test override for the opaque path therefore does not change scene-5 output.

## 7. Console PLAN (not run)

Follow the established EQ-4n/EQ-4o procedure:

1. Copy the captured `twlhd_frame_v2.bin` from (a) to the SD card as
   `sdmc:/twlhd_frame.bin`.
2. Launch the NEW `arm11/twlhd-arm11.3dsx` on the console.
3. Press A to load the frame; wait for the render.
4. Capture the top screen with the cc3dsfs board.
5. Repeat for frame (b).
6. Crop to 400×240 and run `tools/eq4w_blend_hw/measure_blend_hw.py` and
   `tools/eq4_hw_compare.py` against
   `tools/fixtures/eq4_hw_scenes/s4_a_top400x240.png` (non-blend) and
   `tools/eq4w_blend_hw/hw_s4bl_a.png` (blend).

Expected numbers (measured post-fix on Azahar, one host run; build HEAD
`a0eea169fe6fae37ee8f4fc41e2bcc558495ff04`):
- (a) NEW render: alpha-16 green `(0,255,0)`, alpha-4 blue `(0,0,255)`,
  `blended=no`; alpha-0 quad invisible (`interior_bright_pixels=0`);
- (b) NEW render: dimmed green/blue `(7,142,11)` and `(13,13,60)`, top-cropped
  PPM byte-identical to OLD(b). This is **not** identical to the hardware
  fixture `hw_s4bl_a.png`: EQ-4w measured hardware as green 134/blue 33, while
  our blended render measures green 142/blue 60. Only the measured gap is
  reported here; this task does not change the blend math.
- `eq4_hw_compare.py` for (a) FAILs because `bbox-top` deviates 10 DS pixels
  (`bbox-right` 4 DS pixels); the quad colours are the acceptance signal. The
  same FAIL is observed for OLD(a) and OLD(b), so the offset predates the change.

## 8. UNVERIFIED items

- No hardware run of the new 3DSX.
- Programs that toggle `DISP3DCNT` bit 3 mid-frame.
- The alpha-test override keeps alpha-0 fragments invisible on the opaque path
  in Azahar; real-console verification is pending.
- The hardware basis is one console, one capture session (EQ-4r/EQ-4w).

## 9. Size audit

```
$ git add -N . && git diff main | wc -c
78490
```

## 10. Append-only pointers

- `docs/EQUALITY_CHECK_DESIGN.md`: see the EQ-4x update.
- `docs/EQ_SCENES.md`: see the EQ-4x update.
- `docs/reports/eq4w-blend-hw-result.md` §8: implementation now in
  `arm11/include/twlhd_blend_gate.h`, `arm11/source/main.c`,
  `arm11/source/renderer.c`; report: `docs/reports/eq4x-blend-gate.md`.
- `docs/reports/eq4u-scene4-alpha-why.md` §6.1: option 1 implemented.
