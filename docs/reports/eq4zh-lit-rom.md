<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4zh — `LIT=1/2` arm9v2 build switch and host-side lighting oracle proof

**Scope:** implement the `LIT=1/2` build switch for the arm9v2 demo ROM, verify
that existing builds stay byte-identical, prove that the ROM emits the expected
v2 GX lighting stream on the host, render it with Azahar/PICA, and compare with
`tools/hw/compare_ppm.py`.  No console was contacted for this report.

**Placeholders.**  `<worktree>` is the repository worktree, `<staging>` holds
ROMs/captures/build logs, and `<logfile>` is the redirected output file.  No
home paths appear.

**External tools.**  `DESMUME_CLI` points to the external DeSmuME CLI (see
`tools/emulator/desmume_capture.sh` and
`docs/reports/eq4r-static-scenes-hw-compare.md` §4.1).  `AZAHAR_LIBRETRO`
points to the external Azahar libretro core (see `tools/azahar_host/README.md`
§Run).  `AZAHAR_HOST` points to the built `azahar_host` binary (see
`tools/azahar_host/README.md` §Run).  `DESMUME_DBG_CLI` points to the scratch DeSmuME CLI with the autopatch switch described in `docs/reports/bkpt-diagnosis.md` section 1.

**Labels.**  **FACT** = exact command + raw output.  **SOURCE-READ** = source
or document citation.  **HYPOTHESIS** / **UNVERIFIED** = not measured.  **PLAN**
= not run.  **EXCERPT** = selected log lines with omitted count.

**Run counts (final source tree, counted over this tree only).**  The §2.1 build
script ran three times (the third is quoted) and the §2.3 byte-identity script
ran once (quoted).  The §3 chain (capture, parser validation, predict, Azahar
render, crop, compare) ran five times with the DeSmuME CLI named in §3.2: runs 1-3
used the AUTODUMP_FRAME=240 twins and failed at capture (§3.2), run 4 used the
AUTODUMP_FRAME=242 twins and passed every step, run 5 is quoted in §3.3-§3.5.
The §1 dry-run ran twice (the second is quoted).  The §3.2 fault evidence ran three times (the third is quoted) and the §3.3
`sha256sum` blocks ran twice (the second is quoted).  Extra ROM builds for the frame scan:
AUTODUMP_FRAME=241 (lit1, lit2) once each, AUTODUMP_FRAME=242, 243 and 244
(lit1 only) once each; their outcomes are in §3.2 and their logs are not quoted.

## 1. What changed

**SOURCE-READ — `LIT` Makefile switch.**  `arm9v2/Makefile:183-194` accepts
`LIT` empty, `1` or `2`; otherwise it stops with `LIT must be empty, 1, or 2`.
When set, it suffixes `-lit1`/`-lit2` to `TARGET`/`BUILD` and defines
`-DTWLHD_DEMO_LIT=<n>` in both `CFLAGS` and `CXXFLAGS`.

**SOURCE-READ — composition order.**  `arm9v2/Makefile:176-181` documents that
`LIT` runs after `AUTODUMP/VP/CUBES/SCENE/BLEND` and before
`STATIC_ANGLE/DIAGSCREEN`.

**FACT — line numbers of the `LIT` block in `arm9v2/Makefile` on the final tree.**

```
$ grep -n -E 'DS-mode lighting build|-litN-stN-diagN|LIT_STRIPPED|TWLHD_DEMO_LIT=\$\(LIT_STRIPPED\)' arm9v2/Makefile > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
160:# -autodump...-cN-sN-bl-litN-stN-diagN (e.g. twlhd-arm9v2-autodump-s4-bl-st30).
176:# Optional DS-mode lighting build: LIT=1 or LIT=2 draws the lit_a / lit_b quad
181:# -autodump...-cN-sN-bl-litN-stN-diagN.
183:LIT_STRIPPED	:=	$(strip $(LIT))
184:ifneq ($(LIT_STRIPPED),)
185:LIT_OK := $(shell echo '$(LIT_STRIPPED)' | grep -Eq '^[12]$$' && echo ok)
189:TARGET		:=	$(TARGET)-lit$(LIT_STRIPPED)
190:BUILD		:=	$(BUILD)-lit$(LIT_STRIPPED)
191:CFLAGS		+=	-DTWLHD_DEMO_LIT=$(LIT_STRIPPED)
193:CXXFLAGS	+=	-DTWLHD_DEMO_LIT=$(LIT_STRIPPED)
268:TWLHD_FLAGS_STAMP := AUTODUMP=$(AUTODUMP_FRAME_STRIPPED) $(VP_FLAGS) TWLHD_DEMO_CUBES=$(if $(CUBES_STRIPPED),$(CUBES_STRIPPED),1) TWLHD_DEMO_SCENE=$(if $(SCENE_STRIPPED),$(SCENE_STRIPPED),0) TWLHD_DEMO_BLEND=$(BLEND_STRIPPED) TWLHD_DEMO_LIT=$(LIT_STRIPPED) TWLHD_DEMO_STATIC_ANGLE=$(STATIC_ANGLE_STRIPPED) TWLHD_DEMO_DIAGSCREEN=$(DIAGSCREEN_STRIPPED)
```

**EXCERPT — dry-run composition (first 2 and last 2 lines of `<staging>/dry.log`; 73 of 77 lines omitted).**

```
$ make -C <worktree>/arm9v2 -n -B SCENE=4 LIT=1 STATIC_ANGLE=30 > <staging>/dry.log 2>&1; echo "exit=$?"; cat <staging>/dry.log
exit=0
make: Entering directory '<worktree>/arm9v2'
[ -d build-s4-lit1-st30 ] || mkdir -p build-s4-lit1-st30
...
echo built ... twlhd-arm9v2-s4-lit1-st30.nds
make: Leaving directory '<worktree>/arm9v2'
```

**FACT — the three composed defines appear in the dry-run log** (they read the `<staging>/dry.log` written by the command above):

```
$ grep -c -e -DTWLHD_DEMO_SCENE=4 <staging>/dry.log > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
34
```

```
$ grep -c -e -DTWLHD_DEMO_LIT=1 <staging>/dry.log > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
34
```

```
$ grep -c -e -DTWLHD_DEMO_STATIC_ANGLE_DEG=30 <staging>/dry.log > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
34
```


**SOURCE-READ — guarded lit draw path.**  `arm9v2/source/demo_frame.cpp:316-428`
contains the `TWLHD_DEMO_LIT` guarded path.  Lighting calls use the libnds API;
`glMaterialf()` is out-of-line in libnds, so it does not go through the
recording proxy, and the material and shininess words are written directly
through the proxy (justified in §2.4).

**FACT — STATIC_ANGLE is not used for lit scenes.**  The lit branch in
`twlhd_demo_draw_frame` returns before any rotation or STATIC_ANGLE code (the
`TWLHD_DEMO_STATIC_ANGLE_BIN` macro is used only on the non-LIT path below it).

```
$ grep -n -E 'TWLHD_DEMO_LIT|draw_lit_a\(\)|draw_lit_b\(\)|glFlush\(0\);|return;|TWLHD_DEMO_STATIC_ANGLE_BIN' arm9v2/source/demo_frame.cpp > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
17:#ifndef TWLHD_DEMO_LIT
18:#define TWLHD_DEMO_LIT 0
316:#if TWLHD_DEMO_LIT
432:#define TWLHD_DEMO_STATIC_ANGLE_BIN \
465:    glFlush(0);
466:    return;
468:#if TWLHD_DEMO_LIT
482:#if TWLHD_DEMO_LIT == 1
483:    draw_lit_a();
485:    draw_lit_b();
488:    glFlush(0);
489:    return;
517:    glRotateXi(TWLHD_DEMO_STATIC_ANGLE_BIN);
518:    glRotateYi(TWLHD_DEMO_STATIC_ANGLE_BIN);
550:    glFlush(0); /* records SWAP_BUFFERS (0x50) and disarms */
573:    glRotateXi(TWLHD_DEMO_STATIC_ANGLE_BIN);
574:    glRotateYi(TWLHD_DEMO_STATIC_ANGLE_BIN);
593:    glFlush(0); /* records SWAP_BUFFERS (0x50) and disarms */
632:        glRotateXi(TWLHD_DEMO_STATIC_ANGLE_BIN);
633:        glRotateYi(TWLHD_DEMO_STATIC_ANGLE_BIN);
644:    glFlush(0); /* records SWAP_BUFFERS (0x50) and disarms */
```


**SOURCE-READ — tests and wiring.**  `tools/test_arm9v2_lit.py` dry-runs the
Makefile for valid names, composition, invalid values and guarded source sites.
`Makefile:633-634` adds `test-arm9v2-lit`, `Makefile:735` adds it to the
aggregate `test` dependencies, and `Makefile:742` sets
`ARM9V2_LIT_DISCOVER_SKIP=1` for the discover pass.

## 2. Build evidence

### 2.1 Six ROM builds

**FACT — `<staging>/build_lit_roms.sh`.**  The script builds six variants from
`<worktree>/arm9v2` (plain and AUTODUMP_FRAME=240 and 242 twins for `LIT=1`
and `LIT=2`) and copies each ROM to `<staging>` immediately after its build.

```
$ cat <staging>/build_lit_roms.sh > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
#!/usr/bin/env bash
set -euo pipefail
WORKTREE=${WORKTREE:-<worktree>}
STAGING=${STAGING:-<staging>}
mkdir -p "$STAGING"

build_copy() {
  local lit=$1 ad=$2 name=$3 out=$4
  make -C "$WORKTREE/arm9v2" clean LIT="$lit" AUTODUMP_FRAME="$ad" >/dev/null 2>&1
  make -C "$WORKTREE/arm9v2" LIT="$lit" AUTODUMP_FRAME="$ad" > "$STAGING/build_lit_${name}.log" 2>&1
  echo "make $name exit=$?"
  local ts
  if [ "$ad" != "0" ]; then
    ts="autodump-lit${lit}"
  else
    ts="lit${lit}"
  fi
  cp "$WORKTREE/arm9v2/twlhd-arm9v2-${ts}.nds" "$STAGING/$out"
}

build_copy 1 0 lit1 twlhd-arm9v2-lit1.nds
build_copy 1 240 autodump_lit1 twlhd-arm9v2-autodump-lit1.nds
build_copy 1 242 autodump_lit1_af242 twlhd-arm9v2-autodump-lit1-af242.nds
build_copy 2 0 lit2 twlhd-arm9v2-lit2.nds
build_copy 2 240 autodump_lit2 twlhd-arm9v2-autodump-lit2.nds
build_copy 2 242 autodump_lit2_af242 twlhd-arm9v2-autodump-lit2-af242.nds
sha256sum "$STAGING"/twlhd-arm9v2-*.nds
```

**FACT — run output and sha256.**

```
$ WORKTREE=<worktree> STAGING=<staging> bash <staging>/build_lit_roms.sh > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
make lit1 exit=0
make autodump_lit1 exit=0
make autodump_lit1_af242 exit=0
make lit2 exit=0
make autodump_lit2 exit=0
make autodump_lit2_af242 exit=0
aa921069541c1b9825af01d372e0ecc69a2c22330a485ebc5eca8566f0b8c92f  <staging>/twlhd-arm9v2-autodump-lit1-af242.nds
5914eb47008ced3f39d829b5d8a10a3892dd128426f4595bb60d003977f704cc  <staging>/twlhd-arm9v2-autodump-lit1.nds
3514b85c57f75791819b82f02f39fb4cff952d498cdf121547e6ceb6a5c98550  <staging>/twlhd-arm9v2-autodump-lit2-af242.nds
260740fd93b8b30c43c93daa076b92bba9c079debef3366e6729035fec7fcbc9  <staging>/twlhd-arm9v2-autodump-lit2.nds
f9d9601ca8abbdc87c008161ef6121592359de411ec8ac2f8f315beb3b9d2adf  <staging>/twlhd-arm9v2-lit1.nds
e61a0a6a5d5d6a06280a8f06118794930a2d12cf9db080806b35a30a5a02c4d0  <staging>/twlhd-arm9v2-lit2.nds
```

Build logs are in `<staging>/build_lit_*.log`.

### 2.2 `make test` passes

**EXCERPT (last 10 lines of `<logfile>`; 746 of 756 lines omitted).**

```
$ make test > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
...
error: angle must be an integer
........ssssssssssssssssssssssssssssss...ssssssssssssssssssssssssssssssssssssssssssssssss...........................................................................................................................................................................................................
----------------------------------------------------------------------
Ran 449 tests in 93.710s

OK (skipped=124)
/tmp/tmpup6owjqu/twlhd_frame_v2_tex_over.bin
vertex_count=36
max_clip_deviation=0.007324
colour_exactness=PASS
```

### 2.3 Byte-identity of existing variants

**FACT — `<staging>/base` is a clean worktree of `main`.**

```
$ git -C <worktree> worktree add --detach <staging>/base main > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
Preparing worktree (detached HEAD 82cea9d)
HEAD is now at 82cea9d EQ-4zg: partial viewport comparison (hardware, DeSmuME, our render)
```

```
$ git -C <staging>/base rev-parse HEAD > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
82cea9d9dc8e8f6a1f7076c430438d6da2a236bb
```

```
$ git -C <staging>/base status --porcelain > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
```

```
$ git -C <worktree> rev-parse main > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
82cea9d9dc8e8f6a1f7076c430438d6da2a236bb
```

The `<staging>/base` HEAD equals the `main` HEAD and the porcelain output is
empty.

**FACT — `<staging>/build_byteid.sh`.**  The script builds each existing
variant once from `<staging>/base` and once from `<worktree>` with
`STATIC_ANGLE=30`, copies each ROM to `<staging>/byteid`, then prints sha256
and `cmp` results.

```
$ cat <staging>/build_byteid.sh > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
#!/usr/bin/env bash
set -euo pipefail
WORKTREE=${WORKTREE:-<worktree>}
BASE=${BASE:-<staging>/base}
STAGING=${STAGING:-<staging>}
OUT="$STAGING/byteid"
mkdir -p "$OUT"

VARIANTS=(
  "STATIC_ANGLE=30:st30:cube"
  "SCENE=1 STATIC_ANGLE=30:s1-st30:s1"
  "SCENE=2 STATIC_ANGLE=30:s2-st30:s2"
  "SCENE=3 STATIC_ANGLE=30:s3-st30:s3"
  "SCENE=4 STATIC_ANGLE=30:s4-st30:s4"
  "SCENE=5 STATIC_ANGLE=30:s5-st30:s5"
  "CUBES=2 STATIC_ANGLE=30:c2-st30:c2"
  "BLEND=1 SCENE=4 STATIC_ANGLE=30:s4-bl-st30:s4bl"
)

for spec in "${VARIANTS[@]}"; do
  IFS=':' read -r var file friendly <<< "$spec"
  make -C "$BASE/arm9v2" clean $var >/dev/null 2>&1
  make -C "$BASE/arm9v2" $var > "$STAGING/byteid_${friendly}.log" 2>&1
  cp "$BASE/arm9v2/twlhd-arm9v2-${file}.nds" "$OUT/base_${friendly}.nds"
  make -C "$WORKTREE/arm9v2" clean $var >/dev/null 2>&1
  make -C "$WORKTREE/arm9v2" $var > "$STAGING/byteid_branch_${friendly}.log" 2>&1
  cp "$WORKTREE/arm9v2/twlhd-arm9v2-${file}.nds" "$OUT/branch_${friendly}.nds"
done

{
  for name in cube s1 s2 s3 s4 s5 c2 s4bl; do
    sha256sum "$OUT/base_${name}.nds" "$OUT/branch_${name}.nds"
    cmp "$OUT/base_${name}.nds" "$OUT/branch_${name}.nds"; echo "cmp $name exit=$?"
  done
} > "$STAGING/byteid_cmp.log" 2>&1
cat "$STAGING/byteid_cmp.log"
```

**FACT — run output.**  Variants compared: cube, `SCENE=1..5`, `CUBES=2`, and
`BLEND=1 SCENE=4`, each at `STATIC_ANGLE=30`.  The builds ran on the final tree.

```
$ WORKTREE=<worktree> BASE=<staging>/base STAGING=<staging> bash <staging>/build_byteid.sh > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
6453ba6219d717a7de5a785c165574ad9cf15cc2f04d994c1ab34bc7cd335a01  <staging>/byteid/base_cube.nds
6453ba6219d717a7de5a785c165574ad9cf15cc2f04d994c1ab34bc7cd335a01  <staging>/byteid/branch_cube.nds
cmp cube exit=0
f9f0d5486c777f08fb80eebf8034e648ad5dbdfd9cb799bd8782208bf23ce801  <staging>/byteid/base_s1.nds
f9f0d5486c777f08fb80eebf8034e648ad5dbdfd9cb799bd8782208bf23ce801  <staging>/byteid/branch_s1.nds
cmp s1 exit=0
dd26ec63bdc20cac510000f515302c0606123dc7d4b0f49ea78c9b84c5413ced  <staging>/byteid/base_s2.nds
dd26ec63bdc20cac510000f515302c0606123dc7d4b0f49ea78c9b84c5413ced  <staging>/byteid/branch_s2.nds
cmp s2 exit=0
5c6e8c2c627e7add146e88fadc10a92d088a0f40efd74300bc6c29112d52ff93  <staging>/byteid/base_s3.nds
5c6e8c2c627e7add146e88fadc10a92d088a0f40efd74300bc6c29112d52ff93  <staging>/byteid/branch_s3.nds
cmp s3 exit=0
9662c06c7aeefa18807112c1d66b96003a567550a4a9bd18709b95ea75c80eba  <staging>/byteid/base_s4.nds
9662c06c7aeefa18807112c1d66b96003a567550a4a9bd18709b95ea75c80eba  <staging>/byteid/branch_s4.nds
cmp s4 exit=0
1b6cb5c10fe32e85f2c498d145240836476c40360a9a86f9a8dc9df44944a105  <staging>/byteid/base_s5.nds
1b6cb5c10fe32e85f2c498d145240836476c40360a9a86f9a8dc9df44944a105  <staging>/byteid/branch_s5.nds
cmp s5 exit=0
43e39eb87558fb1019699c33d8cd333ecf191ba747532bf163fabfc4b4ec2940  <staging>/byteid/base_c2.nds
43e39eb87558fb1019699c33d8cd333ecf191ba747532bf163fabfc4b4ec2940  <staging>/byteid/branch_c2.nds
cmp c2 exit=0
e0aa3778c91ae0a06ecc895a92f3b6cb41fd8775158af579b24ebe4b678e6d29  <staging>/byteid/base_s4bl.nds
e0aa3778c91ae0a06ecc895a92f3b6cb41fd8775158af579b24ebe4b678e6d29  <staging>/byteid/branch_s4bl.nds
cmp s4bl exit=0
```

### 2.4 Guarded lighting sites

**SOURCE-READ — libnds declarations.**  `glLight` packs the light index into
bits 30-31 of both `GFX_LIGHT_VECTOR` and `GFX_LIGHT_COLOR`
(`$DEVKITPRO/libnds/include/nds/arm9/videoGL.h:675-686`); `glMaterialf` is
declared out-of-line (`videoGL.h:523`) and therefore does not expand through
the recording proxy in `arm9v2/include/twlhd_gx_shim.hpp`.
`glMaterialShinyness` installs a fixed 0,2,4,...,254 ramp
(`videoGL.h:727-740`).  No libnds material API therefore goes through the
recording proxy; the material words and the custom 1,3,5,...,255 shininess ramp
are emitted directly.  `lit_material()` writes the material words directly through the recording
proxy; the captured stream contains only the direct `GFX_DIFFUSE_AMBIENT` /
`GFX_SPECULAR_EMISSION` writes.

**FACT — lighting-call grep.**  All matches are in `arm9v2/source/demo_frame.cpp`.

```
$ grep -rn -E 'glLight|glMaterialf|glNormal|glShininess|POLY_FORMAT_LIGHT' arm9v2/source > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
arm9v2/source/demo_frame.cpp:84:        glNormal(cube_normals[face]);
arm9v2/source/demo_frame.cpp:321: * glLight(), glNormal() and glPolyFmt() are libnds static-inline helpers, so
arm9v2/source/demo_frame.cpp:325: * glMaterialf() is only declared out-of-line in libnds (videoGL.h:523) and
arm9v2/source/demo_frame.cpp:355:/* Light vectors from gen_lit_frames.py are 20.12 fixed point; glLight takes
arm9v2/source/demo_frame.cpp:371:    glNormal(normal);
arm9v2/source/demo_frame.cpp:380:    /* glMaterialf() is out-of-line in libnds, so it does not go through the recording proxy. */
arm9v2/source/demo_frame.cpp:386:    glLight(0, RGB15(31, 31, 31),
arm9v2/source/demo_frame.cpp:390:    draw_lit_quad(31, POLY_FORMAT_LIGHT0, LIT_NORMAL_FACING, lit_quads[0]);
arm9v2/source/demo_frame.cpp:391:    draw_lit_quad(31, POLY_FORMAT_LIGHT0, LIT_NORMAL_60, lit_quads[1]);
arm9v2/source/demo_frame.cpp:392:    draw_lit_quad(31, POLY_FORMAT_LIGHT0, LIT_NORMAL_GRAZING, lit_quads[2]);
arm9v2/source/demo_frame.cpp:393:    draw_lit_quad(31, POLY_FORMAT_LIGHT0, LIT_NORMAL_AWAY, lit_quads[3]);
arm9v2/source/demo_frame.cpp:399:    glLight(0, RGB15(31, 0, 0),
arm9v2/source/demo_frame.cpp:401:    glLight(1, RGB15(0, 0, 31),
arm9v2/source/demo_frame.cpp:406:    draw_lit_quad(31, POLY_FORMAT_LIGHT0, LIT_NORMAL_FACING, lit_quads[0]);
arm9v2/source/demo_frame.cpp:410:    draw_lit_quad(31, POLY_FORMAT_LIGHT0, LIT_NORMAL_AWAY, lit_quads[1]);
arm9v2/source/demo_frame.cpp:414:    glLight(0, RGB15(31, 31, 31),
arm9v2/source/demo_frame.cpp:419:    draw_lit_quad(31, POLY_FORMAT_LIGHT0, LIT_NORMAL_FACING, lit_quads[2]);
arm9v2/source/demo_frame.cpp:423:    glLight(0, RGB15(31, 0, 0),
arm9v2/source/demo_frame.cpp:425:    draw_lit_quad(31, POLY_FORMAT_LIGHT0 | POLY_FORMAT_LIGHT1,
```

```
$ grep -rn -E 'GFX_LIGHT_VECTOR|GFX_LIGHT_COLOR|GFX_DIFFUSE_AMBIENT|GFX_SPECULAR_EMISSION|GFX_SHININESS' arm9v2/source > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
arm9v2/source/demo_frame.cpp:328: * recording proxy; both the material words (GFX_DIFFUSE_AMBIENT and
arm9v2/source/demo_frame.cpp:329: * GFX_SPECULAR_EMISSION) and the custom 1,3,5,...,255 shininess ramp are
arm9v2/source/demo_frame.cpp:381:    GFX_DIFFUSE_AMBIENT = (ambient << 16) | diffuse;
arm9v2/source/demo_frame.cpp:382:    GFX_SPECULAR_EMISSION = (emission << 16) | specular;
arm9v2/source/demo_frame.cpp:417:        GFX_SHININESS = LIT_SHININESS[i];
```

Classification: line 84 is the non-LIT cube's per-vertex `glNormal`; every
other match is inside `#if TWLHD_DEMO_LIT` (lines 317-425, including comments).
`tools/test_arm9v2_lit.py` pins the two `glNormal` allowlist sites and asserts
16 guarded lighting sites with zero violations.

## 3. Host-side lighting proof

### 3.1 Generate oracle frames

**FACT.**

```
$ python3 tools/hw/gen_lit_frames.py <staging>/frames > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
<staging>/frames/twlhd_frame_v2_lit_a.bin
<staging>/frames/twlhd_frame_v2_lit_b.bin
<staging>/frames/twlhd_frame_v2_lit_ctl.bin
```

### 3.2 DeSmuME fault on the AUTODUMP_FRAME=240 twins

**SOURCE-READ.**  A DeSmuME-only fault class that depends on the ROM layout and
on DeSmuME's in-place DLDI autopatch is already documented for other ROMs in
`docs/reports/bkpt-diagnosis.md` (section CAUSE, with the autopatch switch in
its section 1).  `DESMUME_DBG_CLI` below names the scratch DeSmuME CLI described in
that section 1.

**FACT — `<staging>/ev.sh` (every command of the fault evidence).**

```
$ cat <staging>/ev.sh > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
S=<staging>
W=<worktree>
D=$DESMUME_CLI
DBG=$DESMUME_DBG_CLI
OUT=$S/evidence.txt
: > $OUT
cd $W
# E1: af240 lit1 crash
rm -rf $S/cap-af240-lit1
DESMUME_CLI=$D tools/emulator/desmume_capture.sh $S/twlhd-arm9v2-autodump-lit1.nds $S/cap-af240-lit1 > $S/e1.log 2>&1; echo "E1 exit=$?" >> $OUT
wc -l < $S/e1.log >> $OUT
tail -n 4 $S/e1.log >> $OUT
grep -a -c armcpu_exception $S/cap-af240-lit1/desmume.log > $S/e1b.log 2>&1; echo "E1b exit=$?" >> $OUT; cat $S/e1b.log >> $OUT
grep -a -m 1 Undefined $S/cap-af240-lit1/desmume.log > $S/e1c.log 2>&1; echo "E1c exit=$?" >> $OUT; cat $S/e1c.log >> $OUT
rm -rf $S/cap-af240-lit1
# E2: autopatch discriminator
for nd in 0 1; do
  rm -rf $S/cap-nd$nd
  if [ $nd = 1 ]; then export TWLHD_NODLDI=1; else unset TWLHD_NODLDI; fi
  DESMUME_CLI=$DBG timeout 70 tools/emulator/desmume_capture.sh $S/twlhd-arm9v2-autodump-lit1.nds $S/cap-nd$nd > $S/e2.log 2>&1
  echo "E2 NODLDI=$nd: exceptions=$(grep -a -c armcpu_exception $S/cap-nd$nd/desmume.log) autopatch_lines=$(grep -a -c AUTO-PATCHING $S/cap-nd$nd/desmume.log)" >> $OUT
  rm -rf $S/cap-nd$nd
done
unset TWLHD_NODLDI
# E3: plain ROMs
for r in lit1 lit2; do
  rm -rf $S/cap-pl-$r
  DESMUME_CLI=$D timeout 60 tools/emulator/desmume_capture.sh $S/twlhd-arm9v2-$r.nds $S/cap-pl-$r > $S/e3.log 2>&1
  echo "E3 plain $r (60 s window): exceptions=$(grep -a -c armcpu_exception $S/cap-pl-$r/desmume.log)" >> $OUT
  rm -rf $S/cap-pl-$r
done
cat $OUT
```

**FACT — run output** (`ev.sh` prints its collected lines last; the capture
script's own trace is cut to its last 4 lines by `ev.sh`).

```
$ bash <staging>/ev.sh > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
E1 exit=1
51
error: twlhd_frame_v2.bin was not written to the FAT image
+ echo '       (did the demo reach the autodump frame?)'
       (did the demo reach the autodump frame?)
+ exit 1
E1b exit=0
48664323
E1c exit=0
ARM9: Undefined instruction: 0xE3003001 PC=0x01FF8628
E2 NODLDI=0: exceptions=14352916 autopatch_lines=1
E2 NODLDI=1: exceptions=0 autopatch_lines=0
E3 plain lit1 (60 s window): exceptions=0
E3 plain lit2 (60 s window): exceptions=0
```

Reading the output: (1) the AUTODUMP_FRAME=240 twin of `LIT=1` faults at boot
with `Undefined instruction ... PC=0x01FF8628`, an ITCM address, and writes no
frame file (E1); (2) the same ROM with DeSmuME's autopatch switched off shows no
exception, with it on shows the fault (E2); (3) the plain `LIT=1`/`LIT=2` ROMs,
which are the ROMs the owner runs on the console, show no exception within the
60-second window, but this window is shorter than a full capture (E3).
AUTODUMP_FRAME=241 gave a fault for `LIT=1` and a capture for `LIT=2`;
AUTODUMP_FRAME=242 gave a capture for both (logs not quoted).  
**HYPOTHESIS.**  The fault is the emulator-side autopatch class of
`bkpt-diagnosis.md`: it changes with ROM layout (a 48-byte layout change moves
it) and needs the autopatch.  A defect in the lit scene code is not excluded:
the mechanism is not found.  **UNVERIFIED (scratch run, not quoted):** a
DeSmuME build with write tracing showed one zero byte written into the ITCM
image after a control transfer into the middle of libnds `bgUpdate`.  **UNVERIFIED:** whether any ROM of this family
behaves the same on a real DS.  The AUTODUMP_FRAME=242 twins are used for the
capture below because they run to completion; the scene code is identical (the
frame number is the only difference), and the captured stream is compared with the
oracle in §3.4.

### 3.3 Capture of the AUTODUMP_FRAME=242 twins and parser validation

**FACT.**  The sha256 of each ROM right before its capture matches §2.1.

```
$ sha256sum <staging>/twlhd-arm9v2-autodump-lit1-af242.nds > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
aa921069541c1b9825af01d372e0ecc69a2c22330a485ebc5eca8566f0b8c92f  <staging>/twlhd-arm9v2-autodump-lit1-af242.nds
```

```
$ sha256sum <staging>/twlhd-arm9v2-autodump-lit2-af242.nds > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
3514b85c57f75791819b82f02f39fb4cff952d498cdf121547e6ceb6a5c98550  <staging>/twlhd-arm9v2-autodump-lit2-af242.nds
```

**EXCERPT — DeSmuME capture of the `LIT=1` twin (first 2 and last 3 lines of `<logfile>`; 44 of 49 lines omitted).**

```
$ DESMUME_CLI="$DESMUME_CLI" tools/emulator/desmume_capture.sh <staging>/twlhd-arm9v2-autodump-lit1-af242.nds <staging>/cap-lit1 > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
+ mkdir -p <staging>/cap-lit1
+ image=<staging>/cap-lit1/cflash.img
...
+ cp <staging>/cap-lit1/fat/twlhd_frame_v2.bin <staging>/cap-lit1/twlhd_frame_v2.bin
+ echo 'capture: <staging>/cap-lit1/twlhd_frame_v2.bin'
capture: <staging>/cap-lit1/twlhd_frame_v2.bin
```

**EXCERPT — DeSmuME capture of the `LIT=2` twin (first 2 and last 3 lines of `<logfile>`; 44 of 49 lines omitted).**

```
$ DESMUME_CLI="$DESMUME_CLI" tools/emulator/desmume_capture.sh <staging>/twlhd-arm9v2-autodump-lit2-af242.nds <staging>/cap-lit2 > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
+ mkdir -p <staging>/cap-lit2
+ image=<staging>/cap-lit2/cflash.img
...
+ cp <staging>/cap-lit2/fat/twlhd_frame_v2.bin <staging>/cap-lit2/twlhd_frame_v2.bin
+ echo 'capture: <staging>/cap-lit2/twlhd_frame_v2.bin'
capture: <staging>/cap-lit2/twlhd_frame_v2.bin
```

**EXCERPT — parser validation (first 1 and last 1 lines of `<logfile>`; 4 of 6 lines omitted).**

```
$ tools/emulator/validate_with_parsers.sh <staging>/cap-lit1/twlhd_frame_v2.bin > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
+ gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I<worktree>/common <worktree>/tests/host/v2_dump_cli.c <worktree>/common/twlhd_v2_parse.c -o /tmp/tmp.MPFpW5s5hm/v2_dump_cli
...
parsers: OK (entries=58 rsta=4)
```

**EXCERPT — parser validation (first 1 and last 1 lines of `<logfile>`; 4 of 6 lines omitted).**

```
$ tools/emulator/validate_with_parsers.sh <staging>/cap-lit2/twlhd_frame_v2.bin > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
+ gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I<worktree>/common <worktree>/tests/host/v2_dump_cli.c <worktree>/common/twlhd_v2_parse.c -o /tmp/tmp.nEcZiaB2nS/v2_dump_cli
...
parsers: OK (entries=102 rsta=4)
```

### 3.4 Interpreter prediction agrees with the oracle

**FACT.**  Per-quad RGBA from `predict_lit.py` for the generated frames and the
captured frames.

```
$ python3 tools/hw/predict_lit.py <staging>/frames/twlhd_frame_v2_lit_a.bin --out <staging>/lit_a.json > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
```
```
$ python3 tools/hw/predict_lit.py <staging>/frames/twlhd_frame_v2_lit_b.bin --out <staging>/lit_b.json > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
```
```
$ python3 tools/hw/predict_lit.py <staging>/cap-lit1/twlhd_frame_v2.bin --out <staging>/cap-lit1.json > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
```
```
$ python3 tools/hw/predict_lit.py <staging>/cap-lit2/twlhd_frame_v2.bin --out <staging>/cap-lit2.json > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
```
```
$ { for src in lit_a cap-lit1 lit_b cap-lit2; do echo "=== $src ==="; jq -r '.quads[] | "\(.index) \(.rgba8[0]) \(.rgba8[1]) \(.rgba8[2]) \(.rgba8[3])"' <staging>/${src}.json; done; } > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
=== lit_a ===
0 197 197 197 255
1 107 107 107 255
2 25 25 25 255
3 25 25 25 255
=== cap-lit1 ===
0 197 197 197 255
1 107 107 107 255
2 25 25 25 255
3 25 25 25 255
=== lit_b ===
0 197 0 0 255
1 33 230 33 255
2 239 239 239 255
3 173 0 148 255
=== cap-lit2 ===
0 197 0 0 255
1 33 230 33 255
2 239 239 239 255
3 173 0 148 255
```

The captured stream predicts exactly the same per-quad colours (all four
channels) as the generated oracle frames.  Material words are written only
through the proxy (§2.4).  The predictor identifies quads by their centre pixel;
the ROM and the generated frame use the same 2×2 grid.

### 3.5 Azahar/PICA render matches the prediction

**FACT — stage the captured frames for `azahar_host`.**  The copy follows
`tools/azahar_host/smoke.sh` and the SD mapping in `tools/azahar_host/README.md`
(section "Isolated environment and the SD mapping").

```
$ { mkdir -p <staging>/sd-lit1 <staging>/sd-lit2 && cp <staging>/cap-lit1/twlhd_frame_v2.bin <staging>/sd-lit1/twlhd_frame.bin && cp <staging>/cap-lit2/twlhd_frame_v2.bin <staging>/sd-lit2/twlhd_frame.bin; } > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
```

**EXCERPT — `azahar_host` render of the `LIT=1` capture (lines containing `wrote`; 462 of 464 lines omitted).**  `azahar_host` and the libretro core are
the tools of `tools/azahar_host/README.md` section "Run".

```
$ azahar_host --game <worktree>/arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 199 --out <staging>/azahar-lit1 --sd-root <staging>/sd-lit1 > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
[azahar_host] wrote <staging>/azahar-lit1_199.ppm (400x480)
[azahar_host] wrote <staging>/azahar-lit1_last.ppm (400x480)
```

**EXCERPT — `azahar_host` render of the `LIT=2` capture (lines containing `wrote`; 462 of 464 lines omitted).**  `azahar_host` and the libretro core are
the tools of `tools/azahar_host/README.md` section "Run".

```
$ azahar_host --game <worktree>/arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 199 --out <staging>/azahar-lit2 --sd-root <staging>/sd-lit2 > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
[azahar_host] wrote <staging>/azahar-lit2_199.ppm (400x480)
[azahar_host] wrote <staging>/azahar-lit2_last.ppm (400x480)
```

**SOURCE-READ — crop with presentation-row correction `on`.**  `azahar_host`
writes a 400×480 stacked image.  `<staging>/crop_top_on.py` (full text below)
imports `tools/azahar_presentation.py`, applies correction `on`, and crops the
top 240 rows.  The predictor JSON encodes true DS top-screen geometry
(`tools/hw/README.md`, section "Derived pixel mapping"), so the Azahar render
needs the documented correction; `docs/reports/eq4zc-alpha-sweep.md` §3
applies `--azahar-correction on` for the same combination and `off` would not
match it.

**FACT — `<staging>/crop_top_on.py`.**

```
$ cat <staging>/crop_top_on.py > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
#!/usr/bin/env python3
import sys
sys.path.insert(0, __import__('os').environ['PYTHONPATH'].split(':')[0])
from azahar_presentation import apply_correction

in_path = sys.argv[1]
out_path = sys.argv[2]

with open(in_path, 'rb') as f:
    data = f.read()
header_end = data.index(b'\n255\n') + 5
w, h = None, None
for line in data[:header_end].split(b'\n'):
    if line.startswith(b'P6'):
        continue
    parts = line.split()
    if len(parts) == 2:
        w, h = int(parts[0]), int(parts[1])
        break
assert w is not None and h is not None
pixels = data[header_end:]
pixels_list = [(pixels[i], pixels[i+1], pixels[i+2]) for i in range(0, len(pixels), 3)]
corrected, applied, reason = apply_correction('on', pixels_list, w, h)
print('presentation-row correction: %s (%s)' % ('applied' if applied else 'not applied', reason))
# Crop top 240 rows.
top = corrected[:w * 240]
out = b'P6\n400 240\n255\n' + bytes(p for rgb in top for p in rgb)
open(out_path, 'wb').write(out)
```

**FACT — crop with correction `on`.**

```
$ PYTHONPATH=<worktree>/tools python3 <staging>/crop_top_on.py <staging>/azahar-lit1_199.ppm <staging>/azahar-lit1_top.ppm > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
presentation-row correction: applied (--azahar-correction on (forced))
```
```
$ PYTHONPATH=<worktree>/tools python3 <staging>/crop_top_on.py <staging>/azahar-lit2_199.ppm <staging>/azahar-lit2_top.ppm > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
presentation-row correction: applied (--azahar-correction on (forced))
```

**FACT — compare against the predictor JSON at tolerance 12.**

```
$ python3 tools/hw/compare_ppm.py <staging>/azahar-lit1_top.ppm <staging>/cap-lit1.json --tol 12 > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
patch       measured        expected        dev R/G/B  
quad 0      (197,197,197) (197,197,197) 0/0/0
quad 1      (107,107,107) (107,107,107) 0/0/0
quad 2      (25 ,25 ,25 ) (25 ,25 ,25 ) 0/0/0
quad 3      (25 ,25 ,25 ) (25 ,25 ,25 ) 0/0/0
background  (0  ,0  ,0  ) (0  ,0  ,0  ) 0/0/0
tolerance: 12 level(s) per channel
result=PASS
```
```
$ python3 tools/hw/compare_ppm.py <staging>/azahar-lit2_top.ppm <staging>/cap-lit2.json --tol 12 > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
patch       measured        expected        dev R/G/B  
quad 0      (197,0  ,0  ) (197,0  ,0  ) 0/0/0
quad 1      (33 ,230,33 ) (33 ,230,33 ) 0/0/0
quad 2      (239,239,239) (239,239,239) 0/0/0
quad 3      (173,0  ,148) (173,0  ,148) 0/0/0
background  (0  ,0  ,0  ) (0  ,0  ,0  ) 0/0/0
tolerance: 12 level(s) per channel
result=PASS
```

Both lit scenes pass `compare_ppm.py` at tolerance 12.

## 4. HYPOTHESIS / UNVERIFIED
* **SOURCE-READ — lit POLYGON_ATTR sets cull bits 6-7 = 3 (no culling).**
  `arm9v2/source/demo_frame.cpp:369` is
  `glPolyFmt(POLY_ALPHA(alpha) | POLY_CULL_NONE | light_mask)`.  This matches
  the cull mode used by every other arm9v2 scene
  (`demo_frame.cpp:302,449,532,534,584,588,639`) and follows
  `docs/DS_GX_RESEARCH.md:38-41`, which records bits 6-7 as
  `0=Cull front and back, 1=Cull front, 2=Cull back, 3=No culling`.

* **SOURCE-READ — POLYGON_ATTR differs from `gen_lit_frames.py` only in the cull bits.**
  The generated oracle frames write `(alpha<<16)|(mask&0xF)`
  (`tools/hw/gen_lit_frames.py:105`), leaving cull bits 0.  The ROM writes the
  same alpha, light-mask and polygon-ID fields, plus `POLY_CULL_NONE`
  (`0xC0` in bits 6-7), so the captured POLYGON_ATTR commands differ from the
  oracle only in those two bits.  The cull bits do not affect the predictor
  (`tools/hw/predict_lit.py` does not decode POLYGON_ATTR), and the arm11/PICA
  path hardcodes `GPU_CULL_NONE` (`arm11/source/renderer.c:897,1552`), so host
  prediction and Azahar rendering are unchanged.

* **UNVERIFIED — DS hardware lighting.**  Host interpreter, predictor and Azahar
  render agree, but no real DS/3DS-in-DS-mode capture was made.  The lighting
  equations are DeSmuME-derived (`docs/DS_GX_RESEARCH.md:222-258`) and were
  previously measured on PICA200 only (Run 18, `docs/PHASE5B_PLAN.md:98-100`).

* **UNVERIFIED — shininess-table and two-light interaction on DS.**  `lit_b`
  exercises specular through the shininess table and two simultaneous lights.

* **HYPOTHESIS — capture pass criterion.**  A real DS capture of the plain LIT
  ROMs will pass `tools/hw/compare_ppm.py` against the predictor JSON at
  tolerance 12, assuming DS hardware matches the DeSmuME-derived lighting model.


* **HYPOTHESIS / UNVERIFIED — DeSmuME fault of the AUTODUMP_FRAME=240 twins
  (§3.2).**  Measured: the `LIT=1` twin faults at boot under DeSmuME with its
  DLDI autopatch and not without it; the plain ROMs did not fault within 60 s.
  The mechanism is not found.  A defect of the lit scene code is not excluded.
  Whether a real DS shows any fault is UNVERIFIED (the owner run in §5 answers
  it for the plain ROMs).  The stream evidence of §3.3-§3.5 comes from the
  AUTODUMP_FRAME=242 twins, not from the AUTODUMP_FRAME=240 twins.

## 5. Owner console PLAN
Goal: measure DS-mode lighting for the two plain ROMs and compare with the host
oracle.  **PLAN — not run.**
1. **Upload the plain ROMs** (not the autodump twins) to the SD card used by
   TWiLight Menu++:
   * `<staging>/twlhd-arm9v2-lit1.nds` `sha256=f9d9601ca8abbdc87c008161ef6121592359de411ec8ac2f8f315beb3b9d2adf`
   * `<staging>/twlhd-arm9v2-lit2.nds` `sha256=e61a0a6a5d5d6a06280a8f06118794930a2d12cf9db080806b35a30a5a02c4d0`
2. **Launch each ROM from TWiLight Menu++ on the New 3DS in DS mode.**  Boot
   TWiLight Menu++, select the ROM, and launch it so the console runs in DS mode.
3. **Capture the top screen** with the cc3dsfs board.  Use the flags from
   `docs/reports/eq4c-ds-scale-measurement.md` §6.5:
   `cc3dsfs --auto_connect --no_audio --no_auto_save --enabled_both 0 --enabled_top 1 --scaling_top 1`
4. **Decode the capture with the corrected `xwd2png.py`** and crop to the top
   screen.  The corrected geometry is in `docs/reports/eq4c-ds-scale-measurement.md` §8:
   > DS pixel (x, y) appears at capture (40 + 1.25x, 1.25y); quads white
   > (255,255,255), explicit grey (132,134,132), red (255,0,0), black (0,0,0),
   > background (66,69,66) ±3.
   This matches the predictor's pixel mapping in `tools/hw/README.md`
   §Derived pixel mapping (`sx = 200 + 160*x`, `sy = 119 - 120*y` for default
   viewport, i.e. the DS 256×192 picture shown as 320×240 centred).
5. **Compare with the predictor JSON** generated in this report:
   `$ python3 tools/hw/compare_ppm.py <staging>/capture-lit1.ppm <staging>/cap-lit1.json --tol 12`
   `$ python3 tools/hw/compare_ppm.py <staging>/capture-lit2.ppm <staging>/cap-lit2.json --tol 12`
6. **Expected verdict (HYPOTHESIS):** `result=PASS` for both captures; a black
   screen would mean the §3.2 fault also occurs on hardware.
7. **If the verdict fails:** save the capture PPM and compare output; check
   whether the failure is global or local to isolate the diverging lighting path.
## 6. Requirement traceability

| Requirement | Where |
|---|---|
| `LIT` accepted values, suffix, define, composition order | §1 (`arm9v2/Makefile:183-194`), §1 dry-run |
| libnds API for lights/normals; direct writes only where libnds bypasses the proxy | §2.4 |
| LIT scenes reproduce `lit_a`/`lit_b` GX stream | §3.4 (RGBA identical), §3.3 (parser OK) |
| Existing variants byte-identical to `main` | §2.3 (8 `cmp` exit=0, final tree) |
| ROMs, DeSmuME capture, validate, predict, Azahar, compare tol 12 | §2.1, §3.2-3.5 |
| Tests, `make test`, `.gitignore` | §1, §2.2 |
| SCENE+STATIC_ANGLE+LIT composition test | §1 dry-run, `tools/test_arm9v2_lit.py` |
| Append-only pointers | `docs/EQUALITY_CHECK_DESIGN.md`, `docs/EQ_SCENES.md`, `docs/reports/eq4ze-hw-coverage.md` |
| Report within its line limit, labels, PLAN | this file, §7 |

## 7. Size audit

```
$ { git add -N . && git diff main | wc -c; wc -l docs/reports/eq4zh-lit-rom.md; } > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
68168
805 docs/reports/eq4zh-lit-rom.md
```
