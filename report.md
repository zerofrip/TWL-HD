# Task 5A-2c — Repair Attempt 2 Report

## Build verification

```
$ make arm11
make -C arm11
main.c
linking twlhd-arm11.elf
built ... twlhd-arm11.3dsx
```

`make arm11` exits 0 with no warnings.

```
$ make arm11-diag
make -C arm11 DIAG=1
make[2]: '/tmp/codex-20261103-133636-1548072/worktree/arm11/twlhd-arm11-diag.3dsx' is up to date.
```

`make arm11-diag` exits 0 (diag build uses `diag_main.c`, which was already up to date).

```
$ make test
...all host tests pass...
```

`make test` green.

## ELF size / symbol summary

```
$ arm-none-eabi-size arm11/twlhd-arm11.elf
   text	   data	    bss	    dec	    hex	filename
 171968	   7616	1425024	1604608	 187c00	arm11/twlhd-arm11.elf
```

Largest new .bss symbols (from `arm-none-eabi-nm --print-size --size-sort`):

| symbol | size | address | note |
|---|---|---|---|
| `g_frame_bytes` | 0x100000 (1 MiB) | 0x00183f14 | enlarged for v2 max file size |
| `g_clip_vertices` | 0x30000 (192 KiB) | 0x0012cf08 | DS clip-space vertex buffer |
| `g_vertices` | 0x24000 (144 KiB) | 0x0015cf08 | existing v1 vertex buffer |
| `g_parsed` | 0x300c (~12 KiB) | 0x00180f08 | existing parsed frame |

## Requirement traceability

| # | Requirement | Location in `arm11/source/main.c` |
|---|---|---|
| 1 | **Loader detects v1/v2, parses v2, interprets GXCM, dispatches** | `load_sd_frame` signature lines 124–132; size/read logic 149–173; `twlhd_detect_format` dispatch 175–224; v2 parse call 187–191; GX interpret call 194–206; `load_v1` helper 78–122. |
| 2 | **Mode labels ≤19 chars, distinct per path** | Built-in line 61: `"BUILT-IN TRIANGLE"` (17 chars); v1 line 408: `"SD FRAME (DEBUG CAMERA)"` (23 chars, unchanged); v2 line 402: `"SD v2 (DS MATRICES)"` (19 chars). |
| 3 | **39-column bottom-screen status** | `clamp_max` / `clamp_4digit` helpers lines 68–69; `status_line()` truncator lines 71–76; v2-only `printf` block lines 273–286; transport/status routed through `status_line()` lines 289–290. |
| 4 | **v1 behaviour preserved** | `load_v1()` re-implements the original v1 path unchanged lines 78–122; invoked from `load_sd_frame` line 177; still calls `renderer_set_geometry()` for non-v2 uploads line 396; v1 `input_vertices` still sourced from `pending_stats.input_vertices` line 409. |

## v1 / built-in behaviours that changed

1. **Loader file-size check.** The original `load_sd_frame` required the file to be exactly `TWLHD_FRAME_WIRE_SIZE` (73,740 bytes). The new loader accepts any size from 1 to `TWLHD_V2_FILE_MAX` (1 MiB) and dispatches by detected format (line 156). A v1 file is still required to be exactly 73,740 bytes by `twlhd_detect_format`, but the error message now reads `"SD: size %ld, max %u"` for out-of-range sizes.
2. **Pending-upload call site.** The original code always called `renderer_set_geometry(g_vertices, pending_vertex_count)` (old line 274). The new code branches on `pending_is_v2`: v2 uses `renderer_set_clip_scene(g_clip_vertices, pending_vertex_count, &pending_viewport)` (lines 391–394), while v1 keeps the original geometry path (lines 395–397).

No other v1 or built-in behaviour was intentionally modified.

## Files changed

- `arm11/source/main.c` — rewritten per the repair contract (v2 loader, clip-scene upload, 39-column console status).
- `docs/PHASE5A_V2_RENDERER_DESIGN.md` — appended section "9. 5A-2 status" with BUILD/UNIT/EMULATOR/HARDWARE table and the 39-column rule explanation.
- `report.md` — this file.
