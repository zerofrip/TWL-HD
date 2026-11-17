# EQ-2b — geometry equality check: our interpreter vs a DeSmuME dump

Stage EQ-2b of `docs/EQUALITY_CHECK_DESIGN.md` section 4 ("EQ-2", lines 396-423)
and section 5.2 (lines 601-630). Owner approved 2026-11-17.

`tools/eq2_compare.py` compares, per polygon and per vertex, the DS screen-space
geometry our GX interpreter produces for one frame with the geometry the
DeSmuME-based, out-of-tree GPL harness
(`/home/one/3ds/twlhd-desmume-harness`) dumped for the same frame.

- **Harness numbers: EMULATOR** (DeSmuME `fc504ee0`). Nothing here is
  hardware-verified.
- **Our-side numbers and the comparison: UNIT** (host arithmetic).
- **Licence posture:** this repository is Apache-2.0. No harness or DeSmuME
  source, code or repository content was copied here. The harness's documented
  text format was read and re-implemented in our own words; the committed test
  fixture `tools/fixtures/eq2/harness_cube_f240.txt` is *data* produced by the
  GPL harness run (see its README), not code.
- **Scope:** host files only. No console, SD-card or network action.

## 1. Our-side interchange format (`twlhd-eq2-clip 1`)

Produced by:

```sh
tests/host/build/gx_interp_cli --eq2 <frame_v2.bin>
```

The format is integer-only and exactly reversible: every float field is the
IEEE-754 **binary32 bit pattern as 8 uppercase hex digits**. The interpreter
computes in `float`, so a decimal or fixed-point re-encoding would either round
or duplicate a conversion the comparison tool must own. The existing default
`gx_interp_cli` output (`%.6f`) is unchanged; `--eq2` is an additive flag.

```
twlhd-eq2-clip 1
frame <recorder-frame-index>
viewport <x1> <y1> <x2> <y2> <present>
v <i> <cx> <cy> <cz> <cw> <cr> <cg> <cb> <ca> <s> <t> <texparam> <pltt>
...
stats commands=... vertices_in=... triangles_out=... quads_split=... ...
```

| Field | Encoding | Unit |
|---|---|---|
| `frame` | decimal | v2 recorder frame index (1-based; 241 for ROM frame 240, see section 3) |
| `viewport` | five decimals | DS viewport `x1,y1,x2,y2` (inclusive 0..255 / 0..191) and `present` (0/1) |
| `v <i>` | decimal | flat output-vertex index, 0-based, stream order |
| `cx..cw` | 8 hex digits | DS clip space (x, y, z, w); no divide, no clipping; unitless per section 5.2 |
| `cr..ca` | 8 hex digits | RGBA as `k/31` for the colour channels (k = the DS 5-bit source) and `(a+1)/32` for alpha (`common/twlhd_gx_interp.c:395-409`) |
| `s,t` | 8 hex digits | latched texture coordinate in texels, s1.11.4 (`common/twlhd_gx_interp.c`, `--tex` semantics) |
| `texparam`, `pltt` | decimal | raw `TEXIMAGE_PARAM` / `PLTT_BASE` words at the vertex; not used by the comparison |
| `stats ...` | `key=value` | the interpreter's existing stats line |

The conversion from these bits is `struct.unpack('<f', struct.pack('<I', bits))`.
`texparam = 0` means untextured.

## 2. Harness format and the DeSmuME-to-ours conversion

The harness emits one frame per file, format `geometry-dump 1`. Fields used here
(documented in that repository's `README.md`):

| Harness field | Unit | Used for |
|---|---|---|
| `captured-frame`, `requested-frame` | decimal frame index | frame selection |
| `poly<N> ... verts <n> ... prim <triangle\|quad> ... rawpoly <r>` | n = 3 or 4 | polygon mapping |
| `viewport <x> <y> <w> <h>` | DS pixels | our clip-to-screen viewport |
| `pos-x-16.16`, `pos-y-16.16` | signed 16.16 screen px, y down | x, y comparison |
| `pos-w-20.12` | signed 20.12 clip w | w comparison |
| `pos-z-0.31` | unsigned 0.31 depth, clamped to `0..0x7FFFFFFF` | z comparison |
| `tex-s-12.4`, `tex-t-12.4` | signed 12.4 texels, post texture matrix | reported, not gated |
| `col-r-6bit`, `col-g-6bit`, `col-b-6bit` | 0..63, 5-bit source expanded by `GFX3D_5TO6` | colour comparison |
| `col-a-u8` | always 0 in this core | **not compared** |

### 2.1 Screen-space conversion

`_float_to_screen` transcribes `gfx3d.cpp:3287-3324` in integer arithmetic:

```
xq,yq,zq,wq = round(clip * 4096)                 # our float -> 20.12
sx = ((xq + wq) * (width  << 16)) / (2 * wq)     # C truncation toward zero
sy = ((yq + wq) * (height << 16)) / (2 * wq)
sz = ((zq + wq) * (1     << 31)) / (2 * wq)
sx += x1 << 16
sy += y1 << 16 ; sy = (192 << 16) - sy           # DS y-down flip
sz = clamp(sz, 0, 0x7FFFFFFF)
w  = wq                                          # 20.12
```

`wScalar = hScalar = 1`: the harness selects the native soft rasteriser, whose
framebuffer is 256x192 (`gfx3d.cpp:3253-3254`, README "How the geometry list is
captured"). The viewport used is the **harness polygon's** latched viewport; the
tool also requires our recorded viewport to equal it and fails with a count
mismatch (exit 1) otherwise.

### 2.2 Polygon and vertex mapping

Our interpreter emits a **flat triangle list in submission order**
(`common/twlhd_gx_interp.c:567-710`): a quad list produces `(0,1,2)` then
`(0,2,3)`, so the four quad corners are `triA[0], triA[1], triA[2], triB[2]`.

The harness list is **Y-sorted by the core**, so its `poly 0` is not necessarily
the first submitted polygon (README). `rawpoly` is the index into the submitted
list. The tool therefore:

1. sorts the harness polygons by `rawpoly`;
2. requires those indices to be exactly `0..P-1` (otherwise exit 1);
3. consumes our triangles in that order: `verts 4` consumes two triangles,
   `verts 3` consumes one;
4. requires the triangles to be consumed exactly (otherwise exit 1).

Vertex order inside a polygon: the DeSmuME clipper starts its output at a
rotated vertex (README: "match clipped vertices to raw indices by coordinates,
not by position"), so the tool selects the **cyclic rotation** that best matches
the quad/triangle corners and compares in that rotation. This keeps winding
meaningful: a reversed assembly does not match any rotation and is reported as a
large deviation. Clipped polygons with more than four vertices are rejected
(exit 1); our interpreter never clips, so the tool is only meaningful for
unclipped frames such as the cube.

## 3. Frame alignment

Three counters are in play (harness README "Frame alignment for EQ-2b", measured
there, not assumed):

| Counter | Value for the default-cube run |
|---|---|
| ROM frame counter (`AUTODUMP_FRAME=240`, `Makefile:473`) | 240 |
| v2 recorder `frame_index` (1-based) | 241 (our `--eq2` prints `frame 241`) |
| DeSmuME/harness frame (`currFrameCounter`) | 242 |

libnds' boot consumes two VBlanks before the ROM's first draw reaches the GPU,
so the ROM's counter `k` is harness frame `k + 2`. The harness sample's own
header says `requested-frame 242` / `captured-frame 242`, which confirms the
`+2` offset for this ROM. For a different ROM the offset must be re-derived
(harness README "OPEN ITEMS"); the tool only enforces that the requested harness
frame is the one in the dump.

Selection: each file is single-frame. `--harness-frame N` (default **242**)
requires both `requested-frame` and `captured-frame` to equal `N`; otherwise the
tool exits 2. Our side is the single v2 capture passed to `gx_interp_cli`.

## 4. Tolerances (cited, not invented)

All references are `docs/EQUALITY_CHECK_DESIGN.md` section 5.2 (starts line 601).

| Field | Tolerance | Citation |
|---|---|---|
| x, y | **65536** units = 1 DS px (16.16) | lines 610-613 ("1 DS px is a safe screen-space tolerance") |
| w | **16 quanta** = 0.004 clip units (20.12) | lines 603-609, "reused rather than re-derived" |
| z | 0.004 clip units propagated per vertex: `floor(0.004 * 2^31 / (2*|w|))` in 0.31 units | lines 616-623: compare z only after the same conversion |
| r, g, b | **+/-1 5-bit level** = **+/-2 LSB** in 6-bit | lines 628-630 (Run-18 agreement); our 5-bit `k` expands as `round(k*63/31)`, which covers both plausible 5->6-bit expansions |
| texture s, t | **none — reported, never gated** | section 5.2 derives no tex tolerance |
| alpha | **not compared** | harness has 6-bit colour without alpha; blend convention UNVERIFIED (section 5.1(ii)) |

z tolerance derivation: `z_screen = ((z + w) / (2w)) * 2^31`, so
`dz_screen/dz = 2^31 / (2w)`; the 0.004 clip-component bound therefore corresponds
to `floor(0.004 * 2^31 / (2*|w|))` in 0.31 units at that vertex's `w`. For the
cube (`w` 3.29..5.71) this is about 0.75-1.3 million units.

Truncated interpreter output never looks like equality: non-zero
`capacity_dropped`, `geometry_incomplete` or `dropped_incomplete` in the stats
line is an explicit mismatch (exit 1).

## 5. Exit codes and output

| Code | Meaning |
|---|---|
| 0 | every compared field within tolerance and the counts consumed exactly |
| 1 | any mismatch: worst deviation per field with polygon and vertex index, or a count/mapping reason |
| 2 | usage or format error (bad arguments, unreadable file, malformed dump, missing selected frame) |

Missing polygons/vertices, an unconsumed tail, a non-contiguous `rawpoly` set, a
viewport disagreement and truncated interpreter stats are exit 1, never a
silent pass. Reported indices name the harness `poly i` (clipped-list index), its
`rawpoly`, and the vertex index within that polygon plus our global output vertex.

## 6. Tests and mutant proof

`tools/test_eq2_compare.py` is picked up by `make test`
(`python3 -B -I -m unittest discover -s tools`). It covers:

- committed positive pair -> exit 0;
- first `pos-x-16.16` +75536 (beyond 1 px) -> exit 1 with polygon/vertex indices;
- +32768 (inside 1 px) -> exit 0 (pins the tolerance from both sides);
- one vertex block deleted (declared count kept) -> exit 1;
- last polygon block dropped -> exit 1 (polygon count);
- bad magic line and a bad field count -> exit 2;
- `--harness-frame 999` -> exit 2;
- two in-process mutants: relaxing `TOL_XY_16_16` makes the beyond-tolerance
  case wrongly pass; setting `ENFORCE_COUNTS = False` makes the dropped-polygon
  case wrongly pass. Each mutant test first asserts the unmodified tool returns
  1, so the mutation is what flips the result.

## 7. Real run (EMULATOR, 2026-11-17)

- Harness reference: `samples/cube_f240.txt` from the GPL harness run
  (DeSmuME `fc504ee0`, ROM SHA-256
  `7dc3f1cf933637a6eff0fe17292b7b66357793ebe962f02ecf84ed0d96cd0db8`,
  harness frame 242 = ROM frame 240). Committed verbatim as
  `tools/fixtures/eq2/harness_cube_f240.txt`
  (sha256 `e59254fe01b1d1229db94dab2d1534117fec0e672c58462dfe16edc3436a15c9`).
- Our input: `tests/host/fixtures/demo_frame_desmume_v2.bin`
  (DeSmuME capture, angle 61440, recorder frame 241,
  sha256 `acbdb48e9c0f5a5159ca58bc116d3d863fb822c0a3a79b1ad55aff0ac7934fa5`,
  see `docs/EMULATOR_VALIDATION.md:22`). The `--eq2` output is committed as
  `tools/fixtures/eq2/ours_cube_f240.txt`
  (sha256 `ccb6e61cf8f911ff652abdc67b010a15c7b4bd3d9553a08692651c43170f4c67`);
  36 clip vertices, 12 triangles, 6 quads, no dropped capacity or incomplete
  geometry.
- Command:
  `python3 tools/eq2_compare.py tools/fixtures/eq2/harness_cube_f240.txt tools/fixtures/eq2/ours_cube_f240.txt`
- Result: **exit 0 — all six quads / 24 dumped vertices match within section
  5.2 tolerance.** Worst deviations (EMULATOR reference, UNIT arithmetic):

| Field | Worst | Tolerance | Location |
|---|---|---|---|
| x | 1244 (16.16 px) = 0.019 px | 65536 | poly 3 (rawpoly 1) vertex 2, our vertex 6 |
| y | 715 (16.16 px) = 0.011 px | 65536 | poly 3 (rawpoly 1) vertex 3, our vertex 7 |
| z | 114325 (0.31) | 1873103 | poly 1 (rawpoly 0) vertex 3, our vertex 1 |
| w | 0 (20.12 quanta) | 16 | poly 1 (rawpoly 0) vertex 0, our vertex 2 |
| r, g, b | 0 (6-bit) | 2 | poly 1 (rawpoly 0) vertex 0, our vertex 2 |
| tex s, t | 0 (12.4) | reported, not gated | poly 1 (rawpoly 0) vertex 0, our vertex 2 |

Interpretation: w and colour are exact after conversion; x/y differ by at most
0.019 px, far inside the 1 px bound. The residual is the expected DS-side
integer 4.12 transform versus our `double`/`float` transform (section 5.2's
stated risk), and it is what the 0.004/1-px tolerance exists to absorb. No
tolerance was loosened and the interpreter was not changed.

## 8. Limits

- One frame, one viewport: a frame that changes `VIEWPORT` mid-frame is not
  compared polygon by polygon beyond the harness's per-polygon latch (the tool
  uses the harness viewport but our side has one recorded viewport).
- Unclipped frames only: clipped polygons with more than four vertices are
  rejected as a count mismatch.
- Geometry only: no rasterisation, texturing, blending, fog, edge marks or
  anti-aliasing. For the same reason, lit/alpha frames are not colour-compared
  beyond the +/-1 5-bit level cited above.
- `viewport-word` wrap-around (y > 191) is not reconstructed by the harness and
  is not used.
