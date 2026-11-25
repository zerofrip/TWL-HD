<!-- SPDX-License-Identifier: Apache-2.0 -->
# eq4w_blend_hw — blend-enabled scene-4 hardware fixture and measurement

Fixtures and a deterministic analyser for the EQ-4w real-hardware run that tests
the EQ-4u §6.3 / EQ-4v hypothesis: the opaque alpha-16 and alpha-4 quads in the
non-blend scene-4 capture are caused by the demo never enabling `GL_BLEND`.
Result and full evidence: `docs/reports/eq4w-blend-hw-result.md`.

**Scope of the evidence:** one New 3DS console, one capture session (three
captures), one ROM (`twlhd-arm9v2-s4-bl-st30.nds`), one scene (`SCENE=4`,
`STATIC_ANGLE=30`, DS mode). No claim here generalises to other consoles,
sessions, angles or textured geometry.

## 1. Files

| Path | Purpose |
|---|---|
| `hw_s4bl_a.png` | blend capture `a` (comparison capture by the a-priori EQ-4r rule) |
| `hw_s4bl_b.png` | blend capture `b` (used only for the a/b noise pixel count) |
| `measure_blend_hw.py` | quad-rectangle derivation, per-channel majority, blend classification, alpha-0 census |
| `test_measure_blend_hw.py` | synthetic positive/negative, fixture hashes and FACTs |
| `README.md` | this file |

Both PNGs are the owner's top-left 400x240 crops of a 1280x720 root capture;
the capture mapping is the shared one (`scale 1.25`, `offset x=40`). Each file
is under 100 KB. Capture `c` is not committed (only `a` and the a/b difference
are required); the owner reports `a` and `c` byte-identical and that the noise
origin is UNVERIFIED.

## 2. Fixture provenance and sha256

Both files are byte-identical copies of the owner's staging files
(`<staging>/hw/hw_s4bl_a.png`, `<staging>/hw/hw_s4bl_b.png`). In-repo hashes:

```
8b4538fe5da92ef27176d420473ec0fa31dae3a54f749b2128fbde8e20e8eb01  hw_s4bl_a.png
77d537869e0316e2eccfa931e7cb4d2636b19adbe6ab0a5c38e188047c87a9e6  hw_s4bl_b.png
```

`test_measure_blend_hw.py` re-checks both hashes, so a changed fixture fails the
suite. The non-blend comparison fixture is
`tools/fixtures/eq4_hw_scenes/s4_a_top400x240.png` (EQ-4r, sha
`ecb998b2…`); it is not copied here.

## 3. Usage

```
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py tools/eq4w_blend_hw/hw_s4bl_a.png
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py --diff tools/eq4w_blend_hw/hw_s4bl_b.png tools/eq4w_blend_hw/hw_s4bl_a.png
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py tools/fixtures/eq4_hw_scenes/s4_a_top400x240.png
```

Exit 0 on success, 2 on usage, PIL or input errors. `--diff OTHER.png` adds the
differing-pixel count between the two captures.

## 4. What is measured (no blend model)

The four quad rectangles are derived from the scene-4 source vertices
(`arm9v2/source/demo_frame.cpp:250-259`) through the demo transform chain
(`:350-383`): `clip = P(70,256/192,0.1,100) · LookAt(0,0,4) · Rx(2730) ·
Ry(2730) · Scale(2)`, NDC = `clip.xy/clip.w`, DS screen
`x=(ndc_x+1)*128`, `y_from_bottom=(ndc_y+1)*96`, capture
`x = 40 + 1.25*ds_x`, `y = 1.25*(192 - y_from_bottom)`. The projection helpers
are imported from `tools/gx_interp_oracle.py`; the mapping is read from
`tools/ds_vtxcolor_test/capture_mapping.json`. The tool prints the derived
floating-point rectangle for every quad.

Because the rotated quads project to trapezoids, the region is their axis-
aligned bounding box shrunk by 2 capture pixels per side ("interior"); the
per-channel majority (mode) over the interior is the measured value. A quad is
`blended=yes` when its source channel's majority is below the source value 255,
else `blended=no` — no DS blend equation is assumed.

Quad 3 (alpha 0, white source) has no classification. Its derived region is
censused with the EQ-4r bright rule `max(r,g,b) > 60`; `bright_pixels`,
`bright_bbox`, `region_pixels` and the shrunk interior's bright count are
printed.

The top 16 of the full colour census are printed as `census=(r, g, b) count`
(ordered by count descending, then RGB ascending), reproducing the owner-side
census.

## 5. Tests

```
$ make test-eq4w-blend-hw
```

Synthetic positive: dimmed alpha-16 green `(0,134,0)` and alpha-4 blue
`(0,0,33)` classify `blended=yes`. Negative: a full `(0,255,0)` alpha-16 quad
classifies `blended=no`. The real-fixture tests pin both committed hashes, the
blend capture's `G=134` / `B=33` majorities, the non-blend capture's
`(0,255,0)` / `(0,0,255)` majorities, both alpha-0 bright censuses, and the
a/b difference of 93 pixels. The target is wired into `make test` like
`tools/eq4o_hw_rows`.
