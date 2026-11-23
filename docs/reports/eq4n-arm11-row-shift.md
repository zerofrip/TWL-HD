<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4n — Where does the one-row (+0.8 DS px) shift come from?

**Status:** OFFLINE INVESTIGATION. No hardware action, no console access, no
production-code change (`arm9v2/`, `arm11/`, `common/`, `arm7/` untouched).
One `ds-edge-record` capture was rendered twice through the unchanged arm11
program and `tools/azahar_host`: once with the EQ-4m frame and once with the
same frame moved `+3` DS rows, each staged as `sdmc:/twlhd_frame.bin` in its own
`--sd-root` before the run, then the 400x480 PPMs were analysed. The answer
is **(B): the Azahar libretro core's software screen presentation**, not our
arm11 program (A) and not the PICA fill rule (C).

Labels: **FACT** = a command and its complete raw output quoted below;
**SOURCE-READ** = a quoted `file:line`; **HYPOTHESIS** / **UNVERIFIED** = not
measured by the quoted output.

```
basis=HEAD 0a40b6a (the work-tree HEAD); ONE linear sequence, nothing reused
      from an earlier round: one host build, one arm11 build, one
      ds-edge-record build, one DeSmuME capture, one frame-B patch, BOTH
      --sd-root trees staged before the two Azahar runs, one Azahar run per
      frame, analysis only afterwards. §1 quotes the single build of each of the
      three artefacts and its hash; those are the builds the runs used. The
      arm11 build ID embeds git HEAD
      (arm11/Makefile:75-84, arm11/source/main.c:406), so the .3dsx hash is
      HEAD-specific; every hash below comes from this one sequence.
      basis=one machine, one emulator, one capture; NO console and NO hardware
      was touched by this round (the §4 hardware procedure is a plan only).
```

**Question (EQ-4m §2.7).** Every horizontal boundary in the arm11/Azahar output
is one 400x240 capture row (0.8 DS px) lower than the DS geometry, every
vertical boundary is exact, and PPM row 0 equals row 239 shifted left one
pixel. Which stage produces the extra row — (A) the arm11 program, (B) the
Azahar host/core readback and presentation, or (C) the PICA rasteriser fill
rule?

**Answer (summary).** (B). The Azahar core's software renderer stores the
top-screen framebuffer transposed with a one-pixel column error
(`info.width` columns read as `stride-x` = 240..1 instead of 239..0), and the
libretro presentation then maps that column axis to the PPM row axis:
`PPM(ox,oy) = framebuffer[ox][240-oy]`, not `[ox][239-oy]`. That shifts every
horizontal boundary down exactly one capture row, leaves the vertical axis
exact, makes PPM row 0 the wrapped column 240 of the next framebuffer row, and
makes the last pixel of row 0 a read one pixel past the 240x400 framebuffer
(black in this run). `tools/azahar_host` itself copies rows verbatim. The PICA
rasteriser contributes only the expected integer-grid snap (`ceil(1.25*y)`,
visible as `+0.25` capture px in the `+3` DS-row frame); it does not produce
the `+1`. The arm11 target/viewport/clipfix path has no one-row offset.

## 1. Identity of this round's artefacts (FACT)

```
$ git rev-parse --short HEAD
0a40b6a
$ sha256sum tools/azahar_host/build/azahar_host arm11/twlhd-arm11.3dsx tools/ds_edge_test/ds-edge-record.nds
7e9739bc2a4ccca1d92c8584d24aa3e10e51cc935cbcaa49580d78d096be65f5  tools/azahar_host/build/azahar_host
556108e8c5b2ab67370a98634f4d597aebbde4301877cd1717a5d13ac0e28c71  arm11/twlhd-arm11.3dsx
bec5cf280e98c68c3a9109c57ce196a70ad7afd2c19e940e3a95d810586fd4ef  tools/ds_edge_test/ds-edge-record.nds
$ stat -c '%n %s bytes' "$R/cap/twlhd_frame_v2.bin" "$R/frame_y3.bin" "$R/runA_199.ppm" "$R/runB_199.ppm"
/tmp/opencode/eq4n-r2/cap/twlhd_frame_v2.bin 1392 bytes
/tmp/opencode/eq4n-r2/frame_y3.bin 1392 bytes
/tmp/opencode/eq4n-r2/runA_199.ppm 576015 bytes
/tmp/opencode/eq4n-r2/runB_199.ppm 576015 bytes
```

Builds (one build each at this HEAD; the arm11 hash is HEAD-dependent because
the build ID string carries the short HEAD):

```
$ make -B azahar-host 2>&1 | sed "s|$PWD|<worktree>|"
mkdir -p tools/azahar_host/build
cc -std=c11 -Wall -Wextra -Werror -O2 -D_POSIX_C_SOURCE=200809L \
	-Itools/azahar_host  \
	-o tools/azahar_host/build/azahar_host tools/azahar_host/azahar_host.c -ldl
$ make -C tools/ds_edge_test clean >/dev/null 2>&1; \
  make -C tools/ds_edge_test record 2>&1 | sed "s|$PWD|<worktree>|" | tail -8; \
  echo "exit=${PIPESTATUS[0]}"
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking ds-edge-record.elf
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... ds-edge-record.nds
make: Leaving directory '<worktree>/tools/ds_edge_test'
exit=0
$ make -C arm11 clean >/dev/null 2>&1; \
  make -C arm11 2>&1 | sed "s|$PWD|<worktree>|" | tail -6; \
  echo "make_exit=${PIPESTATUS[0]}"
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm11.elf
built ... twlhd-arm11.3dsx
make: Leaving directory '<worktree>/arm11'
make_exit=0
```

Every absolute work-tree path in the build output above is replaced by
`<worktree>` by the `sed` shown *in the command*; nothing else was edited.

The captured frame is byte-identical to EQ-4m's capture of record
(`sha256 1e6ef3f4…`, 1392 bytes):

```
$ DESMUME_CLI="$HOME/3ds/hwtest-staging/desmume-dbg-build5/cli/desmume-cli" \
  DESMUME_TIMEOUT=25 tools/emulator/desmume_capture.sh \
  tools/ds_edge_test/ds-edge-record.nds "$R/cap" >"$R/cap.out" 2>"$R/cap.err"
$ echo "capture_exit=$?"
capture_exit=0
$ tail -2 "$R/cap.err"
+ cp /tmp/opencode/eq4n-r2/cap/fat/twlhd_frame_v2.bin /tmp/opencode/eq4n-r2/cap/twlhd_frame_v2.bin
+ echo 'capture: /tmp/opencode/eq4n-r2/cap/twlhd_frame_v2.bin'
$ sha256sum "$R/cap/twlhd_frame_v2.bin"
1e6ef3f464738f29ba695d9373271540ed70ff0b041f0a654d0d0806c9b10dc5  /tmp/opencode/eq4n-r2/cap/twlhd_frame_v2.bin
```

The recorded viewport word is the EQ-4m `glViewport(0,0,255,191)`; the host
interpreter reports the frame and its statistics:

```
$ tests/host/build/gx_interp_cli --eq2 "$R/cap/twlhd_frame_v2.bin" | sed -n '1,6p;$p'
twlhd-eq2-clip 1
frame 1
viewport 0 0 255 191 1
v 0 BF800000 3F800000 00000000 3F800000 00000000 00000000 00000000 3F800000 00000000 00000000 0 0
v 1 3F800000 3F800000 00000000 3F800000 00000000 00000000 00000000 3F800000 00000000 00000000 0 0
v 2 3F800000 BF7FFF80 00000000 3F800000 00000000 00000000 00000000 3F800000 00000000 00000000 0 0
stats commands=94 vertices_in=44 triangles_out=22 quads_split=11 dropped_incomplete=0 ignored_outside_begin=0 ignored_no_effect=0 unsupported=0 raw_fifo_skipped=0 stack_errors=0 capacity_dropped=0 geometry_incomplete=0 tex_transform_unsupported=0
```

Azahar core used (external; not vendored): source tree commit `9c38437`,
`.so` 67514328 bytes:

```
$ git -C "$HOME/3ds/azahar" log -1 --format='%H %ci'
9c38437daad90a2f05f55a047d04d4073a476456 2026-11-03 17:37:58 +0100
$ stat -c '%s bytes' "$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so"
67514328 bytes
```

## 2. REQ 1 — source read, and what each candidate predicts

### 2.1 `tools/azahar_host` adds no rotation and no offset (SOURCE-READ)

The host does not rotate or shift; it stores the libretro frame row by row and
writes it back row by row.

`tools/azahar_host/azahar_host.c:271-306` (`video_refresh_callback`; the copy
is verbatim, `pitch` only decides the source stride):

```
static void video_refresh_callback(const void *data, unsigned width,
                                   unsigned height, size_t pitch) {
...
    stride = pitch;
...
    for (y = 0; y < height; y++) {
        memcpy(g_frame + (size_t)y * width * 4u,
               (const unsigned char *)data + (size_t)y * stride,
               (size_t)width * 4u);
    }
```

`tools/azahar_host/azahar_host.c:336-368` (`write_ppm`; verbatim
XRGB8888→RGB24, `width x height`, no crop/flip/offset):

```
static int write_ppm(const char *path, unsigned width, unsigned height) {
...
    if (xrgb8888_to_rgb24(g_frame, rgb, width, height,
                          (long)width * 4) != 0) {
...
    if (fprintf(file, "P6\n%u %u\n255\n", width, height) < 0 ||
        fwrite(rgb, 1, pixels * 3u, file) != pixels * 3u) {
```

`tools/azahar_host/azahar_host_util.h:42-66` is the same row-by-row conversion
(the only optional transform is a whole-image vertical flip when the caller
passes a negative pitch; the host passes `width*4`). SOURCE-READ of the host
therefore predicts **no one-row shift and no row-0 artefact from the host**.

### 2.2 The core's libretro software presentation (SOURCE-READ)

The core reports `400x480`
(`$HOME/3ds/azahar/src/citra_libretro/emu_window/libretro_window.cpp:302-317`,
`ComputeLayoutGeometry` + `SET_GEOMETRY`) and the host logs it (§3). The default
layout puts the top screen at `(0,0,400,240)` and the bottom at
`(40,240,360,480)` (`$HOME/3ds/azahar/src/core/frontend/framebuffer_layout.cpp:95`
`DefaultFrameLayout` → `:187-237` `LargeFrameLayout`;
`$HOME/3ds/azahar/src/core/3ds.h:16-19`: top 400x240, bottom 320x240).

The software renderer copies the PICA framebuffer into `ScreenInfo` here
(`src/video_core/renderer_software/renderer_software.cpp:37-75`):

```
void RendererSoftware::LoadFBToScreenInfo(int i, const Pica::ColorFill& color_fill) {
...
    const s32 pixel_stride = framebuffer.stride / bpp;
    info.height = framebuffer.height;
    info.width = pixel_stride;
    info.pixels.resize(info.width * info.height * 4);

    for (u32 y = 0; y < info.height; y++) {
        for (u32 x = 0; x < info.width; x++) {
            const u8* pixel = framebuffer_data + (y * pixel_stride + pixel_stride - x) * bpp;
...
            const u32 output_offset = (x * info.height + y) * 4;
```

For the arm11 target the emulated PICA framebuffer defaults are top
`width=240, height=400, stride=3*240` and bottom `width=240, height=320`
(`$HOME/3ds/azahar/src/video_core/pica/pica_core.cpp:113-122`):

```
    framebuffer_top.width.Assign(240);
    framebuffer_top.height.Assign(400);
    framebuffer_top.stride = 3 * 240;
...
    framebuffer_sub.width.Assign(240);
    framebuffer_sub.height.Assign(320);
    framebuffer_sub.stride = 3 * 240;
```

so with RGB8 (3 bytes/px) `info.height = 400` and `info.width = 240` for the
top, and `info.height = 320`, `info.width = 240` for the bottom. The read
`pixel_stride - x` gives columns `240,239,…,1` for `x = 0..239`: **column 0 is
never read and column 240 is read out of the row** (it is the next row's
column 0 in contiguous memory, or one past the whole buffer on the last row).
The storage index `(x*info.height + y)` is the transpose.

The libretro blit then maps the transposed screen to the PPM
(`src/citra_libretro/emu_window/libretro_window.cpp:131-152`):

```
            const u32 native_w = info.height;
            const u32 native_h = info.width;

            for (u32 oy = 0; oy < rect_h; oy++) {
                for (u32 ox = 0; ox < rect_w; ox++) {
                    const u32 dx = ox * native_w / rect_w;
                    const u32 dy = oy * native_h / rect_h;

                    const u32 src_off = (dy * info.height + dx) * 4;
                    if (src_off + 3 >= info.pixels.size())
                        continue;
...
                    const size_t dst_off = static_cast<size_t>(rect.top + oy) * pitch +
                                           static_cast<size_t>(rect.left + ox) * 4;
```

With `rect = (0,0,400,240)` this is `dx = ox`, `dy = oy`, so
`PPM(ox,oy)` reads `ScreenInfo(x=oy, y=ox)` = framebuffer row `ox`, column
`240-oy`. The correct read (one column lower) would be `239-oy`.

### 2.3 The arithmetic prediction, and why it matches §3

For the top screen `PPM(ox,oy) = framebuffer[ox][240-oy]`:

- every horizontal (DS-y) boundary moves **one PPM row down**, uniformly, for
  all geometry and all y (because `240-oy = 239-(oy-1)`);
- the vertical (DS-x) axis is `PPM x = framebuffer row = ox`, so vertical
  edges do **not** move;
- PPM row 0 is `framebuffer[ox][240]`, i.e. the **next framebuffer row's
  column 0**, so row 0 is the last row's content shifted left one pixel in x;
- at `ox = 399`, `framebuffer[399][240]` is one pixel **past** the
  `240*400` pixel allocation; this run's value is 0, so PPM(399,0) is black
  while PPM(399,1) is the clear colour.

The same `LoadFBToScreenInfo`/`blit_screen` pair renders the bottom rect
(`:157-160`); the bottom framebuffer is read through the same `stride-x`
expression with its own height/stride (its rect is 320x240, `40,240,360,480`).

### 2.4 The arm11 path: target, viewport, clipfix (SOURCE-READ)

`arm11/source/renderer.c:47-52` — the display transfer has no vertical flip,
no raw copy, no scaling:

```
#define DISPLAY_TRANSFER_FLAGS \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | \
     GX_TRANSFER_RAW_COPY(0) | \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | \
     GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))
```

`arm11/source/renderer.c:742-750` — the target is 240x400 and is presented to
`GFX_TOP/GFX_LEFT` with those flags:

```
    g_renderer.target = C3D_RenderTargetCreate(
        240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
...
    C3D_RenderTargetSetOutput(g_renderer.target, GFX_TOP, GFX_LEFT,
                              DISPLAY_TRANSFER_FLAGS);
```

citro3d binds that target with a full-target viewport
(`$HOME/3ds/citro3d/source/renderqueue.c:179-186`,
`C3D_SetViewport(0, 0, width, height)`), i.e. halfsize `(120,200)`, offset
`(0,0)` and scissor disabled (`$HOME/3ds/citro3d/source/base.c:136-145`). The
clip program applies only the clipfix uniform (`renderer.c:970`, `:1579-1584`):

```
    twlhd_clipfix_matrix(viewport, g_renderer.clipfix_pending);
...
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g_renderer.u_loc_clipfix, &m4);
```

and the shader is a pure `dp4` (`arm11/shaders/twlhd_clip_vshader.v.pica:22-27`):

```
	dp4 outpos.x, clipfix[0], inpos
	dp4 outpos.y, clipfix[1], inpos
	dp4 outpos.z, clipfix[2], inpos
	dp4 outpos.w, clipfix[3], inpos
```

`common/twlhd_clipfix.c:34-40` builds that matrix from the recorded viewport
(`x1=0,y1=0,x2=255,y2=191` → `hw=hh=1`, `cx=cy=0`):

```
    hw = (float)width / 256.0f;
    hh = (float)height / 192.0f;
    cx = (float)(2 * (int)x1 + width) / 256.0f - 1.0f;
    cy = (float)(2 * y1v + height) / 192.0f - 1.0f;

    m[0][0] = 0.0f;  m[0][1] = hh;    m[0][2] = 0.0f;  m[0][3] = cy;
    m[1][0] = -0.8f * hw; m[1][1] = 0.0f; m[1][2] = 0.0f; m[1][3] = -0.8f * cx;
```

so DS clip y (`−1..1` for DS `y=0..192`) becomes PICA clip x with halfsize 120:
the target short coordinate is `240 - 1.25*y_ds` (DS y grows toward target
x=0), with **zero offset**. The load read's `stride-x` reversal then turns that
axis back into a PPM row that grows with DS y; the only error in that step is
the one-column shift of §2.2. The DS x axis picks up the 40 px pillar offset
(`0.8=320/400`, `common/twlhd_clipfix.h:24-26`). The interpreter has no DS
`ceil` on y (`common/twlhd_gx_interp.c:540-559`, `out->clip[row] =
(float)(acc / 4096.0)`), only the 1/4096 matrix floor.

Therefore (A) as implemented predicts the half-open DS geometry directly in
the target (reversed on the short axis, then reversed back by the load read):
horizontal boundaries at `ceil(1.25*y_ds)` and no extra row; vertical
boundaries at `40 + 1.25*x_ds`; PPM row 0 normal content, not a wrap.

### 2.5 The PICA rasteriser fill rule (SOURCE-READ)

`$HOME/3ds/azahar/src/video_core/renderer_software/sw_rasterizer.cpp:208-227`
builds screen coordinates from the f24 viewport; `:235-238` converts to
`Fix12P4` (1/16 px); `:291-296` sets the top-left bias (`-1` for a right-side
or flat-bottom edge, 0 otherwise); `:306-326` tests half-pixel centres
(`min_y + 8`, `x += 0x10`) and skips only `w < 0`:

```
    for (u16 y = min_y + 8; y < max_y; y += 0x10) {
...
                const s32 w0 = bias0 + SignedArea(vtxpos[1].xy(), vtxpos[2].xy(), {x, y});
...
                if (w0 < 0 || w1 < 0 || w2 < 0) {
                    continue;
                }
```

This is a top-inclusive / bottom-exclusive half-open rule: a rectangle
`[y0,y1)` is painted on the PICA grid as `[ceil(1.25*y0), ceil(1.25*y1))`.
It is **axis-asymmetric by design** (top and left included, bottom and right
excluded), so it cannot move both the top and the bottom boundary of every
quad down by one row; it also cannot produce a PPM row that is another row's
shifted copy. (C) predicts no `+1`, no horizontal-only shift, and no row-0
artefact.

### 2.6 Per-candidate predictions

| candidate | horizontal edges | vertical edges | PPM row 0 |
|---|---|---|---|
| (A) arm11 target/projection/clipfix/GPU offset | a fixed y offset would move all horizontal boundaries by the same amount, but the source above has no such offset (`cy=0`, viewport offset 0, no transfer flip/crop), so **no +1** | exact (a y-only offset does not touch x) | normal first scanline; no wrap |
| (B) Azahar core software presentation | **+1 PPM row, constant, independent of DS y** (`240-oy` instead of `239-oy`) | exact (long axis is copied 1:1) | **next framebuffer row's column 0** → last row shifted left one; last pixel reads past the framebuffer (black here) |
| (C) PICA rasteriser fill rule | half-open `[ceil(1.25*y0), ceil(1.25*y1))`; top-inclusive/bottom-exclusive; **no uniform +1** | exact half-open rule in x (already exact) | normal painted content; no wrap |

## 3. REQ 2 — measurements

### 3.1 The two frames, and the two Azahar runs (FACT)

Frame A is the unchanged captured frame. Frame B is the same frame with every
`VTX_16` x/y word's `y` moved `+3` raw units (raw unit = 1 DS px, verified by
the clip change below); it was produced by a scratch patcher outside the repo:

```
$ cat > "$R/shift_vtx.py" <<'PYEOF'
import struct, sys

def load(path):
    return bytearray(open(path, "rb").read())

def chunks(buf):
    ver, hdr, total, flags, frame, nchunks, a, b = struct.unpack_from("<8I", buf, 0)
    off = hdr
    out = []
    for _ in range(nchunks):
        ctype, plen, cflags, res = struct.unpack_from("<4I", buf, off)
        out.append((struct.pack("<I", ctype).decode("latin1"), off, plen))
        off += 16 + plen
    return out

def shift_frame(src, dst, dy):
    buf = load(src)
    for name, off, plen in chunks(buf):
        if name != "GXCM":
            continue
        base = off + 16
        n = plen // 8
        i = 0
        shifted = 0
        while i < n:
            cw = struct.unpack_from("<I", buf, base + i * 8)[0]
            if cw != 0x23:
                i += 1
                continue
            j = i
            while j < n and struct.unpack_from("<I", buf, base + j * 8)[0] == 0x23:
                j += 1
            run = j - i
            if run % 2 != 0:
                raise SystemExit("odd VTX_16 run at entry %d" % i)
            for k in range(i, j, 2):
                p = base + k * 8 + 4
                val = struct.unpack_from("<I", buf, p)[0]
                x = val & 0xFFFF
                y = (val >> 16) & 0xFFFF
                y = (y + dy) & 0xFFFF
                struct.pack_into("<I", buf, p, x | (y << 16))
                shifted += 1
            i = j
    open(dst, "wb").write(buf)
    print("shifted %d x/y vertex words by %+d raw units" % (shifted, dy))

if __name__ == "__main__":
    shift_frame(sys.argv[1], sys.argv[2], int(sys.argv[3]))
PYEOF
$ python3 "$R/shift_vtx.py" "$R/cap/twlhd_frame_v2.bin" "$R/frame_y3.bin" 3
shifted 44 x/y vertex words by +3 raw units
$ sha256sum "$R/frame_y3.bin"
2959a7f25d7ae88dcde1b2d191e5706de302ef6dd122427b300f1db2353b541d  /tmp/opencode/eq4n-r2/frame_y3.bin
$ tests/host/build/gx_interp_cli --eq2 "$R/frame_y3.bin" | sed -n '1,6p;$p'
twlhd-eq2-clip 1
frame 1
viewport 0 0 255 191 1
v 0 BF800000 3F780002 00000000 3F800000 00000000 00000000 00000000 3F800000 00000000 00000000 0 0
v 1 3F800000 3F780002 00000000 3F800000 00000000 00000000 00000000 3F800000 00000000 00000000 0 0
v 2 3F800000 BF83FFBF 00000000 3F800000 00000000 00000000 00000000 3F800000 00000000 00000000 0 0
stats commands=94 vertices_in=44 triangles_out=22 quads_split=11 dropped_incomplete=0 ignored_outside_begin=0 ignored_no_effect=0 unsupported=0 raw_fifo_skipped=0 stack_errors=0 capacity_dropped=0 geometry_incomplete=0 tex_transform_unsupported=0
```

`v 0`'s y clip is `3F800000` (1.0) in A and `3F780002` (0.96875) in B, i.e.
`−2*3/192 = −0.03125`: the +3 raw units are +3 DS px, as intended.

The frame load reads one fixed SD path, `sdmc:/twlhd_frame.bin`
(`arm11/source/main.c:35`), and it runs when A is pressed
(`arm11/source/main.c:579-592`). (The only other SD reads in the program are
the X-key sequence frames, `sdmc:/twlhd_seq/seq_%03u.bin`
`common/twlhd_seq.h:44`, which these runs never use.) `tools/azahar_host`
recursively copies the
`--sd-root` directory into `sdmc:/` of its isolated temp root before the core
starts (`tools/azahar_host/azahar_host.c:495-513`, log line `:512`; the run
log below shows the mapping for each run). Both roots were therefore staged
**before** the two runs, each holding the frame at the exact name the arm11
program opens:

```
$ mkdir -p "$R/sdA" "$R/sdB"
$ cp "$R/cap/twlhd_frame_v2.bin" "$R/sdA/twlhd_frame.bin"
$ cp "$R/frame_y3.bin" "$R/sdB/twlhd_frame.bin"
$ sha256sum "$R/sdA/twlhd_frame.bin" "$R/sdB/twlhd_frame.bin"
1e6ef3f464738f29ba695d9373271540ed70ff0b041f0a654d0d0806c9b10dc5  /tmp/opencode/eq4n-r2/sdA/twlhd_frame.bin
2959a7f25d7ae88dcde1b2d191e5706de302ef6dd122427b300f1db2353b541d  /tmp/opencode/eq4n-r2/sdB/twlhd_frame.bin
```

The staged hashes are the captured frame (`1e6ef3f4…`, §1) and the `+3` frame
(`2959a7f2…`, above) exactly, and each is staged under exactly the name
`FRAME_PATH` expands to — (FACT).

One Azahar run per frame, with two shot times each; both shots of a run are
byte-identical because the scene is static:

```
$ tools/azahar_host/build/azahar_host --core "$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so" \
    --game arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 150,199 \
    --out "$R/runA" --sd-root "$R/sdA" >"$R/runA.out" 2>"$R/runA.err"
$ echo "host_exit=$?"
host_exit=0
$ grep "av_info geometry" "$R/runA.err"; grep -c "software framebuffer 400x480" "$R/runA.err"
[azahar_host] av_info geometry 400x480 @ 60.000 fps
200
$ sha256sum "$R/runA_150.ppm" "$R/runA_199.ppm"
d06bbc6300536f7fc00fbde81261e55d58aefc38ee013d62e6f7f962eb2cd391  /tmp/opencode/eq4n-r2/runA_150.ppm
d06bbc6300536f7fc00fbde81261e55d58aefc38ee013d62e6f7f962eb2cd391  /tmp/opencode/eq4n-r2/runA_199.ppm

$ tools/azahar_host/build/azahar_host --core "$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so" \
    --game arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 150,199 \
    --out "$R/runB" --sd-root "$R/sdB" >"$R/runB.out" 2>"$R/runB.err"
$ echo "host_exit=$?"
host_exit=0
$ grep "av_info geometry" "$R/runB.err"; grep -c "software framebuffer 400x480" "$R/runB.err"
[azahar_host] av_info geometry 400x480 @ 60.000 fps
200
$ sha256sum "$R/runB_150.ppm" "$R/runB_199.ppm"
24a4fee3e79343381a8dd80eb4c46a2e254b5e533e9a8b5c268a6897fd3b0e7f  /tmp/opencode/eq4n-r2/runB_150.ppm
24a4fee3e79343381a8dd80eb4c46a2e254b5e533e9a8b5c268a6897fd3b0e7f  /tmp/opencode/eq4n-r2/runB_199.ppm
```

Frame A's top crop is byte-identical to EQ-4m's (`227454cc…`), so the
unchanged chain reproduces the baseline at this HEAD.

Which root each run actually used, and that the core then opened the staged
file (FACT):

```
$ grep "staged --sd-root" "$R/runA.err" "$R/runB.err"
/tmp/opencode/eq4n-r2/runA.err:[azahar_host] staged --sd-root /tmp/opencode/eq4n-r2/sdA as sdmc:/ (/tmp/azahar-host-B6WSYn/save/Azahar/sdmc)
/tmp/opencode/eq4n-r2/runB.err:[azahar_host] staged --sd-root /tmp/opencode/eq4n-r2/sdB as sdmc:/ (/tmp/azahar-host-VGbypN/save/Azahar/sdmc)
$ grep -F 'called path=[Wchar: /twlhd_frame.bin]' "$R/runA.err" "$R/runB.err"
/tmp/opencode/eq4n-r2/runA.err:[azahar_libretro] [   0.317332] Service.FS <Debug> core/file_sys/archive_sdmc.cpp:OpenFileBase:59: called path=[Wchar: /twlhd_frame.bin] mode=1
/tmp/opencode/eq4n-r2/runB.err:[azahar_libretro] [   0.319059] Service.FS <Debug> core/file_sys/archive_sdmc.cpp:OpenFileBase:59: called path=[Wchar: /twlhd_frame.bin] mode=1
```

Each run copied a different `--sd-root` (`sdA` / `sdB`) into its own private
temp tree (`/tmp/azahar-host-B6WSYn` / `-VGbypN`), and each run's core opened
`/twlhd_frame.bin` from that tree. Combined with the staged hashes above and
the different top screens of §3.2/§3.3, A loaded the captured frame
(`1e6ef3f4…`) and B loaded the `+3` frame (`2959a7f2…`) — (FACT).

### 3.2 Row 0 / row 239 and column 0 / column 399, on both frames (FACT)

```
$ python3 - "$R" <<'PYEOF'
import sys, os, hashlib
from PIL import Image
r = sys.argv[1]
for label, name in (("A", "runA_199.ppm"), ("B", "runB_199.ppm")):
    im = Image.open(os.path.join(r, name)).convert("RGB"); px = im.load()
    top = im.crop((0, 0, 400, 240))
    print("== frame", label, name, "size", im.size,
          "top400x240 sha256", hashlib.sha256(top.tobytes()).hexdigest())
    print(" row0[x]==row239[x+1] for x=0..398:",
          all(px[x, 0] == px[x + 1, 239] for x in range(399)))
    print(" row0 cols 36..41:", [px[x, 0] for x in range(36, 42)])
    print(" row239 cols 36..41:", [px[x, 239] for x in range(36, 42)])
    print(" row0 col399:", px[399, 0], "row1 col399:", px[399, 1])
    print(" col0 rows 0..1:", [px[0, y] for y in range(2)],
          "col399 rows 0..1:", [px[399, y] for y in range(2)])
    print(" bottom row240[x]==row479[x+1] x=40..358:",
          all(px[x, 240] == px[x + 1, 479] for x in range(40, 359)))
    print(" bottom row240[359]:", px[359, 240], "row241[359]:", px[359, 241],
          "row479[359]:", px[359, 479])
    first = next(y for y in range(240, 480)
                 if any(px[x, y] != (0, 0, 0) for x in range(40, 360)))
    print(" bottom first content row:", first)
PYEOF
== frame A runA_199.ppm size (400, 480) top400x240 sha256 227454cccddbc47feee337bb4473158f62e58b63cb77599e261349b293bb307f
 row0[x]==row239[x+1] for x=0..398: True
 row0 cols 36..41: [(16, 16, 24), (16, 16, 24), (16, 16, 24), (255, 255, 255), (255, 255, 255), (255, 255, 255)]
 row239 cols 36..41: [(16, 16, 24), (16, 16, 24), (16, 16, 24), (16, 16, 24), (255, 255, 255), (255, 255, 255)]
 row0 col399: (0, 0, 0) row1 col399: (16, 16, 24)
 col0 rows 0..1: [(16, 16, 24), (16, 16, 24)] col399 rows 0..1: [(0, 0, 0), (16, 16, 24)]
 bottom row240[x]==row479[x+1] x=40..358: True
 bottom row240[359]: (0, 0, 0) row241[359]: (0, 0, 0) row479[359]: (0, 0, 0)
 bottom first content row: 241
== frame B runB_199.ppm size (400, 480) top400x240 sha256 a72ac48752b61d8ab578f8ec917b37f3dbaf2793b3947cb300b9270d1b2d22a3
 row0[x]==row239[x+1] for x=0..398: True
 row0 cols 36..41: [(16, 16, 24), (16, 16, 24), (16, 16, 24), (255, 255, 255), (255, 255, 255), (255, 255, 255)]
 row239 cols 36..41: [(16, 16, 24), (16, 16, 24), (16, 16, 24), (16, 16, 24), (255, 255, 255), (255, 255, 255)]
 row0 col399: (0, 0, 0) row1 col399: (16, 16, 24)
 col0 rows 0..1: [(16, 16, 24), (16, 16, 24)] col399 rows 0..1: [(0, 0, 0), (16, 16, 24)]
 bottom row240[x]==row479[x+1] x=40..358: True
 bottom row240[359]: (0, 0, 0) row241[359]: (0, 0, 0) row479[359]: (0, 0, 0)
 bottom first content row: 241
```

What this shows, against §2.3:

- `row0[x] == row239[x+1]` for all 399 x on both frames; at the pillar
  boundary row 0 is white at x=39 while row 239 is still clear at x=39 and
  white at x=40. That is the **long-axis wrap**: row 0 samples column 240 of
  framebuffer row `ox`, which is column 0 of framebuffer row `ox+1`, i.e. it
  shifts row 0 left by one framebuffer row. No geometry or fill rule can shift
  only row 0 horizontally by one pixel.
- `row0 col399 = (0,0,0)` while `row1 col399 = (16,16,24)` on both frames: the
  last pixel of row 0 is the value read at framebuffer index
  `399*240 + 240 = 96000`, exactly one pixel past the 240x400 allocation. The
  value is 0 in this run, so it is black; the destination pixel is not
  skipped because its `ScreenInfo` index (399) is in range.
- Columns 0 and 399 otherwise: `col0` is the clear colour on both rows;
  `col399` is black only on the wrapped row 0. The vertical (x) axis has no
  shift.
- The bottom rect (40,240)-(360,480) repeats the same first-row identity
  (`row240[x] == row479[x+1]`, 319/319 px); its first non-background row is
  PPM 241, one row inside the rect. The bottom background is black, so
  `row240[359]` (the out-of-range pixel) is not distinguishable from the
  background there; the bottom check is consistent with (B) but the
  top-screen evidence above is the decisive one.

### 3.3 Geometry dependence: the same frame moved +3 DS rows (FACT)

White-run boundaries at `col 100`; `measured` is the first white row of a top
edge / first black row of a bottom edge; `meas-1` removes the presentation row;
`ceil` is `ceil(1.25*y_ds)`.

```
$ python3 - "$R" <<'PYEOF'
import sys, os, math
from PIL import Image
r = sys.argv[1]
edges_ds = [("top_band.bottom", 16), ("sq1.top", 32), ("sq1.bottom", 64),
            ("sq3.top", 128), ("sq3.bottom", 160), ("bottom_band.top", 176)]
for label, name, dy in (("A", "runA_199.ppm", 0), ("B", "runB_199.ppm", 3)):
    im = Image.open(os.path.join(r, name)).convert("RGB"); px = im.load()
    white = [sum(px[100, y]) / 3.0 >= 128 for y in range(240)]
    runs = []; s = None
    for i, f in enumerate(white):
        if f and s is None: s = i
        elif not f and s is not None: runs.append((s, i - 1)); s = None
    if s is not None: runs.append((s, 239))
    print("== frame %s (DS geometry y+%d) col100 white runs %s" % (label, dy, runs))
    print(" %-17s %7s %8s %8s %7s %7s" % ("edge", "y_ds", "nominal", "measured", "meas-1", "ceil"))
    for name_e, y in edges_ds:
        yy = y + dy
        nominal = 1.25 * yy
        start = int(math.ceil(nominal))
        if name_e.endswith("top"):
            m = next(t for t in range(start, 240) if white[t])
        else:
            m = next(t for t in range(start, 240) if not white[t])
        print(" %-17s %7.2f %8.2f %8d %7.2f %7d" % (name_e, yy, nominal, m, m - 1, math.ceil(nominal)))
PYEOF
== frame A (DS geometry y+0) col100 white runs [(0, 20), (41, 80), (161, 200), (221, 239)]
 edge                 y_ds  nominal measured  meas-1    ceil
 top_band.bottom     16.00    20.00       21   20.00      20
 sq1.top             32.00    40.00       41   40.00      40
 sq1.bottom          64.00    80.00       81   80.00      80
 sq3.top            128.00   160.00      161  160.00     160
 sq3.bottom         160.00   200.00      201  200.00     200
 bottom_band.top    176.00   220.00      221  220.00     220
== frame B (DS geometry y+3) col100 white runs [(0, 0), (5, 24), (45, 84), (165, 204), (225, 239)]
 edge                 y_ds  nominal measured  meas-1    ceil
 top_band.bottom     19.00    23.75       25   24.00      24
 sq1.top             35.00    43.75       45   44.00      44
 sq1.bottom          67.00    83.75       85   84.00      84
 sq3.top            131.00   163.75      165  164.00     164
 sq3.bottom         163.00   203.75      205  204.00     204
 bottom_band.top    179.00   223.75      225  224.00     224
```

Two conclusions from the table:

- **A (integer DS rows):** `measured = 1.25*y_ds + 1` exactly; `meas-1 =
  1.25*y_ds` (pre-presentation boundary). This is EQ-4m's `+0.8000` DS =
  `+1` capture row.
- **B (+3 DS rows, fractional capture positions):** `nominal = 43.75` etc.,
  `meas-1 = ceil(nominal)` for every edge, and `measured = ceil(nominal) + 1`.
  The extra `+1` is constant; only the sub-pixel `ceil` follows the geometry.
  That `ceil` is exactly what §2.5's top-left rule predicts for the PICA
  grid. So (C) is present and accounts for the `+0.25` in this frame, but the
  `+1` is independent of geometry and is added after the rasteriser.

The vertical axis is unchanged by the y shift (as it must be):

```
$ python3 - "$R" <<'PYEOF'
import sys, os
from PIL import Image
r = sys.argv[1]
for label, name in (("A", "runA_199.ppm"), ("B", "runB_199.ppm")):
    im = Image.open(os.path.join(r, name)).convert("RGB"); px = im.load()
    print("frame", label, "row120 cols 37..41:", [(c, px[c, 120]) for c in range(37, 42)])
    print("frame", label, "row120 cols 355..361:", [(c, px[c, 120]) for c in range(355, 362)])
PYEOF
frame A row120 cols 37..41: [(37, (16, 16, 24)), (38, (16, 16, 24)), (39, (16, 16, 24)), (40, (255, 255, 255)), (41, (255, 255, 255))]
frame A row120 cols 355..361: [(355, (255, 255, 255)), (356, (255, 255, 255)), (357, (255, 255, 255)), (358, (255, 255, 255)), (359, (255, 255, 255)), (360, (16, 16, 24)), (361, (16, 16, 24))]
frame B row120 cols 37..41: [(37, (16, 16, 24)), (38, (16, 16, 24)), (39, (16, 16, 24)), (40, (255, 255, 255)), (41, (255, 255, 255))]
frame B row120 cols 355..361: [(355, (255, 255, 255)), (356, (255, 255, 255)), (357, (255, 255, 255)), (358, (255, 255, 255)), (359, (255, 255, 255)), (360, (16, 16, 24)), (361, (16, 16, 24))]
```

Left band starts at capture col 40 (DS 0) and the right band ends at 359
(DS 255), black/clear at 360 (DS 256), for both frames.

### 3.4 What separates the candidates

- **(A) vs (B):** the top-screen row 0 is a horizontally shifted copy of the
  last row and its last pixel is a read past the framebuffer allocation. Those
  are properties of the read/rotation index, not of a projection or a
  rasteriser. Also, after removing the presentation row, both frames place the
  boundary at exactly `ceil(1.25*y_ds)` — the arm11 path has no extra row.
  (A) is excluded.
- **(B) vs (C):** the same `+1` appears for every edge and for every
  geometry, whereas the PICA fill rule (C) is top-inclusive/bottom-exclusive
  and can move at most one boundary of a quad; after removing the `+1`, the
  painted rows are exactly the half-open `ceil` interval (C), so (C) is not
  the source of the `+1`. The row-0 wrap has no rasteriser explanation.
  (C) is excluded as the source of the shift; it is the source of the
  integer-grid snap.
- **Bottom screen:** the console is 2D-engine content and never passes through
  the arm11 clipfix/projection or the PICA 3D rasteriser. The bottom rect
  starts with the same wrapped first row (row 240 == row 479 shifted) and its
  first non-background row is rect row 1. The black console background masks
  the out-of-range pixel there, so this is corroborating rather than decisive;
  the shared code is the decisive part (`LoadFBToScreenInfo`/`blit_screen` are
  used for both rects, `libretro_window.cpp:157-160`).

## 4. REQ 3 — conclusion, labels, and a hardware test that would falsify it

**FACT.** At HEAD `0a40b6a`, the unchanged chain reproduces EQ-4m's top crop
(`227454cc…`); the `+3` DS-row frame moves every measured horizontal boundary
to `ceil(1.25*(y_ds+3)) + 1`, while the measured vertical edges (the left/right
band boundaries) stay at capture cols 40 and 360 in both frames. Row 0 equals
row 239 shifted left one pixel and its last pixel is black (a read one pixel
past the 240x400 top framebuffer).

**SOURCE-READ.** `tools/azahar_host` copies the libretro frame verbatim. The
Azahar core's software renderer reads the top framebuffer as
`framebuffer[y][stride-x]` with `x=0..239` (columns 240..1 instead of 239..0)
and the libretro blit maps that column axis to the PPM row axis. That
arithmetic predicts exactly the measured `+1`, the left-shifted row 0, and the
past-the-end pixel.

**Verdict.**

- **(B) SUPPORTED.** The one-row shift is the Azahar core's libretro software
  presentation (transposed framebuffer read with a one-pixel column error),
  not the arm11 program. The real 3DS has no such readback/rotation step; the
  task's premise that hardware shows nothing of (B) is consistent with this.
- **(A) NOT SUPPORTED.** The target is 240x400 with `GX_TRANSFER_FLIP_VERT(0)`,
  the viewport is the full target (offset 0), the clipfix matrix is
  `hw=hh=1, cx=cy=0`, and the clip shader is a pure `dp4`. There is no one-row
  term. The measured pre-presentation boundary is exactly `ceil(1.25*y_ds)`.
- **(C) NOT THE SOURCE OF THE `+1`.** The PICA top-left rule is
  top-inclusive/bottom-exclusive and axis-asymmetric; it produces the
  integer-grid `ceil` (the `+0.25` seen in the `+3` frame) but cannot shift
  both boundaries of every quad by one row, and cannot produce the row-0
  wrap. After subtracting the presentation `+1`, the painted rows are exactly
  the half-open `ceil` interval predicted by the quoted rasteriser.

**UNVERIFIED.** The real 3DS GPU was not measured; this investigation used the
Azahar software renderer only. The upstream intent of
`pixel_stride - x` (whether the missing `-1` is a known Azahar bug or a
compensation for a flipped framebuffer) was not established beyond the
arithmetic match; the bottom-screen artefact is masked by the console's black
background; and a hardware display-transfer row offset (if any) was not
measured. A hardware run is the only way to falsify (B).

**Hardware test (plan only — nothing was run on hardware).**

1. **3dsx path (our arm11 program).** Build at this HEAD with
   `make -C arm11`; copy `arm11/twlhd-arm11.3dsx` and the v2 frame
   `twlhd_frame_v2.bin` to the SD card as `sdmc:/twlhd_frame.bin`. That name is
   the path the frame load opens — **SOURCE-READ** `arm11/source/main.c:35`:

   ```
   #define FRAME_PATH "sdmc:/twlhd_frame.bin"
   ```

   Launch the `.3dsx` from hbmenu and **press A** to load the frame —
   **SOURCE-READ** `arm11/source/main.c:579-592`:

   ```
           if (keys & KEY_A) {
               g_seq_on = 0;
               if (load_sd_frame(FRAME_PATH, &pending_vertex_count,
                                 &pending_stats, &pending_parse,
                                 &pending_frame_index, &pending_cmd_count,
                                 &pending_is_v2, &pending_viewport,
                                 &pending_gx_stats, &pending_gxcm_count,
                                 0) == 0) {
                   pending_load = 1;
                   pending_builtin = 0;
                   pending_source = FRAME_PATH;
               }
               update_console();
           }
   ```

   The key state is polled once per main-loop iteration, so A loads the frame
   whenever it is pressed — there is **no frame window inside the arm11
   program** (the on-screen legend `A=load SD  Y=builtin  X=seq  START=exit` is
   `arm11/source/main.c:476`). The `frames 60..63` in §3 is the *host-side*
   `--press A:60-63` schedule applied by `tools/azahar_host`: `--press
   NAME:a-b` "Hold a button on frames `a..b` inclusive"
   (`tools/azahar_host/README.md:53`). On hardware the operator simply presses
   A once, any time after the program starts; the §3 host schedule has no
   hardware counterpart.

   Capture the top LCD with the existing capture rig `tools/cc3dsfs_capture`:
   a passive capture board through `cc3dsfs`, which EQ-4b ran
   (`docs/reports/eq4b-cc3dsfs-ds-capture.md:3-8`) and which EQ-4l reports for
   its owner-run capture (`docs/reports/eq4l-edge-rows-hw-result.md:32-44`).
   **SOURCE-READ** of its entry point,
   `tools/cc3dsfs_capture/cc_shots.sh:1-11`:

   ```
   #!/bin/sh
   # SPDX-License-Identifier: Apache-2.0
   # usage: CC3DSFS_BIN=<dir with cc3dsfs> OUT_DIR=<dir> sh cc_shots.sh <label> <n> <gap_s>
   # Xvfb + cc3dsfs (capture board), one xwd->png per gap.
   L=${1:-t}; N=${2:-24}; G=${3:-5}
   O=${OUT_DIR:-$PWD}
   B=${CC3DSFS_BIN:-.}
   HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
   cd "$B" || exit 1
   export OUT_DIR="$O" L N G CONVERTER="$HERE/xwd2png.py"
   xvfb-run -a -s "-screen 0 1280x720x24" sh -c '
   ```

   (the `sh -c '` block continues on line 12 with
   `./cc3dsfs --auto_connect --no_audio --no_auto_save …`). Then run
   `tools/ds_edge_test/measure_rows.py` on the 400x240 crop.
   `tools/ntr_capture` also exists in the tree but is **BUILD/UNIT only** —
   "neither was **connected to a console** during implementation"
   (`tools/ntr_capture/README.md:14-17`) — so it is not a rig this plan may
   cite as tested. Using `tools/cc3dsfs_capture` to capture a *3DS-mode*
   `.3dsx` top screen (rather than the DS-mode ROM of EQ-4b/EQ-4l) is
   **UNVERIFIED**: the rig is described as DS-mode capture, its 400×240 layout,
   frame sync and capture window size are labelled UNVERIFIED
   (`tools/cc3dsfs_capture/README.md:1,13-16`).
2. **DS ROM path (control).** Run `tools/ds_edge_test/ds-edge-test.nds` (the
   default `make -C tools/ds_edge_test` target; not rebuilt or hashed this
   round — **UNVERIFIED** here) on the New 3DS in DS mode (flashcard) and
   capture its top screen with the same `tools/cc3dsfs_capture` rig — this is
   the combination EQ-4b and EQ-4l actually used; compare with EQ-4l's
   `classification=none`, every edge within about 0.1 DS px of nominal.
3. **What the result means.** If hardware puts every horizontal boundary at
   nominal `1.25*y_ds` (no `+1`) and has no row-0 wrap, (B) is confirmed and
   the shift is presentation-only; our arm11 path needs no change and future
   host-based per-row claims must subtract the presentation row or fix the
   core's blit. If hardware shows the same `+1`/wrap, (B) is wrong and the
   arm11/PICA path has a real problem that this round did not find.

**Consequence for the repository.** Do not add `+1`/`-1` to the DS geometry or
the GX interpreter, and do not change `common/twlhd_clipfix.c`; if a host-based
comparison must be exact, account for the presentation row in the host/analysis
tool (or fix the external core), and re-validate on hardware.

## 5. REQ 4 — tests (FACT)

No code was added; existing suites were run unchanged:

```
$ make test-ds-edge-test
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/ds_edge_test -p 'test_*.py'
..................................
----------------------------------------------------------------------
Ran 34 tests in 0.742s

OK
```

```
$ make test >"$R/make-test.log" 2>&1; rc=$?; echo "make_test_exit=$rc"; tail -25 "$R/make-test.log"
make_test_exit=0
Ran 64 tests in 2.443s

OK
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/ds_edge_test -p 'test_*.py'
..................................
----------------------------------------------------------------------
Ran 34 tests in 0.758s

OK
EQ2_DISCOVER_SKIP=1 EQ3_DISCOVER_SKIP=1 EQ_SCENES_DISCOVER_SKIP=1 \
ARM9V2_STATIC_DISCOVER_SKIP=1 EQ4_HW_COMPARE_DISCOVER_SKIP=1 \
FIT_MAPPING_DISCOVER_SKIP=1 \
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
.....sssssss..........................................................................................................usage: compare_texture_capture.py <captured.bin> [angle]
usage: compare_texture_capture.py <captured.bin> [angle]
error: angle must be an integer
........ssssssssssssssssssssssssss...ssssssssssssssssssssssssssssssssssss...........................................................................................................................................................................................................
----------------------------------------------------------------------
Ran 394 tests in 83.838s

OK (skipped=69)
/tmp/tmpantn6dwz/twlhd_frame_v2_tex_over.bin
vertex_count=36
max_clip_deviation=0.007324
colour_exactness=PASS
```

(The `usage:`/`error: angle` lines are expected stdout from the texture
compare test's own argument checks; `make_test_exit=0` and the final
`OK (skipped=69)` are the verdict. No test was changed, weakened or skipped by
this round.)

## 6. REQ 5 — house rules

- New file (added by this investigation, now tracked on this branch):
  `docs/reports/eq4n-arm11-row-shift.md`; append-only pointers in
  `docs/EQUALITY_CHECK_DESIGN.md`, `docs/EQ4_HW_COMPARE.md`, and
  `docs/reports/eq4m-pica-edge-rows.md` §7. No production file was touched.
- Diff budget. The earlier round's number was taken while the report was still
  untracked, so it did not cover the report body; the numbers below are taken
  now, after the report is tracked (committed on this branch), and therefore
  cover every changed file:

```
$ git status --short docs/reports/eq4n-arm11-row-shift.md
 M docs/reports/eq4n-arm11-row-shift.md
$ git add -N docs/reports/eq4n-arm11-row-shift.md && git diff main | wc -c
56221
$ git diff main --name-only
docs/EQ4_HW_COMPARE.md
docs/EQUALITY_CHECK_DESIGN.md
docs/reports/eq4m-pica-edge-rows.md
docs/reports/eq4n-arm11-row-shift.md
$ wc -c docs/reports/eq4n-arm11-row-shift.md
46933 docs/reports/eq4n-arm11-row-shift.md
```

  `git status --short` prints ` M` (tracked, modified), not `??` (untracked),
  so the report body is inside `git diff main`; `git add -N` on an
  already-tracked path is a no-op (FACT). The total is 56221 bytes, i.e. below
  the 90000-byte budget, and the `--name-only` list shows the diff covers
  exactly the four files this round changed — three docs plus this report; no
  code, no test and no build file. (The earlier round's untracked-report figure
  was `8120` bytes for the three pointer docs alone and is superseded by the
  measurement above.)

- Privacy: every command uses `$HOME` / `$R`; no home path or user name is
  added to the repository. Re-run after all edits above, so the report body is
  inside the diff:

```
$ grep -rn -E '/(home|Users)/' docs/reports/eq4n-arm11-row-shift.md; echo "grep_exit=$?"
grep_exit=1
$ git diff main | grep -cE '/(home|Users)/'; echo "diff_grep_exit=$?"
0
diff_grep_exit=1
```

## 7. Requirement traceability

- REQ 1: §2 — host copy (`azahar_host.c:271-306`, `:336-368`), core framebuffer
  read/rotation (`renderer_software.cpp:37-75`,
  `libretro_window.cpp:131-152`), 400x480 layout
  (`framebuffer_layout.cpp:95,187-237`, `core/3ds.h:16-19`), framebuffer
  defaults (`pica_core.cpp:113-122`), arm11 target and clipfix
  (`renderer.c:47-52,742-750,970,1579-1584`, shader `:22-27`), PICA fill rule
  (`sw_rasterizer.cpp:208-227,291-326`); per-candidate predictions in §2.6.
- REQ 2: §3 — the two `--sd-root` trees staged with the captured and the `+3`
  frame under the `FRAME_PATH` name (with staged `sha256sum`s and the per-run
  `staged --sd-root` / core file-open log), one Azahar run per frame (two shots
  each, A and B), row-0/row-239 and col-0/col-399 on both, bottom-rect check on
  both, and the `+3` DS-row geometry measurement; the three candidates are
  separated in §3.4.
- REQ 3: §4 — (B) supported, (A) not supported, (C) not the `+1`; explicit
  UNVERIFIED line; hardware procedure (3dsx + DS ROM) as plan only.
- REQ 4: §5 — `make test-ds-edge-test` (34 tests, OK) and `make test`
  (`make_test_exit=0`, 394 tests, OK, 69 skipped); no test weakened.
- REQ 5: this report; append-only pointers in `docs/EQUALITY_CHECK_DESIGN.md`,
  `docs/EQ4_HW_COMPARE.md`, `docs/reports/eq4m-pica-edge-rows.md` §7; diff and
  privacy checks in §6.
