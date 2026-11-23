<!-- SPDX-License-Identifier: Apache-2.0 -->
# melonDS scene5 cross-check — does melonDS agree with DeSmuME and the real DS?

**Scope:** INVESTIGATION ONLY. No product code, test, interpreter, renderer, ROM,
harness, melonDS or DeSmuME change; the only file added to this repository is
this report. The melonDS checkout was built **out of tree** under
`$HOME/3ds/hwtest-staging/melonds-build/` (scratch, not committed) and a
small GPL-labelled driver was written **there**, not in this repo. No console,
SD-card or network action was performed, and no system package was installed.
Every statement about the real DS remains a **HYPOTHESIS** or **UNVERIFIED**
(section 3). This report follows `docs/reports/scene5-colour-diagnosis.md`
(same labels, same question) and only adds the third emulator.

**Labels.** FACT = read at the cited `file:line` in this session, or a command
output quoted below. HYPOTHESIS = an inference from facts, not directly read or
run. UNVERIFIED = no local source exists / it was not run. Emulator facts are
facts about the *model*, never about hardware.

**Evidence paths.** melonDS line numbers are from the pinned checkout
`$HOME/3ds/melonDS` at HEAD `1e654443e4c0a54b40709c0208aa0eeba41e6142`
(`git status --porcelain` empty before and after; a scratch **copy** was used for
the one control in section 2.5, never the checkout). DeSmuME line numbers are
from the pinned checkout `$HOME/3ds/desmume` at
`release_0_9_13-429-gfc504ee0`, which stores the geometry engine at
`desmume/src/gfx3d.cpp` (no `src/3D/`). The ROM run is
`$HOME/3ds/hwtest-staging/run23/s5cull-plain.nds`, sha256
`1bde37007ab36c05b0457874dce56e633670aedf2c66e840b0c5e219fe8afd01`
(verified in this session, matching the prefix the owner supplied).

---

## 1. melonDS's current-vertex-colour state (Q1)

### 1.1 Reset / initial value: **black (0,0,0)**

**FACT (EMULATOR source): melonDS's geometry engine resets the current colour to
zero, not to white.**

- `GPU::Reset()` calls `GPU3D.Reset()` (`src/GPU.cpp:190`).
- `GPU3D::Reset()` (`src/GPU3D.cpp:182`) clears the current-vertex state:
  `memset(CurVertex, 0, sizeof(CurVertex));` (`:257`) and
  `memset(VertexColor, 0, sizeof(VertexColor));` (`:258`).
- The declarations are zero-initialised members: `s16 CurVertex[3] {};` and
  `u8 VertexColor[3] {};` (`src/GPU3D.h:272-273`).
- melonDS itself flags this value as unchecked against hardware: the block opens
  with `// TODO: confirm initial polyid/color/fog values` (`:242`), immediately
  above the clear-colour/poly-ID fogs and the vertex colour clear.

There is no alpha component at all: the vertex colour is a 3-component 5-bit RGB
(`GPU3D.h:273`); the polygon alpha comes from `POLYGON_ATTR` (section 1.4).

### 1.2 Where it becomes a vertex's colour, and the 5→6-bit formula

**FACT (EMULATOR source).** Every submitted vertex copies the latched colour in
`GPU3D::SubmitVertex()` (`src/GPU3D.cpp:1340`):

```
vertextrans->Color[0] = (VertexColor[0] << 12) + 0xFFF;   // :1353
vertextrans->Color[1] = (VertexColor[1] << 12) + 0xFFF;   // :1354
vertextrans->Color[2] = (VertexColor[2] << 12) + 0xFFF;   // :1355
```

and the polygon store converts that to the interpolated 9-bit final colour
(`src/GPU3D.cpp:1244-1249`):

```
vtx->FinalColor[0] = vtx->Color[0] >> 12;
if (vtx->FinalColor[0]) vtx->FinalColor[0] = ((vtx->FinalColor[0] << 4) + 0xF);
```

Because `0xFFF < 0x1000`, `Color >> 12` recovers the 5-bit value exactly, so the
map is `0 → 0` and `v ≠ 0 → 16v + 15` in a 9-bit space. The pixel stage then
reduces it with `>>3` — `RenderPixel(polygon, vr>>3, vg>>3, vb>>3, s, t)`
(`src/GPU3D_Soft.cpp:1157`, also `:1253`, `:1345`), which gives
`(16v + 15) >> 3 = 2v + 1` for `v ≥ 1`.

**So melonDS's effective 5→6-bit formula is exactly `0 → 0`, `v → 2v + 1`** —
the same formula DeSmuME uses (table in 1.5). The two emulators **agree on the
conversion and disagree only on the initial value**.

**FACT (measured in the run, section 2):** with the stock (zero) reset value the
emulator reports `color=4095,4095,4095` = `(0<<12)+0xFFF` and
`finalcol=0,0,0`; with the white control (`VertexColor=31`) it reports
`color=131071` and `finalcol=511`, i.e. `255>>3 = 63` at the pixel stage —
numerically identical to DeSmuME's `63,63,63`.

### 1.3 Commands that change the current colour

**FACT (EMULATOR source): only the same three commands as DeSmuME.**

| command | melonDS handler | effect |
|---|---|---|
| `0x20` COLOR | `GPU3D.cpp:1892-1903` (`case 0x20: // vertex color`, `VertexColor[0] = r;` at `:1899-1901`) | latches the 5-bit RGB |
| `0x21` NORMAL | `GPU3D.cpp:1905-1912` → `CalculateLighting()` (`:1438`), result stored at `:1522-1524` | lighting result replaces the colour |
| `0x30` DIF_AMB, bit 15 | `GPU3D.cpp:1981-1994` (`if (entry.Param & 0x8000)` at `:1989`, `VertexColor[0] = MatDiffuse[0];` at `:1991`) | copies diffuse material into the colour |

The scene5 ROM issues **none** of the three (`docs/reports/scene5-colour-diagnosis.md:129-131`,
`:144-161`), so melonDS's colour for the whole run is the reset value.

### 1.4 Pixel stage: MODULATE when texture sampling is on

**FACT (EMULATOR source).** The texture path is entered only when 3D texturing
is on and the polygon's texture format is non-zero —
`if ((GPU3D.RenderDispCnt & (1<<0)) && (((polygon->TexParam >> 26) & 0x7) != 0))`
(`src/GPU3D_Soft.cpp:489`). Texels are expanded 5→6 bit with the same rule
(`tr = (tcolor << 1) & 0x3E; if (tr) tr++;`, `:496-498`), and MODULATE is
(`src/GPU3D_Soft.cpp:526-531`):

```
r = ((tr+1) * (vr+1) - 1) >> 6;
g = ((tg+1) * (vg+1) - 1) >> 6;
b = ((tb+1) * (vb+1) - 1) >> 6;
a = ((talpha+1) * (polyalpha+1) - 1) >> 5;
```

`vr/vg/vb` are the interpolated vertex colours after `>>3`. With `vr = 0` the
RGB result is `((tr+1)*1 - 1) >> 6 = tr >> 6 = 0` for every texel: **a zero
vertex colour zeroes the whole textured quad.**

Note the alpha asymmetry (out of scope, `scene5-colour-diagnosis.md:342-346`):
melonDS has no vertex alpha and modulates by `POLYGON_ATTR` alpha (`:531`,
`polyalpha` read at `:459`), which is 31 here, so melonDS's quad stays opaque.

### 1.5 melonDS vs DeSmuME

| item | melonDS `1e654443` | DeSmuME `fc504ee0` |
|---|---|---|
| reset / initial current colour | **black 0,0,0** (FACT) — `GPU3D.cpp:258`, members `{}` at `GPU3D.h:273`, own TODO at `:242` | **white 31,31,31 → 63,63,63** (FACT) — `gfx3d.cpp:553-563` (`_vtxColor15 = 0x7FFF`, `_vtxColor555X = {31,31,31,0}`, `_vtxColor666X = {63,63,63,0}`) |
| where it becomes a vertex colour | `SubmitVertex()` `GPU3D.cpp:1353-1355`, then `FinalColor` `:1244-1249` | conversion `gfx3d.cpp:1568-1575`, copy `vtx.color = this->_vtxColor666X;` `:1631` |
| commands that change it | `0x20` `:1892-1903`; `0x21` `:1905-1912` → `:1522-1524`; `0x30` bit 15 `:1981-1994` | COLOR `:2620` → `:1352-1363`; NORMAL `:1324`; DIF_AMB `:1158-1167` |
| 5→6-bit formula | `0 → 0`, `v → 2v+1` (via `+0xFFF` `:1353`, `>>12`+`<<4|0xF` `:1244-1249`, `>>3` at `GPU3D_Soft.cpp:1157`) | `0 → 0`, `v → 2v+1` (`GFX3D_5TO6(x) ((x)?(((x)<<1)+1):0)` `gfx3d.h:73`; table `colorspacehandler.cpp:100-104`) |
| MODULATE, texture sampling ON | `((t+1)*(v+1)-1) >> 6` `GPU3D_Soft.cpp:528-530` | `modulate_table[i][j] = ((i+1)*(j+1)-1) >> 6` `rasterize.cpp:2080`, applied `:580-582` |
| white source when sampling is OFF | not applicable here (sampling is ON) | synthetic `colorWhite = {0x3F,0x3F,0x3F,0x1F}` `rasterize.cpp:571-573` |

**Answer (Q1, FACT for both models): melonDS's initial current vertex colour is
BLACK (`0,0,0`), DeSmuME's is WHITE (63).** The 5→6 conversion and the MODULATE
rule are identical in the two emulators, so with scene5's command stream (no
`COLOR`/`NORMAL`/`DIF_AMB`) the difference is entirely the reset value: DeSmuME
multiplies the texels by white (picture = palette colours), melonDS multiplies
them by zero (picture = black).

---

## 2. Headless run of the scene5 ROM (Q2)

### 2.1 Build (out of tree, no packages installed)

**FACT (commands run in this session).**

```sh
cmake -S $HOME/3ds/melonDS -B $HOME/3ds/hwtest-staging/melonds-build \
      -DBUILD_QT_SDL=OFF -DENABLE_OGLRENDERER=OFF -DENABLE_GDBSTUB=OFF \
      -DCMAKE_BUILD_TYPE=Release
cmake --build $HOME/3ds/hwtest-staging/melonds-build --target core -j16
```

- `BUILD_QT_SDL=OFF` (`CMakeLists.txt:106-111`) builds only the `core` static
  library (`src/CMakeLists.txt:5`), no Qt/SDL.
- Configuration succeeded **with no missing system packages** (gcc 15.2.0,
  cmake 4.2.3, 16 cores); `libpcap`, `libarchive`, `sdl2`, `liblzma` are present
  and `zstd` is not, but it is not needed by `core` (it linked without
  `-lzstd`).
- Driver (scratch, GPL-3.0-or-later labelled, **not** in this repo and **not**
  in the checkout): `$HOME/3ds/hwtest-staging/melonds-build/scene5_driver.cpp`
  plus `platform_stubs.cpp` (the `melonDS::Platform` functions the core expects
  from a frontend), linked against
  `melonds-build/src/libcore.a` and `.../teakra/src/libteakra.a` with the
  library's own definitions (`-DARCHITECTURE_x86_64=1 -DJIT_ENABLED`,
  `-fwrapv`). `NDS` was heap-allocated (it is several MB and overflows an 8 MB
  stack).
- **Software 3D renderer** (required): `NDSArgs::Renderer` defaults to `nullptr`
  (`Args.h:119`) and `GPU::SetRenderer` then instantiates `SoftRenderer`
  (`GPU.cpp:334-338`). The run therefore used `src/GPU3D_Soft.cpp`.
- **CPU core:** `NDSArgs::JIT` was set to `std::nullopt`, so the run used the
  interpreter. A JIT-enabled attempt with the same driver did **not** boot the
  ROM (the 3D engine never initialised: `3Dcnt=0000 polys=0` at frame 240); the
  cause was not investigated and is **UNVERIFIED**. This does not affect the
  colour question — `GPU3D`/`GPU3D_Soft` are identical either way.

### 2.2 Boot path used (no BIOS files exist here)

**FACT.** melonDS's default `NDSArgs` installs the bundled **FreeBIOS** images
and a generated firmware (`Args.h:88-99`; the images are built from
`freebios/src/bios_common.s` into `src/FreeBIOS_Data.h` and handed out by
`src/FreeBIOS.h:36-37`). FreeBIOS cannot boot
firmware, so `NDS::NeedsDirectBoot()` returns true (`NDS.cpp:286-289`) and the
driver called `NDS::SetupDirectBoot(rompath)` (`NDS.cpp:390`) — the same path the
Qt frontend takes (`EmuInstance.cpp:1946-1951`). **No BIOS or firmware file was
used.**

Observed warnings, quoted verbatim:

```
ROM entry not found for gamecode 589505315
!! bad ROM size 235520 (expected 262144) rounded to 262144
NeedsDirectBoot()=true, SetupDirectBoot()
Secure area decryption failed
```

**FACT:** the secure-area failure is not specific to this ROM and did not stop
it: without a BIOS `DecryptSecureArea` cannot key the block, so melonDS fills its
first `0x800` bytes with `0xE7FFDEFF` (`NDSCart.cpp:358-362`). This ROM's ARM9
entry (`0x02004800`) is exactly `ARM9RAMAddress + 0x800`, i.e. past that block —
and the stock devkitPro examples used as controls (2.5) have the identical
header layout (`0x4000`/`entry 0x02004800`/`ram 0x02004000`), take the same
warning, and render correctly. So the clobbered range is the secure-area
placeholder, not live code. **Caveat (FACT):** this run is a FreeBIOS direct
boot, not a BIOS boot; it is not byte-identical to a console boot.

### 2.3 Result: the quad is BLACK

**FACT (emulator run, artefact
`$HOME/3ds/hwtest-staging/melonds-build/s5_f240.ppm`, sha256
`1fd9d9655964fcd78aad61404b948ac0efed627174e950dfb385989ce6f07560`, 147471
bytes, 256×192 P6; byte-identical across two repeat runs).**

- Top screen: **1 distinct colour, `(0,0,0)`, all 49152 pixels**. The 3D clear
  colour is also black (`0x3F1F0000` → RGB 0,0,0, per the RSTA stream,
  `scene5-colour-diagnosis.md:165-169`).
- melonDS's own decoded state for the last frame (printed from
  `GPU3D::NumPolygons`/`CurPolygonRAM` by the driver):

```
GPU3D: NumPolygons=1 NumVertices=5
 poly0 attr=001F00C0 texparam=0C000000 texpal=00000000 nverts=5
  v0 finalcol=0,0,0 color=4095,4095,4095 texc=0,1
  v1 finalcol=0,0,0 color=4095,4095,4095 texc=7,0
  v2 finalcol=0,0,0 color=4095,4095,4095 texc=128,0
  v3 finalcol=0,0,0 color=4095,4095,4095 texc=128,128
  v4 finalcol=0,0,0 color=4095,4095,4095 texc=0,128
```

`attr=0x001F00C0` (MODULATE, cull none, alpha 31), `texparam=0x0C000000`
(I4 8×8, slot 0), `texpal=0` and texcoords 0/128 are exactly the values recorded
in the GXCM capture (`scene5-colour-diagnosis.md:143-158`), so the ROM executed
its own init and draw. Every vertex's final colour is `0,0,0`.

**Quad region:** derived from the white-init control (2.5), where the quad is
visible, as `x 90-234, y 27-191` (23925 px). Inside it the stock run has
**1 distinct colour** (`(0,0,0)`, 23925/23925 px) and 0 non-black pixels; the
control has 102 distinct colours and 4848 non-black pixels.

**Answer:** the quad's colours are **black** — neither the palette colours nor
any white-modulated texture output.

### 2.4 Interpretation caveat, stated plainly

The composited picture alone cannot separate "quad rasterised black" from "quad
not rasterised", because the 3D clear colour is black too. Two observations
separate them: (a) the emulator's own per-vertex state is `finalcol=0,0,0` with
MODULATE, which makes the rasterised result exactly `(0,0,0)` for every texel
(section 1.4); and (b) the same ROM, same driver, with only the reset colour
changed to white, draws 4848 non-black pixels in that region (2.5). **FACT:**
final colour 0. **HYPOTHESIS (strong):** the quad is rasterised and is black.

### 2.5 Controls (what makes the black attributable)

| control | what it shows | result |
|---|---|---|
| stock devkitPro `run23/ex-Textured_Quad.nds`, same driver | the headless build, direct boot, software renderer and PPM dump really do draw textured quads | **267 distinct colours**, 4761 non-black px (FACT) |
| stock devkitPro `run23/ex-Simple_Tri.nds`, same driver | same, for an untextured triangle | **4271 distinct colours** (FACT) |
| scratch **copy** of melonDS with `VertexColor[0..2] = 31` added after the reset `memset` (one line, `melonds-build/melonds-src-white/src/GPU3D.cpp`, **not** the checkout, **not** a proposed change) | the only difference from the stock run is the initial colour | `finalcol=511,511,511` (= 63 after `>>3`, matching DeSmuME's 63) and **102 distinct colours**: white 2542, blue 682, green 522, red 477, cyan 414 px in the quad region — i.e. the ROM's own palette (`arm9v2/source/demo_frame.cpp:155-160`) modulated by white (FACT; `melonds-build/s5_f240_whiteinit.ppm`, sha256 `67c48830ca88a72fa86a5417ed092bcc914242d2aa069d69301757bf2cf26a0b`) |

The control proves the ROM, its texture upload, the palette and the compositing
path are all sound in this build: **the black picture is caused only by the zero
initial vertex colour.**

---

## 3. The real DS observation (Q3 — unchanged)

**UNVERIFIED, exactly as in `scene5-colour-diagnosis.md:188-217`.** No GBATEK
text, no native BIOS and no libnds statement about the power-on colour exists
locally, and no console run was possible or permitted in this session.

**HYPOTHESIS (weak, qualitative hardware run):** the owner's New 3DS DS-mode run
of `SCENE=5` drew the quad "with the palette colours"
(`docs/TEXTURE_CAPTURE_DESIGN.md:1135-1137`), which bounds the hardware default
away from 0 but does not prove it is 31. The melonDS result therefore
**disagrees with that observation** in the same way DeSmuME would if its default
were 0 — but the hardware side stays a photo-bounded hypothesis.

---

## 4. Conclusion

CAUSE/RESULT: **melonDS does NOT agree with DeSmuME for scene5 — melonDS resets
the current vertex colour to black (`GPU3D.cpp:258`) where DeSmuME resets it to
white 63 (`gfx3d.cpp:553-563`), and with identical 5→6 conversion and MODULATE
rules it renders the quad `(0,0,0)` (1 distinct colour, FACT from the headless
run) where DeSmuME renders the palette colours — so melonDS agrees with our
interpreter's 0, and disagrees with the real-DS picture (which remains
HYPOTHESIS, bounded away from black by the owner's photo).**

## Open items

1. **The real-DS default is still UNVERIFIED.** Nothing here changes
   `scene5-colour-diagnosis.md` open item 1: the `DIAGSCREEN`-style untextured,
   uncoloured quad on hardware is the only thing that settles it. melonDS now
   votes "black", DeSmuME "white", the photo "not black".
2. **A "match DeSmuME" fix is now doubly model-bound.** The proposed one-line
   change (a) in `scene5-colour-diagnosis.md:269-293` would make our interpreter
   agree with DeSmuME *and disagree with melonDS*; hardware remains the only
   tiebreak. Not implemented here (investigation stage).
3. **Picture-level attribution of "drawn black" vs "not drawn"** rests on the
   emulator's own vertex state plus the white-init control, not on the picture
   alone (section 2.4). Both emulator artefacts are deterministic across repeat
   runs.
4. **The JIT path was not usable in this scratch harness** (`JIT = nullopt`
   used, section 2.1); the JIT-enabled attempt never initialised the 3D engine.
   Cause UNVERIFIED; it cannot affect the colour result because `GPU3D` and the
   software renderer are the same code either way.
5. **Only the software renderer was checked**, as the task required. melonDS's
   OpenGL renderer (`GPU_OpenGL.cpp`) was not built (`-DENABLE_OGL_RENDERER=OFF`)
   and its per-vertex colour handling was not read.
6. **Boot path caveat:** FreeBIOS direct boot with a failed secure-area
   decryption (section 2.2) — correct for this ROM family (controls render), but
   not a BIOS boot.
7. **melonDS's own source flags the value as unverified** against hardware
   (`GPU3D.cpp:242`), so melonDS is not evidence for the hardware default either.
8. **Alpha** remains out of scope (`scene5-colour-diagnosis.md:342-346`);
   melonDS has no vertex alpha and uses `POLYGON_ATTR` alpha
   (`GPU3D_Soft.cpp:531`), which differs from both DeSmuME's forced 0 and our
   `(a+1)/32`.

---

## Update 2026-11-23: capture decode corrected

The cc3dsfs capture colour counts and the `(255,251,255)` DS-white reading cited
earlier were taken from mis-decoded PNGs (24-bpp XWD decoded as 32-bpp BGRX).
They are not hardware evidence. The dedicated `tools/ds_vtxcolor_test` ROM,
captured through the fixed decoder, measures the initial vertex colour as white
(255,255,255): `docs/reports/eq4d-ds-vertex-colour-hw.md`. That agrees with
DeSmuME and disagrees with melonDS, on one capture and one loader state
(HYPOTHESIS, not a settled value).
