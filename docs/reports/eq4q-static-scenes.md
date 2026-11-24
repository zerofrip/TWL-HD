<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4q — static angle build for every arm9v2 demo scene

**Scope:** extend the `STATIC_ANGLE=0..359` build option from the textured
SCENE=5 to **every** demo draw path (cube baseline, SCENE=1..4, CUBES=2 grid),
prove the default builds stay byte-identical, prove the new static variants
differ, extend the dry-run test suite, and document the owner hardware-comparison
plan. No ROM is run on a console in this task.

**Labels.**
- **FACT** = a quoted command and its raw complete output.
- **SOURCE-READ** = a file:line reference to repository source.
- **HYPOTHESIS** = a design assumption or planned step not measured here.
- **UNVERIFIED** = a claim that is explicitly not proven in this task.

---

## 1. What changed

### 1.1 Source guard

`arm9v2/source/demo_frame.cpp` now defines one shared binary-angle macro at
file scope immediately before `twlhd_demo_draw_frame` and uses it in all three
`glRotateXi/Yi` sites that previously rotated with the frame counter.

SOURCE-READ:

- `arm9v2/source/demo_frame.cpp:312-316` — `TWLHD_DEMO_STATIC_ANGLE_BIN` macro.
- `arm9v2/source/demo_frame.cpp:366-379` — SCENE≠0 shared draw path.
- `arm9v2/source/demo_frame.cpp:426-433` — single-cube baseline / DIAGSCREEN 7/8/9 path.
- `arm9v2/source/demo_frame.cpp:485-492` — multi-cube grid (CUBES>1) loop.

FACT — the three `(int)angle` sites are now inside `#else` arms of the static
define, and the old scene-5-only combined condition is gone:

```
$ grep -n 'glRotate\|STATIC_ANGLE' arm9v2/source/demo_frame.cpp
312:#ifdef TWLHD_DEMO_STATIC_ANGLE_DEG
313:/* Fixed modelview angle for every draw path (STATIC_ANGLE=0..359). */
314:#define TWLHD_DEMO_STATIC_ANGLE_BIN \
315:    ((int)((unsigned)TWLHD_DEMO_STATIC_ANGLE_DEG * 32768u / 360u))
326:    glRotateX(simple_tri_rotateX);
327:    glRotateY(simple_tri_rotateY);
366:#if defined(TWLHD_DEMO_STATIC_ANGLE_DEG)
367:    /* Fixed modelview angle for every draw path (STATIC_ANGLE=0..359 in the
372:    glRotateXi(TWLHD_DEMO_STATIC_ANGLE_BIN);
373:    glRotateYi(TWLHD_DEMO_STATIC_ANGLE_BIN);
375:    glRotateXi((int)angle);
376:    glRotateYi((int)angle);
426:#if defined(TWLHD_DEMO_STATIC_ANGLE_DEG)
428:    glRotateXi(TWLHD_DEMO_STATIC_ANGLE_BIN);
429:    glRotateYi(TWLHD_DEMO_STATIC_ANGLE_BIN);
431:    glRotateXi((int)angle);
432:    glRotateYi((int)angle);
485:#if defined(TWLHD_DEMO_STATIC_ANGLE_DEG)
487:        glRotateXi(TWLHD_DEMO_STATIC_ANGLE_BIN);
488:        glRotateYi(TWLHD_DEMO_STATIC_ANGLE_BIN);
490:        glRotateXi((int)angle);
491:        glRotateYi((int)angle);
$ grep -n 'defined(TWLHD_DEMO_STATIC_ANGLE_DEG) && TWLHD_DEMO_SCENE == 5' arm9v2/source/demo_frame.cpp
[no output]
```

The `DIAGSCREEN=6` Simple_Tri path uses `glRotateX(simple_tri_rotateX)` /
`glRotateY(simple_tri_rotateY)` with float counters and is intentionally
untouched — it is outside the `glRotateXi/Yi((int)angle)` inventory named in the
task.

### 1.2 Makefile comment

SOURCE-READ: `arm9v2/Makefile:154-176` — the comment now says the define applies
to the cube baseline, SCENE=1..4, CUBES>1 and SCENE=5; the old
"accepted but unused when SCENE != 5" caveat is removed. No functional Makefile
change.

### 1.3 Tests

SOURCE-READ: `tools/test_arm9v2_static_angle.py:132-227` — added/extended:

- the existing negative test for invalid angles (`abc`, `360`, etc.) at
  `:132-137`;
- a new combined negative test at `:139-146` that runs invalid `STATIC_ANGLE`
  values (`-1`, `360`, `abc`) together with no scene, `SCENE=1..4`, and
  `CUBES=2`;
- positive dry-runs for `STATIC_ANGLE=30` with no scene, `SCENE=1..4`, and
  `CUBES=2` at `:148-164`;
- a source-level check at `:166-172` that the old
  `#if defined(TWLHD_DEMO_STATIC_ANGLE_DEG) && TWLHD_DEMO_SCENE == 5`
  condition is absent;
- a preprocessor-lite scan at `:174-184` (helper at `:30-52`) asserting every
  `glRotateXi((int)angle)` / `glRotateYi((int)angle)` occurrence sits in the
  `#else` arm of a `#if defined(TWLHD_DEMO_STATIC_ANGLE_DEG)` guard;
- a negative self-check at `:186-227` that removes one guard block's `#if` and
  matching `#else` in an in-memory copy and proves the scanner reports exactly
  the two resulting unguarded sites.

---

## 2. Base commit

FACT — base `main` HEAD used for the before-build worktree.  The worktree was
added at `main-base/`, verified clean, and then removed so it does not appear
as an untracked directory in the working branch:

```
$ git -C main-base rev-parse HEAD
d21d18953e4452a52a85afc668f5b858a517b73e
$ git -C main-base status --porcelain
```

---

## 3. Default-build byte identity (STATIC_ANGLE unset)

Each ROM was built once on a clean checkout of `main` at the base commit
(`main-base/`) and once on the working branch with `STATIC_ANGLE` unset.
The quoted lines are the raw pipeline output (last 15 lines of `make`, with
`$HOME` redacted to `~`) followed by the full `sha256sum` output.  Each ROM was
copied to a distinct scratch name under `/tmp/opencode/eq4q/` immediately after
its build so later builds could not overwrite it.

### cube baseline

FACT — main:
```
$ make -C main-base/arm9v2 clean && make -C main-base/arm9v2 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/main-base/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/main-base/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2.nds
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/main-base/arm9v2'
$ sha256sum main-base/arm9v2/twlhd-arm9v2.nds
d7427286347d809c17376af6c5037b0d08e3de28f5b8c08d42f3e747450cfb6a  main-base/arm9v2/twlhd-arm9v2.nds
```

FACT — branch:
```
$ make -C arm9v2 clean && make -C arm9v2 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2.nds
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/arm9v2'
$ sha256sum arm9v2/twlhd-arm9v2.nds
d7427286347d809c17376af6c5037b0d08e3de28f5b8c08d42f3e747450cfb6a  arm9v2/twlhd-arm9v2.nds
```

### SCENE=1

FACT — main:
```
$ make -C main-base/arm9v2 clean && make -C main-base/arm9v2 SCENE=1 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/main-base/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/main-base/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-s1.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-s1.nds
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/main-base/arm9v2'
$ sha256sum main-base/arm9v2/twlhd-arm9v2-s1.nds
a3f89a56e761243adbdd19c673843eb78348e73ff839a831b9af79a326d909f5  main-base/arm9v2/twlhd-arm9v2-s1.nds
```

FACT — branch:
```
$ make -C arm9v2 clean && make -C arm9v2 SCENE=1 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-s1.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-s1.nds
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/arm9v2'
$ sha256sum arm9v2/twlhd-arm9v2-s1.nds
a3f89a56e761243adbdd19c673843eb78348e73ff839a831b9af79a326d909f5  arm9v2/twlhd-arm9v2-s1.nds
```

### SCENE=2

FACT — main:
```
$ make -C main-base/arm9v2 clean && make -C main-base/arm9v2 SCENE=2 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/main-base/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/main-base/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-s2.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-s2.nds
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/main-base/arm9v2'
$ sha256sum main-base/arm9v2/twlhd-arm9v2-s2.nds
479792983437417d8e95fb97e5c872ddef0c97cf3dd2839917c977cceb0564ba  main-base/arm9v2/twlhd-arm9v2-s2.nds
```

FACT — branch:
```
$ make -C arm9v2 clean && make -C arm9v2 SCENE=2 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-s2.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-s2.nds
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/arm9v2'
$ sha256sum arm9v2/twlhd-arm9v2-s2.nds
479792983437417d8e95fb97e5c872ddef0c97cf3dd2839917c977cceb0564ba  arm9v2/twlhd-arm9v2-s2.nds
```

### SCENE=3

FACT — main:
```
$ make -C main-base/arm9v2 clean && make -C main-base/arm9v2 SCENE=3 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/main-base/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/main-base/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-s3.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-s3.nds
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/main-base/arm9v2'
$ sha256sum main-base/arm9v2/twlhd-arm9v2-s3.nds
c81f45d72d1869e5e9b8aad70c3faf3f376f4fbc79a1299ae1bb0abf0b37dc9e  main-base/arm9v2/twlhd-arm9v2-s3.nds
```

FACT — branch:
```
$ make -C arm9v2 clean && make -C arm9v2 SCENE=3 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-s3.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-s3.nds
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/arm9v2'
$ sha256sum arm9v2/twlhd-arm9v2-s3.nds
c81f45d72d1869e5e9b8aad70c3faf3f376f4fbc79a1299ae1bb0abf0b37dc9e  arm9v2/twlhd-arm9v2-s3.nds
```

### SCENE=4

FACT — main:
```
$ make -C main-base/arm9v2 clean && make -C main-base/arm9v2 SCENE=4 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/main-base/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/main-base/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-s4.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-s4.nds
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/main-base/arm9v2'
$ sha256sum main-base/arm9v2/twlhd-arm9v2-s4.nds
f9a2f7f146aeffd0bb8cbf82f5e6ad0624eff24b4b26ab826e86133081665626  main-base/arm9v2/twlhd-arm9v2-s4.nds
```

FACT — branch:
```
$ make -C arm9v2 clean && make -C arm9v2 SCENE=4 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-s4.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-s4.nds
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/arm9v2'
$ sha256sum arm9v2/twlhd-arm9v2-s4.nds
f9a2f7f146aeffd0bb8cbf82f5e6ad0624eff24b4b26ab826e86133081665626  arm9v2/twlhd-arm9v2-s4.nds
```

### CUBES=2

FACT — main:
```
$ make -C main-base/arm9v2 clean && make -C main-base/arm9v2 CUBES=2 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/main-base/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/main-base/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-c2.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-c2.nds
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/main-base/arm9v2'
$ sha256sum main-base/arm9v2/twlhd-arm9v2-c2.nds
6fd8abf1dc6565c32e9e4c2663a3ce80542fe184e5026f131c89a85fd8caeb89  main-base/arm9v2/twlhd-arm9v2-c2.nds
```

FACT — branch:
```
$ make -C arm9v2 clean && make -C arm9v2 CUBES=2 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-c2.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-c2.nds
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/arm9v2'
$ sha256sum arm9v2/twlhd-arm9v2-c2.nds
6fd8abf1dc6565c32e9e4c2663a3ce80542fe184e5026f131c89a85fd8caeb89  arm9v2/twlhd-arm9v2-c2.nds
```

All six default ROMs are byte-identical between `main` and the branch.

---

## 4. Static-angle builds

Each ROM was built once on the branch with `STATIC_ANGLE=30` for the cube
baseline, SCENE=1..4 and CUBES=2, plus `SCENE=5 STATIC_ANGLE=30` to prove the
existing textured-scene path is unchanged. Two additional builds
(`STATIC_ANGLE=315`) for the cube baseline and SCENE=1 prove that different
angles produce different ROMs.  Every ROM was copied to a distinct scratch name
under `/tmp/opencode/eq4q/` immediately after its build.

FACT — `STATIC_ANGLE=30` builds:

```
$ make -C arm9v2 clean && make -C arm9v2 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
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
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/arm9v2'
$ sha256sum arm9v2/twlhd-arm9v2-st30.nds
6453ba6219d717a7de5a785c165574ad9cf15cc2f04d994c1ab34bc7cd335a01  arm9v2/twlhd-arm9v2-st30.nds

$ make -C arm9v2 clean && make -C arm9v2 SCENE=1 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
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
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/arm9v2'
$ sha256sum arm9v2/twlhd-arm9v2-s1-st30.nds
f9f0d5486c777f08fb80eebf8034e648ad5dbdfd9cb799bd8782208bf23ce801  arm9v2/twlhd-arm9v2-s1-st30.nds

$ make -C arm9v2 clean && make -C arm9v2 SCENE=2 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
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
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/arm9v2'
$ sha256sum arm9v2/twlhd-arm9v2-s2-st30.nds
dd26ec63bdc20cac510000f515302c0606123dc7d4b0f49ea78c9b84c5413ced  arm9v2/twlhd-arm9v2-s2-st30.nds

$ make -C arm9v2 clean && make -C arm9v2 SCENE=3 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
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
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/arm9v2'
$ sha256sum arm9v2/twlhd-arm9v2-s3-st30.nds
5c6e8c2c627e7add146e88fadc10a92d088a0f40efd74300bc6c29112d52ff93  arm9v2/twlhd-arm9v2-s3-st30.nds

$ make -C arm9v2 clean && make -C arm9v2 SCENE=4 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
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
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/arm9v2'
$ sha256sum arm9v2/twlhd-arm9v2-s4-st30.nds
9662c06c7aeefa18807112c1d66b96003a567550a4a9bd18709b95ea75c80eba  arm9v2/twlhd-arm9v2-s4-st30.nds

$ make -C arm9v2 clean && make -C arm9v2 CUBES=2 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
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
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/arm9v2'
$ sha256sum arm9v2/twlhd-arm9v2-c2-st30.nds
43e39eb87558fb1019699c33d8cd333ecf191ba747532bf163fabfc4b4ec2940  arm9v2/twlhd-arm9v2-c2-st30.nds

$ make -C arm9v2 clean && make -C arm9v2 SCENE=5 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
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
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/arm9v2'
$ sha256sum arm9v2/twlhd-arm9v2-s5-st30.nds
1b6cb5c10fe32e85f2c498d145240836476c40360a9a86f9a8dc9df44944a105  arm9v2/twlhd-arm9v2-s5-st30.nds
```

FACT — `STATIC_ANGLE=315` difference proof:

```
$ make -C arm9v2 clean && make -C arm9v2 STATIC_ANGLE=315 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-st315.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-st315.nds
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/arm9v2'
$ sha256sum arm9v2/twlhd-arm9v2-st315.nds
bed94045f7e7d0442d5dd9ffc952973f7e20b99db75f59f61aac548928fdc423  arm9v2/twlhd-arm9v2-st315.nds

$ make -C arm9v2 clean && make -C arm9v2 SCENE=1 STATIC_ANGLE=315 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm9v2-s1-st315.elf
Checking for guard variables...
Guard-variable check passed.
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... twlhd-arm9v2-s1-st315.nds
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/arm9v2'
$ sha256sum arm9v2/twlhd-arm9v2-s1-st315.nds
83be889fcf77fec1268dbc1d00d8d377a26c478566ff09e91a8b085669382fd1  arm9v2/twlhd-arm9v2-s1-st315.nds
```

The two angles produce different ROMs for both the cube baseline and SCENE=1.

FACT — `SCENE=5 STATIC_ANGLE=30` unchanged versus `main`:

FACT — main:
```
$ make -C main-base/arm9v2 clean && make -C main-base/arm9v2 SCENE=5 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/main-base/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/main-base/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
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
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/main-base/arm9v2'
$ sha256sum main-base/arm9v2/twlhd-arm9v2-s5-st30.nds
1b6cb5c10fe32e85f2c498d145240836476c40360a9a86f9a8dc9df44944a105  main-base/arm9v2/twlhd-arm9v2-s5-st30.nds
```

FACT — branch:
```
$ make -C arm9v2 clean && make -C arm9v2 SCENE=5 STATIC_ANGLE=30 2>&1 | tail -n 15 | sed "s|$HOME|~|g"
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
/tmp/codex-20261124-150106-327844/worktree/arm9v2/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
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
make: Leaving directory '/tmp/codex-20261124-150106-327844/worktree/arm9v2'
$ sha256sum arm9v2/twlhd-arm9v2-s5-st30.nds
1b6cb5c10fe32e85f2c498d145240836476c40360a9a86f9a8dc9df44944a105  arm9v2/twlhd-arm9v2-s5-st30.nds
```

The two hashes are equal, so the existing textured-scene static path is
unchanged.

FACT — byte-level difference proofs on the scratch copies:

```
$ cmp /tmp/opencode/eq4q/branch-st30.nds /tmp/opencode/eq4q/branch-st315.nds; echo exit=$?
/tmp/opencode/eq4q/branch-st30.nds /tmp/opencode/eq4q/branch-st315.nds differ: byte 109, line 1
exit=1
$ cmp /tmp/opencode/eq4q/branch-s1-st30.nds /tmp/opencode/eq4q/branch-s1-st315.nds; echo exit=$?
/tmp/opencode/eq4q/branch-s1-st30.nds /tmp/opencode/eq4q/branch-s1-st315.nds differ: byte 109, line 1
exit=1
$ cmp /tmp/opencode/eq4q/main-s5-st30.nds /tmp/opencode/eq4q/branch-s5-st30.nds; echo exit=$?
exit=0
$ cmp /tmp/opencode/eq4q/main-cube.nds /tmp/opencode/eq4q/branch-cube.nds; echo exit=$?
exit=0
$ cmp /tmp/opencode/eq4q/main-s1.nds /tmp/opencode/eq4q/branch-s1.nds; echo exit=$?
exit=0
$ cmp /tmp/opencode/eq4q/main-s2.nds /tmp/opencode/eq4q/branch-s2.nds; echo exit=$?
exit=0
$ cmp /tmp/opencode/eq4q/main-s3.nds /tmp/opencode/eq4q/branch-s3.nds; echo exit=$?
exit=0
$ cmp /tmp/opencode/eq4q/main-s4.nds /tmp/opencode/eq4q/branch-s4.nds; echo exit=$?
exit=0
$ cmp /tmp/opencode/eq4q/main-c2.nds /tmp/opencode/eq4q/branch-c2.nds; echo exit=$?
exit=0
```

The `STATIC_ANGLE=30` and `STATIC_ANGLE=315` ROMs differ (exit 1), while the
main-vs-branch `SCENE=5 STATIC_ANGLE=30` pair and all six default pairs are
byte-identical (exit 0).

---

## 5. Tests

FACT — `make test-arm9v2-static` on the branch (now 14 tests):

```
$ make test-arm9v2-static 2>&1 | sed "s|$HOME|~|g"
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_arm9v2_static_angle.py'
..............
----------------------------------------------------------------------
Ran 14 tests in 1.071s

OK
```

FACT — the suite is still wired into the aggregate `make test` target
(`Makefile:655`) and the full aggregate suite passes:

```
$ make test 2>&1 | tail -n 40 | sed "s|$HOME|~|g"
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/ds_alpha_test -p 'test_*.py'
...............................................................
----------------------------------------------------------------------
Ran 64 tests in 2.442s

OK
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/ds_edge_test -p 'test_*.py'
.......................................
----------------------------------------------------------------------
Ran 37 tests in 1.032s

OK
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/eq4o_hw_rows -p 'test_*.py'
...............
----------------------------------------------------------------------
Ran 13 tests in 0.796s

OK
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_azahar_presentation.py'
.......
----------------------------------------------------------------------
Ran 7 tests in 0.112s

OK
EQ2_DISCOVER_SKIP=1 EQ3_DISCOVER_SKIP=1 EQ_SCENES_DISCOVER_SKIP=1 \
ARM9V2_STATIC_DISCOVER_SKIP=1 EQ4_HW_COMPARE_DISCOVER_SKIP=1 \
FIT_MAPPING_DISCOVER_SKIP=1 AZAHAR_PRESENTATION_DISCOVER_SKIP=1 \
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
.....sssssssssssssssssssss..........................................................................................................usage: compare_texture_capture.py <captured.bin> [angle]
usage: compare_texture_capture.py <captured.bin> [angle]
error: angle must be an integer
........ssssssssssssssssssssssssssss...ssssssssssssssssssssssssssssssssssssss............................................................................................................................................................................................................
----------------------------------------------------------------------
Ran 412 tests in 83.848s

OK (skipped=87)
/tmp/tmphyo5kota/twlhd_frame_v2_tex_over.bin
vertex_count=36
max_clip_deviation=0.007324
colour_exactness=PASS
```

---

## 6. Owner console-run plan (PLAN, not run here)

Goal: for each of the six newly-covered static variants, capture one or more
frames on a DS console and compare with a reference picture using the existing
EQ-4 toolchain.

### 6.1 ROMs to run

Build plain static ROMs (no `AUTODUMP_FRAME`) on the branch:

| Scene / variant | Build command | ROM file name |
|---|---|---|
| cube baseline | `make -C arm9v2 STATIC_ANGLE=30` | `twlhd-arm9v2-st30.nds` |
| scene1 tri-list | `make -C arm9v2 SCENE=1 STATIC_ANGLE=30` | `twlhd-arm9v2-s1-st30.nds` |
| scene2 tri-strip | `make -C arm9v2 SCENE=2 STATIC_ANGLE=30` | `twlhd-arm9v2-s2-st30.nds` |
| scene3 quad-strip | `make -C arm9v2 SCENE=3 STATIC_ANGLE=30` | `twlhd-arm9v2-s3-st30.nds` |
| scene4 alpha quads | `make -C arm9v2 SCENE=4 STATIC_ANGLE=30` | `twlhd-arm9v2-s4-st30.nds` |
| cubes variant | `make -C arm9v2 CUBES=2 STATIC_ANGLE=30` | `twlhd-arm9v2-c2-st30.nds` |

### 6.2 Console procedure

HYPOTHESIS / PLAN (copied from `docs/EQ4_HW_COMPARE.md` §7 and adapted):

1. Copy the `.nds` to `/roms/nds/` on the SD card with the owner's existing
   FTP/SD flow.
2. Start the ROM from TWiLight Menu++ in **DS mode** and let the static frame
   display.
3. Capture with the cc3dsfs board using the three flags measured for the
   scene5 static run:
   `--enabled_both 0 --enabled_top 1 --scaling_top 1`
   (`docs/EQ4_HW_COMPARE.md` §7 item 3; `docs/reports/eq4d-ds-vertex-colour-hw.md`
   section 2).
4. Decode with `python3 tools/cc3dsfs_capture/xwd2png.py ...` and crop the top
   400×240 region.
5. Compare with `python3 tools/eq4_hw_compare.py --capture <top400x240.png>
   --reference <ref.ppm>`.

### 6.3 Reference pictures

For SCENE=5 the committed harness PNG fixtures already exist
(`tools/fixtures/eq_scenes/harness_s5_st{0,30}_f240.png`).

For the new scenes the harness reference must be generated **per scene** before
comparison:

```sh
make -C arm9v2 SCENE=N STATIC_ANGLE=30 AUTODUMP_FRAME=240
$HARNESS_BIN arm9v2/twlhd-arm9v2-autodump-sN-st30.nds 242 --out harness.txt --ppm harness.ppm
```

Then use `harness.ppm` as `--reference`. This is a PLAN/UNVERIFIED step; only
the SCENE=5 static references exist as committed fixtures.

---

## 7. UNVERIFIED / what this task does not prove

- **No ROM was run on hardware.** Every hash is a build artifact only.
- **Static frame equals animated frame at that angle only by construction.** The
  static builds use the same binary-angle formula the animated build would use
  for that degree value, but the animated counter advances by 256 per frame
  (`main.cpp:263`). For `STATIC_ANGLE=30` the binary value is 2730, which is
  not a multiple of 256, so the animated ROM never displays exactly that angle.
  Equality is therefore a consequence of the shared conversion formula, not an
  observed frame match.
- **No static harness references exist for s1-s4/c2.** They must be generated
  before a hardware comparison (section 6.3).
- **DIAGSCREEN=6 remains a counter-driven float path** and is not covered by
  `STATIC_ANGLE`.
- **Other angles/consoles/capture sessions** are not verified.

---

## 8. Diff budget

FACT — branch HEAD and main HEAD used for the base worktree:

```
$ git rev-parse HEAD
31d8020018d3eedd210753ed598c4618390b1280
$ git rev-parse main
d21d18953e4452a52a85afc668f5b858a517b73e
$ git add -N . && git diff main | wc -c
54618
```

The diff is below the 90 000-byte limit.  No unredacted home path appears in
the diff.


