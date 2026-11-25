# EQ-4v: blend-enabled falsification ROM for scene 4

**Status:** implementation complete, hardware capture PLAN only (not run).  
**Scope:** DS-side arm9v2 only; `arm11/`, `arm7/`, `common/`, `tools/azahar_host/` untouched.

---

## 1. What changed

### 1.1 `arm9v2/Makefile` — `BLEND=1` build switch

SOURCE-READ.  A new optional block runs after `AUTODUMP`/`VP`/`CUBES`/`SCENE` and
before `STATIC_ANGLE`/`DIAGSCREEN`, so suffixes compose as
`-autodump…-cN-sN-bl-stN-diagN`.  Empty/unset leaves `TARGET`, `BUILD` and
`CFLAGS` untouched so default ROMs stay byte-identical.  The block validates
that `BLEND` is empty or exactly `1` (the EQ-4u §6.3 plan proposed accepting `0`
as a no-op; EQ-4v narrows this to reject `0`).

```makefile
#---------------------------------------------------------------------------------
# Optional blend-enable build: BLEND=1 adds glEnable(GL_BLEND) to the scene
# draw path so a ROM can be compared with the blend bit set (EQ-4u §6.3
# falsification ROM). Unset/empty leaves TARGET, BUILD and CFLAGS untouched so
# default ROMs stay byte-identical. This block runs after AUTODUMP/VP/CUBES/SCENE
# and before STATIC_ANGLE/DIAGSCREEN, so suffixes compose as
# -autodump...-cN-sN-bl-stN-diagN (e.g. twlhd-arm9v2-autodump-s4-bl-st30).
#---------------------------------------------------------------------------------
BLEND_STRIPPED	:=	$(strip $(BLEND))
ifneq ($(BLEND_STRIPPED),)
BLEND_OK := $(shell echo '$(BLEND_STRIPPED)' | grep -Eq '^1$$' && echo ok)
ifeq ($(BLEND_OK),)
$(error BLEND must be empty or 1 (got: $(BLEND)))
endif
TARGET		:=	$(TARGET)-bl
BUILD		:=	$(BUILD)-bl
CFLAGS		+=	-DTWLHD_DEMO_BLEND=1
# CXXFLAGS was expanded from CFLAGS with := above, so it needs the define too.
CXXFLAGS	+=	-DTWLHD_DEMO_BLEND=1
endif
```

The variant is also recorded in `TWLHD_FLAGS_STAMP` so switching `BLEND`
without `clean` drops stale objects, and clean rules remove `build-bl*/` and
`twlhd-arm9v2*-bl*` artifacts.

### 1.2 `arm9v2/source/demo_frame.cpp` — guarded `glEnable(GL_BLEND)`

SOURCE-READ.  Inside the `#if TWLHD_DEMO_SCENE != 0` per-frame state re-issue
block, immediately after `glEnable(GL_ANTIALIAS);`:

```c
    glEnable(GL_ANTIALIAS);
#ifdef TWLHD_DEMO_BLEND
    /* Falsification build (EQ-4u §6.3): enable DS alpha blending. */
    glEnable(GL_BLEND);
#endif
    glClearColor(0, 0, 0, 31);
```

The call is intentionally only in the scene draw path, not in the cube path or
diag paths.  Because this block is inside the armed recording window
(`main.cpp:240-243`), the `GL_BLEND` enable is recorded as a `DISP3DCNT` RSTA
write (see `eq4u` §4.1).  No interpreter or renderer consumer is added.

### 1.3 Tests and report pointers

- New dry-run test suite: `tools/test_arm9v2_blend.py`.
- Wired into root `Makefile`: `test-arm9v2-blend` target, added to `.PHONY`,
  the `test` prerequisite list, and the discover-skip env line.
- `.gitignore`: added `arm9v2/build-bl/` and `arm9v2/build-*bl*/` so dry-run
  invocations do not create untracked artifacts.
- Append-only pointers added to:
  - `docs/EQUALITY_CHECK_DESIGN.md` (end),
  - `docs/EQ_SCENES.md` (new §13),
  - `docs/reports/eq4u-scene4-alpha-why.md` (end of §6.3).

---

## 2. Byte-identity proof for default (non-BLEND) builds (REQ 1)

FACT.  A clean worktree of `main` was prepared and built side-by-side with the
branch worktree.  Each ROM was built exactly once, then copied to a distinct
name under `/tmp/opencode/eq4v/roms/` before the next build.

```sh
git -C /tmp/opencode/eq4v/main-base rev-parse HEAD
git -C /tmp/opencode/eq4v/main-base status --porcelain
```

```
9624740ba93aa39c661a80291f3df07b0fbf4e00
```

(The status output was empty.)

```sh
git -C /tmp/opencode/eq4v/worktree rev-parse HEAD
git -C /tmp/opencode/eq4v/worktree status --porcelain
git -C /tmp/opencode/eq4v/worktree diff main --stat
```

```
525f32ae5b09d94c2d45df13aaef069543fd7376
 M arm9v2/Makefile
 M tools/test_arm9v2_blend.py
 .gitignore                            |   2 +
 Makefile                              |  11 +-
 arm9v2/Makefile                       |  25 +-
 arm9v2/source/demo_frame.cpp          |   4 +
 docs/EQUALITY_CHECK_DESIGN.md         |   2 +
 docs/EQ_SCENES.md                     |   7 +
 docs/reports/eq4u-scene4-alpha-why.md |   4 +
 docs/reports/eq4v-blend-rom.md        | 692 ++++++++++++++++++++++++++++++++++
 tools/test_arm9v2_blend.py            | 157 ++++++++
 9 files changed, 900 insertions(+), 4 deletions(-)
```

The branch worktree contains the BLEND block (`arm9v2/Makefile`,
`arm9v2/source/demo_frame.cpp`) and the supporting wiring (root `Makefile`,
`.gitignore`, report pointers).  The builds of this section were redone in
repair round 1 so the exact full command lines (including the `tail`, `sed`,
`cp`, `sha256sum` and `cmp` invocations) could be quoted in the record.

The eight variants built on both trees were:

| Variant | Make variables | ROM file name |
|---|---|---|
| cube baseline | `STATIC_ANGLE=30` | `twlhd-arm9v2-st30.nds` |
| scene 1 tri-list | `SCENE=1 STATIC_ANGLE=30` | `twlhd-arm9v2-s1-st30.nds` |
| scene 2 tri-strip | `SCENE=2 STATIC_ANGLE=30` | `twlhd-arm9v2-s2-st30.nds` |
| scene 3 quad-strip | `SCENE=3 STATIC_ANGLE=30` | `twlhd-arm9v2-s3-st30.nds` |
| scene 4 alpha quads | `SCENE=4 STATIC_ANGLE=30` | `twlhd-arm9v2-s4-st30.nds` |
| cubes variant | `CUBES=2 STATIC_ANGLE=30` | `twlhd-arm9v2-c2-st30.nds` |
| scene 4 autodump | `SCENE=4 STATIC_ANGLE=30 AUTODUMP_FRAME=240` | `twlhd-arm9v2-autodump-s4-st30.nds` |
| scene 5 textured | `SCENE=5 STATIC_ANGLE=30` | `twlhd-arm9v2-s5-st30.nds` |

### 2.1 Raw build output tails

**main st30** — `make -C /tmp/opencode/eq4v/main-base/arm9v2 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"`

```
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    
/tmp/opencode/eq4v/main-base/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/opencode/eq4v/main-base/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-st30.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-st30.nds
make: Leaving directory '/tmp/opencode/eq4v/main-base/arm9v2'
```

**branch st30** — `make -C /tmp/opencode/eq4v/worktree/arm9v2 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"`

```
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    
/tmp/opencode/eq4v/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/opencode/eq4v/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-st30.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-st30.nds
make: Leaving directory '/tmp/opencode/eq4v/worktree/arm9v2'
```

```sh
cp /tmp/opencode/eq4v/main-base/arm9v2/twlhd-arm9v2-st30.nds /tmp/opencode/eq4v/roms/main-st30.nds
cp /tmp/opencode/eq4v/worktree/arm9v2/twlhd-arm9v2-st30.nds /tmp/opencode/eq4v/roms/branch-st30.nds
sha256sum /tmp/opencode/eq4v/roms/main-st30.nds /tmp/opencode/eq4v/roms/branch-st30.nds
cmp /tmp/opencode/eq4v/roms/main-st30.nds /tmp/opencode/eq4v/roms/branch-st30.nds; echo "cmp exit: $?"
```

```
6453ba6219d717a7de5a785c165574ad9cf15cc2f04d994c1ab34bc7cd335a01  /tmp/opencode/eq4v/roms/main-st30.nds
6453ba6219d717a7de5a785c165574ad9cf15cc2f04d994c1ab34bc7cd335a01  /tmp/opencode/eq4v/roms/branch-st30.nds
cmp exit: 0
```

**main s1-st30** — `make -C /tmp/opencode/eq4v/main-base/arm9v2 SCENE=1 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"`

```
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    
/tmp/opencode/eq4v/main-base/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/opencode/eq4v/main-base/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-s1-st30.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-s1-st30.nds
make: Leaving directory '/tmp/opencode/eq4v/main-base/arm9v2'
```

**branch s1-st30** — `make -C /tmp/opencode/eq4v/worktree/arm9v2 SCENE=1 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"`

```
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    
/tmp/opencode/eq4v/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/opencode/eq4v/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-s1-st30.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-s1-st30.nds
make: Leaving directory '/tmp/opencode/eq4v/worktree/arm9v2'
```

```sh
cp /tmp/opencode/eq4v/main-base/arm9v2/twlhd-arm9v2-s1-st30.nds /tmp/opencode/eq4v/roms/main-s1-st30.nds
cp /tmp/opencode/eq4v/worktree/arm9v2/twlhd-arm9v2-s1-st30.nds /tmp/opencode/eq4v/roms/branch-s1-st30.nds
sha256sum /tmp/opencode/eq4v/roms/main-s1-st30.nds /tmp/opencode/eq4v/roms/branch-s1-st30.nds
cmp /tmp/opencode/eq4v/roms/main-s1-st30.nds /tmp/opencode/eq4v/roms/branch-s1-st30.nds; echo "cmp exit: $?"
```

```
f9f0d5486c777f08fb80eebf8034e648ad5dbdfd9cb799bd8782208bf23ce801  /tmp/opencode/eq4v/roms/main-s1-st30.nds
f9f0d5486c777f08fb80eebf8034e648ad5dbdfd9cb799bd8782208bf23ce801  /tmp/opencode/eq4v/roms/branch-s1-st30.nds
cmp exit: 0
```

**main s2-st30** — `make -C /tmp/opencode/eq4v/main-base/arm9v2 SCENE=2 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"`

```
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    
/tmp/opencode/eq4v/main-base/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/opencode/eq4v/main-base/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-s2-st30.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-s2-st30.nds
make: Leaving directory '/tmp/opencode/eq4v/main-base/arm9v2'
```

**branch s2-st30** — `make -C /tmp/opencode/eq4v/worktree/arm9v2 SCENE=2 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"`

```
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    
/tmp/opencode/eq4v/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/opencode/eq4v/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-s2-st30.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-s2-st30.nds
make: Leaving directory '/tmp/opencode/eq4v/worktree/arm9v2'
```

```sh
cp /tmp/opencode/eq4v/main-base/arm9v2/twlhd-arm9v2-s2-st30.nds /tmp/opencode/eq4v/roms/main-s2-st30.nds
cp /tmp/opencode/eq4v/worktree/arm9v2/twlhd-arm9v2-s2-st30.nds /tmp/opencode/eq4v/roms/branch-s2-st30.nds
sha256sum /tmp/opencode/eq4v/roms/main-s2-st30.nds /tmp/opencode/eq4v/roms/branch-s2-st30.nds
cmp /tmp/opencode/eq4v/roms/main-s2-st30.nds /tmp/opencode/eq4v/roms/branch-s2-st30.nds; echo "cmp exit: $?"
```

```
dd26ec63bdc20cac510000f515302c0606123dc7d4b0f49ea78c9b84c5413ced  /tmp/opencode/eq4v/roms/main-s2-st30.nds
dd26ec63bdc20cac510000f515302c0606123dc7d4b0f49ea78c9b84c5413ced  /tmp/opencode/eq4v/roms/branch-s2-st30.nds
cmp exit: 0
```

**main s3-st30** — `make -C /tmp/opencode/eq4v/main-base/arm9v2 SCENE=3 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"`

```
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    
/tmp/opencode/eq4v/main-base/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/opencode/eq4v/main-base/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-s3-st30.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-s3-st30.nds
make: Leaving directory '/tmp/opencode/eq4v/main-base/arm9v2'
```

**branch s3-st30** — `make -C /tmp/opencode/eq4v/worktree/arm9v2 SCENE=3 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"`

```
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    
/tmp/opencode/eq4v/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/opencode/eq4v/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-s3-st30.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-s3-st30.nds
make: Leaving directory '/tmp/opencode/eq4v/worktree/arm9v2'
```

```sh
cp /tmp/opencode/eq4v/main-base/arm9v2/twlhd-arm9v2-s3-st30.nds /tmp/opencode/eq4v/roms/main-s3-st30.nds
cp /tmp/opencode/eq4v/worktree/arm9v2/twlhd-arm9v2-s3-st30.nds /tmp/opencode/eq4v/roms/branch-s3-st30.nds
sha256sum /tmp/opencode/eq4v/roms/main-s3-st30.nds /tmp/opencode/eq4v/roms/branch-s3-st30.nds
cmp /tmp/opencode/eq4v/roms/main-s3-st30.nds /tmp/opencode/eq4v/roms/branch-s3-st30.nds; echo "cmp exit: $?"
```

```
5c6e8c2c627e7add146e88fadc10a92d088a0f40efd74300bc6c29112d52ff93  /tmp/opencode/eq4v/roms/main-s3-st30.nds
5c6e8c2c627e7add146e88fadc10a92d088a0f40efd74300bc6c29112d52ff93  /tmp/opencode/eq4v/roms/branch-s3-st30.nds
cmp exit: 0
```

**main s4-st30** — `make -C /tmp/opencode/eq4v/main-base/arm9v2 SCENE=4 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"`

```
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    
/tmp/opencode/eq4v/main-base/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/opencode/eq4v/main-base/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-s4-st30.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-s4-st30.nds
make: Leaving directory '/tmp/opencode/eq4v/main-base/arm9v2'
```

**branch s4-st30** — `make -C /tmp/opencode/eq4v/worktree/arm9v2 SCENE=4 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"`

```
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    
/tmp/opencode/eq4v/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/opencode/eq4v/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-s4-st30.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-s4-st30.nds
make: Leaving directory '/tmp/opencode/eq4v/worktree/arm9v2'
```

```sh
cp /tmp/opencode/eq4v/main-base/arm9v2/twlhd-arm9v2-s4-st30.nds /tmp/opencode/eq4v/roms/main-s4-st30.nds
cp /tmp/opencode/eq4v/worktree/arm9v2/twlhd-arm9v2-s4-st30.nds /tmp/opencode/eq4v/roms/branch-s4-st30.nds
sha256sum /tmp/opencode/eq4v/roms/main-s4-st30.nds /tmp/opencode/eq4v/roms/branch-s4-st30.nds
cmp /tmp/opencode/eq4v/roms/main-s4-st30.nds /tmp/opencode/eq4v/roms/branch-s4-st30.nds; echo "cmp exit: $?"
```

```
9662c06c7aeefa18807112c1d66b96003a567550a4a9bd18709b95ea75c80eba  /tmp/opencode/eq4v/roms/main-s4-st30.nds
9662c06c7aeefa18807112c1d66b96003a567550a4a9bd18709b95ea75c80eba  /tmp/opencode/eq4v/roms/branch-s4-st30.nds
cmp exit: 0
```

**main c2-st30** — `make -C /tmp/opencode/eq4v/main-base/arm9v2 CUBES=2 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"`

```
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    
/tmp/opencode/eq4v/main-base/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/opencode/eq4v/main-base/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-c2-st30.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-c2-st30.nds
make: Leaving directory '/tmp/opencode/eq4v/main-base/arm9v2'
```

**branch c2-st30** — `make -C /tmp/opencode/eq4v/worktree/arm9v2 CUBES=2 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"`

```
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    
/tmp/opencode/eq4v/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/opencode/eq4v/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-c2-st30.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-c2-st30.nds
make: Leaving directory '/tmp/opencode/eq4v/worktree/arm9v2'
```

```sh
cp /tmp/opencode/eq4v/main-base/arm9v2/twlhd-arm9v2-c2-st30.nds /tmp/opencode/eq4v/roms/main-c2-st30.nds
cp /tmp/opencode/eq4v/worktree/arm9v2/twlhd-arm9v2-c2-st30.nds /tmp/opencode/eq4v/roms/branch-c2-st30.nds
sha256sum /tmp/opencode/eq4v/roms/main-c2-st30.nds /tmp/opencode/eq4v/roms/branch-c2-st30.nds
cmp /tmp/opencode/eq4v/roms/main-c2-st30.nds /tmp/opencode/eq4v/roms/branch-c2-st30.nds; echo "cmp exit: $?"
```

```
43e39eb87558fb1019699c33d8cd333ecf191ba747532bf163fabfc4b4ec2940  /tmp/opencode/eq4v/roms/main-c2-st30.nds
43e39eb87558fb1019699c33d8cd333ecf191ba747532bf163fabfc4b4ec2940  /tmp/opencode/eq4v/roms/branch-c2-st30.nds
cmp exit: 0
```

**main autodump-s4-st30** — `make -C /tmp/opencode/eq4v/main-base/arm9v2 SCENE=4 STATIC_ANGLE=30 AUTODUMP_FRAME=240 2>&1 | tail -n 15 | sed "s|$HOME|~|g"`

```
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    
/tmp/opencode/eq4v/main-base/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/opencode/eq4v/main-base/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-autodump-s4-st30.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-autodump-s4-st30.nds
make: Leaving directory '/tmp/opencode/eq4v/main-base/arm9v2'
```

**branch autodump-s4-st30** — `make -C /tmp/opencode/eq4v/worktree/arm9v2 SCENE=4 STATIC_ANGLE=30 AUTODUMP_FRAME=240 2>&1 | tail -n 15 | sed "s|$HOME|~|g"`

```
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    
/tmp/opencode/eq4v/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/opencode/eq4v/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-autodump-s4-st30.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-autodump-s4-st30.nds
make: Leaving directory '/tmp/opencode/eq4v/worktree/arm9v2'
```

```sh
cp /tmp/opencode/eq4v/main-base/arm9v2/twlhd-arm9v2-autodump-s4-st30.nds /tmp/opencode/eq4v/roms/main-autodump-s4-st30.nds
cp /tmp/opencode/eq4v/worktree/arm9v2/twlhd-arm9v2-autodump-s4-st30.nds /tmp/opencode/eq4v/roms/branch-autodump-s4-st30.nds
sha256sum /tmp/opencode/eq4v/roms/main-autodump-s4-st30.nds /tmp/opencode/eq4v/roms/branch-autodump-s4-st30.nds
cmp /tmp/opencode/eq4v/roms/main-autodump-s4-st30.nds /tmp/opencode/eq4v/roms/branch-autodump-s4-st30.nds; echo "cmp exit: $?"
```

```
0f63a52595c3774aa1b7400b4c112c3062c08a6dd788f28939f14bd962629ff7  /tmp/opencode/eq4v/roms/main-autodump-s4-st30.nds
0f63a52595c3774aa1b7400b4c112c3062c08a6dd788f28939f14bd962629ff7  /tmp/opencode/eq4v/roms/branch-autodump-s4-st30.nds
cmp exit: 0
```

**main s5-st30** — `make -C /tmp/opencode/eq4v/main-base/arm9v2 SCENE=5 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"`

```
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    
/tmp/opencode/eq4v/main-base/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/opencode/eq4v/main-base/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-s5-st30.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-s5-st30.nds
make: Leaving directory '/tmp/opencode/eq4v/main-base/arm9v2'
```

**branch s5-st30** — `make -C /tmp/opencode/eq4v/worktree/arm9v2 SCENE=5 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"`

```
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    
/tmp/opencode/eq4v/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/opencode/eq4v/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-s5-st30.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-s5-st30.nds
make: Leaving directory '/tmp/opencode/eq4v/worktree/arm9v2'
```

```sh
cp /tmp/opencode/eq4v/main-base/arm9v2/twlhd-arm9v2-s5-st30.nds /tmp/opencode/eq4v/roms/main-s5-st30.nds
cp /tmp/opencode/eq4v/worktree/arm9v2/twlhd-arm9v2-s5-st30.nds /tmp/opencode/eq4v/roms/branch-s5-st30.nds
sha256sum /tmp/opencode/eq4v/roms/main-s5-st30.nds /tmp/opencode/eq4v/roms/branch-s5-st30.nds
cmp /tmp/opencode/eq4v/roms/main-s5-st30.nds /tmp/opencode/eq4v/roms/branch-s5-st30.nds; echo "cmp exit: $?"
```

```
1b6cb5c10fe32e85f2c498d145240836476c40360a9a86f9a8dc9df44944a105  /tmp/opencode/eq4v/roms/main-s5-st30.nds
1b6cb5c10fe32e85f2c498d145240836476c40360a9a86f9a8dc9df44944a105  /tmp/opencode/eq4v/roms/branch-s5-st30.nds
cmp exit: 0
```


All eight `cmp` calls exited `0`.  Default ROMs are byte-identical between the
clean `main` worktree and the branch.

---

## 3. BLEND=1 ROMs and control (REQ 2)

FACT.  Built on the branch:

```sh
make -C /tmp/codex-20261125-014147-3330372/worktree/arm9v2 clean
make -C /tmp/codex-20261125-014147-3330372/worktree/arm9v2 SCENE=4 STATIC_ANGLE=30 BLEND=1 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
```

```
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    
/tmp/codex-20261125-014147-3330372/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261125-014147-3330372/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-s4-bl-st30.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-s4-bl-st30.nds
make: Leaving directory '/tmp/codex-20261125-014147-3330372/worktree/arm9v2'
```

```sh
sha256sum /tmp/codex-20261125-014147-3330372/worktree/arm9v2/twlhd-arm9v2-s4-bl-st30.nds
cp /tmp/codex-20261125-014147-3330372/worktree/arm9v2/twlhd-arm9v2-s4-bl-st30.nds /tmp/opencode/eq4v/roms/branch-s4-bl-st30.nds
```

```
e0aa3778c91ae0a06ecc895a92f3b6cb41fd8775158af579b24ebe4b678e6d29  /tmp/codex-20261125-014147-3330372/worktree/arm9v2/twlhd-arm9v2-s4-bl-st30.nds
```

FACT.  Emulator/autodump blend ROM:

```sh
make -C /tmp/codex-20261125-014147-3330372/worktree/arm9v2 clean
make -C /tmp/codex-20261125-014147-3330372/worktree/arm9v2 SCENE=4 STATIC_ANGLE=30 BLEND=1 AUTODUMP_FRAME=240 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
```

```
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    
/tmp/codex-20261125-014147-3330372/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261125-014147-3330372/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-autodump-s4-bl-st30.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-autodump-s4-bl-st30.nds
make: Leaving directory '/tmp/codex-20261125-014147-3330372/worktree/arm9v2'
```

```sh
sha256sum /tmp/codex-20261125-014147-3330372/worktree/arm9v2/twlhd-arm9v2-autodump-s4-bl-st30.nds
cp /tmp/codex-20261125-014147-3330372/worktree/arm9v2/twlhd-arm9v2-autodump-s4-bl-st30.nds /tmp/opencode/eq4v/roms/branch-autodump-s4-bl-st30.nds
```

```
64f60afcddc39748affb33d69e19411d21f4137e09a105d3c7901a1044956d8a  /tmp/codex-20261125-014147-3330372/worktree/arm9v2/twlhd-arm9v2-autodump-s4-bl-st30.nds
```

FACT.  Control comparison against the non-blend `SCENE=4 STATIC_ANGLE=30` ROM
built in REQ 1 (not rebuilt):

```sh
cmp /tmp/opencode/eq4v/roms/branch-s4-bl-st30.nds /tmp/opencode/eq4v/roms/branch-s4-st30.nds; echo "cmp exit: $?"
```

```
/tmp/opencode/eq4v/roms/branch-s4-bl-st30.nds /tmp/opencode/eq4v/roms/branch-s4-st30.nds differ: byte 45, line 1
cmp exit: 1
```

SOURCE-READ.  `GL_BLEND` appears only inside the `TWLHD_DEMO_BLEND`
guard in `arm9v2/source/`:

```sh
grep -rn "GL_BLEND" arm9v2/source
```

```
arm9v2/source/demo_frame.cpp:355:    glEnable(GL_BLEND);
```

```sh
sed -n '350,360p' arm9v2/source/demo_frame.cpp
```

```c
#if TWLHD_DEMO_SCENE != 0
    /* Re-issue all state needed to replay this frame independently. */
    glEnable(GL_ANTIALIAS);
#ifdef TWLHD_DEMO_BLEND
    /* Falsification build (EQ-4u §6.3): enable DS alpha blending. */
    glEnable(GL_BLEND);
#endif
    glClearColor(0, 0, 0, 31);
```

---

## 4. Exact ROM file names

| Purpose | Build command | ROM file | sha256 |
|---|---|---|---|
| Hardware falsification ROM | `make -C arm9v2 SCENE=4 STATIC_ANGLE=30 BLEND=1` | `twlhd-arm9v2-s4-bl-st30.nds` | `e0aa3778c91ae0a06ecc895a92f3b6cb41fd8775158af579b24ebe4b678e6d29` |
| Emulator/autodump ROM | `make -C arm9v2 SCENE=4 STATIC_ANGLE=30 BLEND=1 AUTODUMP_FRAME=240` | `twlhd-arm9v2-autodump-s4-bl-st30.nds` | `64f60afcddc39748affb33d69e19411d21f4137e09a105d3c7901a1044956d8a` |
| Non-blend control | `make -C arm9v2 SCENE=4 STATIC_ANGLE=30` | `twlhd-arm9v2-s4-st30.nds` | `9662c06c7aeefa18807112c1d66b96003a567550a4a9bd18709b95ea75c80eba` |

---

## 5. Console-run PLAN (not run)

PLAN.  Run the hardware falsification ROM exactly as `eq4r` §2 captured
`twlhd-arm9v2-s4-st30.nds`:

1. Copy `twlhd-arm9v2-s4-bl-st30.nds`
   (`sha256: e0aa3778c91ae0a06ecc895a92f3b6cb41fd8775158af579b24ebe4b678e6d29`)
   to `/roms/nds/` on the SD card.
2. Start it from TWiLight Menu++ in **DS mode** and let the static frame display.
3. Capture with the cc3dsfs board using the same flags as the `eq4r` scene-4
   capture:
   `--enabled_both 0 --enabled_top 1 --scaling_top 1`
   (`docs/reports/eq4r-static-scenes-hw-compare.md` §2; 24-bpp
   `xwd2png.py`, capture letter `a`).
4. Crop the top 400×240 region and read the alpha-16 and alpha-4 quad colours.

HYPOTHESIS.  With `GL_BLEND` enabled, the alpha-16 and alpha-4 quads should stop
being full brightness and dim toward the black backdrop by the source-alpha
weight.  The predicted pixel values are cited from
`docs/reports/eq4h-alpha-convention-test.md` §2 (`python3 tools/ds_alpha_test/expected.py`)
and from `docs/reports/eq4u-scene4-alpha-why.md` §6.3:

```
a= 4 A1=33/33/33/32/32 A2=41/33/40/39/36 sep=5bit_round=8/5bit_floor=0/8bit_round=7/8bit_floor=7/6bit_round=4
a=16 A1=132/132/132/131/130 A2=132/132/135/135/134 sep=5bit_round=0/5bit_floor=0/8bit_round=3/8bit_floor=4/6bit_round=4
```

- alpha 16 green channel: **134** (6-bit A2 column; `eq4i` §5.1 measured
  hardware green matching the 6-bit A2 model; `eq4u` §6.3 predicts 134).
- alpha 4 blue channel: **36** (6-bit A2 column; `eq4u` §6.3 predicts 36).

The alpha-0 quad outline is predicted to be unaffected by the blend enable
(`eq4u` §6.3 separation argument).

UNVERIFIED:

- That `glEnable(GL_BLEND)` alone enables DS alpha blending on real hardware
  (no hardware run was performed in this task).
- The red/blue A2-vs-A1 origin and other mechanisms listed in `eq4u` §6.3.
- End-to-end capture pipeline behaviour for the new ROM.

**Follow-up (EQ-4w, 2026-11-25):** this plan was executed by the owner (one
console, one capture session) and recorded in
`docs/reports/eq4w-blend-hw-result.md`: alpha-16 green `134` (prediction 134)
and alpha-4 blue `33` (prediction 36), with the alpha-0 outline present in both
captures. Tool and fixtures: `tools/eq4w_blend_hw/`.

---

## 6. Test result

FACT.  The new `tools/test_arm9v2_blend.py` suite passes, and the aggregate
`make test` discover pass skips it via `ARM9V2_BLEND_DISCOVER_SKIP=1` after the
explicit target runs.

```sh
make test-arm9v2-blend
```

```
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_arm9v2_blend.py'
........
----------------------------------------------------------------------
Ran 8 tests in 0.325s

OK
```

FACT.  Aggregate `make test` was run once for the whole project:

```sh
make test
```

```
EQ2_DISCOVER_SKIP=1 EQ3_DISCOVER_SKIP=1 EQ_SCENES_DISCOVER_SKIP=1 \
ARM9V2_STATIC_DISCOVER_SKIP=1 ARM9V2_BLEND_DISCOVER_SKIP=1 EQ4_HW_COMPARE_DISCOVER_SKIP=1 \
FIT_MAPPING_DISCOVER_SKIP=1 AZAHAR_PRESENTATION_DISCOVER_SKIP=1 \
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
ssssssss.....sssssssssssssssssssss..........................................................................................................usage: compare_texture_capture.py <captured.bin> [angle]
usage: compare_texture_capture.py <captured.bin> [angle]
error: angle must be an integer
........ssssssssssssssssssssssssssss...ssssssssssssssssssssssssssssssssssssssssssssssss...........................................................................................................................................................................................................
----------------------------------------------------------------------
Ran 430 tests in 87.785s

OK (skipped=105)
/tmp/tmp_032cm29/twlhd_frame_v2_tex_over.bin
vertex_count=36
max_clip_deviation=0.007324
colour_exactness=PASS
```

---

## 7. Diff size

FACT.

```sh
git add -N . && git diff main | wc -c
```

```
51127
```
