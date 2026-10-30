# Phase 4 — real-hardware findings (New 3DS)

Labels: **HARDWARE OBSERVED** = reported by the project owner from a real New 3DS (photos kept outside the repo);
**EMULATOR** = Tanuki3DS offscreen runs by the assistant (not hardware evidence); **BUILD/UNIT** = devkitARM build and host tests.
This document is the evidence trail for the vertex-buffer padding fix. Nothing here claims DS reconstruction fidelity: the renderer is a
fixed-debug-camera view.

## Symptom (Runs 1–2)
The original app printed its console text ending in `ready`, then the top screen stayed black and the buttons stopped responding
(forced power-off needed; the console always booted normally afterwards). The same binary rendered fine in an emulator. The official
devkitPro `simple_tri` example rendered correctly on the same console (control).

## Method
A diagnostic build (`arm11/source/diag_main.c`, built with `make arm11-diag`) prints a marker before each citro3d/PICA step and,
after each frame, polls with `C3D_FrameBegin(C3D_FRAME_NONBLOCK)` for up to 120 vblanks. It reports `OK` or `STUCK` instead of
freezing, and has runtime toggles for the suspects. Every toggle was tested on hardware; only the vertex layout mattered.

## Evidence (HARDWARE OBSERVED)
| Config | Declared bytes / stride | Result |
|---|---|---|
| skip draw (clear only) | – | OK |
| official layout (1 float3 loader + fixed colour, stride 12) | 12 / 12 | OK |
| 2 loaders (float3+float4), packed stride 28 | 28 / 28 | OK |
| 2 loaders in 2 buffers (strides 12 and 16) | 12/12, 16/16 | OK |
| 1 loader, stride 48 (private copy) | 12 / 48 | frame completes, vertices misread (distorted polygon) |
| 2 loaders, stride 48 (our original layout) | 28 / 48 | GPU never finishes the frame (STUCK) |
| 2 loaders, stride 48, **padding declared** (`BufInfo_Add(buf,48,4,0xCF10)`) | 48 / 48 | OK, correct triangle; same session without padding: STUCK |
| production app with padding: built-in triangle, 36-vertex cube, 900-vertex frame | 48 / 48 | OK, responsive |
| stride-48 layout with padding, 3000 vertices (~144 KB) | 48 / 48 | OK; same with padding disabled: STUCK |
Ruled out on hardware: projection/camera, depth/cull/texenv state calls, uniform upload, per-frame program bind, cache flush
(a `GSPGPU_FlushDataCache` of the vertex buffer is kept but is not the fix).

## Root cause and fix
A PICA200 attribute buffer lists *components* (attributes and padding); the sum of the declared component bytes must equal the buffer
stride. Our `TwlhdVertex` is 48 bytes but only 28 bytes (float3 position + float4 colour) were declared, leaving a 20-byte gap.
Real hardware then steps through the buffer incorrectly (misread vertices with one loader, a GPU hang with two). Emulators use the stride
register directly and never show the problem. citro3d has no padding API, but `BufInfo_Add`'s `permutation` nibbles 0xC/0xD/0xE/0xF
declare 4/8/12/16 bytes of padding, and the attribute count includes them.
`common/twlhd_attr_pad.{h,c}` computes the declaration (host test `tests/host/test_attr_pad.c`: 48/28 → count 4, perm `0xCF10`;
48/12 → `0xCFF0`; 128/28 → count 9, `0xCFFFFFF10`). `renderer.c` applies it to every stride-based layout; the diagnostic flag
`TWLHD_RDBG_NO_PAD` restores the old, known-hanging behaviour only for contrast.
The "declared bytes must equal stride" statement is an empirical rule derived from the observations above (and consistent with how
other emulators decode vertex buffers); it was not taken from vendor documentation.

## Not covered
Animation or long runs with changing data, textures, real DS captures, other vertex formats, and any DS-faithful reconstruction.
The production app has no GPU-completion probe; a hang there still needs a POWER-hold.
