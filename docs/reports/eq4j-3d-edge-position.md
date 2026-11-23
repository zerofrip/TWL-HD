# EQ-4j — 3D edge positions: hardware vs DeSmuME vs the exact DS geometry

**Basis: one console, one capture session, one ROM.** Offline analysis only: no
hardware action and no console access in this round. Every number below is taken
from a command quoted in this report and run in this round.

## 1. Question

EQ-4f (`docs/reports/eq4f-scene5-static-hw-compare.md`) and EQ-4g
(`docs/reports/eq4g-capture-offset.md`) found a ~1-2 DS px offset of real-hardware
captures against the DeSmuME/PICA references that the capture mapping does not
explain (`dy` negative for all 20 class centroids, `dx` mixed). This report asks
whether it is a real 3D rasterisation difference between hardware and DeSmuME,
using the `ds-alpha-test` ROM whose geometry is exact in
`tools/ds_alpha_test/quads.json` (two opaque backdrops, 14 translucent white
quads, 16 axis-aligned rectangles).

## 2. Tool and method

`tools/ds_alpha_test/measure_edges.py` (standard library + the repo's PIL /
P6-PPM readers) measures every rectangle edge to sub-pixel precision in the
hardware capture (DS pixels via a command-line mapping, default
`capture_mapping.json`) and in the DeSmuME native frame (`--native`, identity
mapping), then compares both with `quads.json`.

* Pixel-corner coordinates: the DS edge `e` sits at capture `offset + scale*e`,
  the inverse of `measure_alpha.capture_xy`.
* A perpendicular mean-`(R+G+B)/3` luminance profile comes from an inset span
  (>=2 DS px from the corners); the interior plateau is the median of the 2..6
  DS px band inside it, the background plateau the median of the backdrop's
  clear top strip (`y = backdrop.y+4 .. +28`) or of the neighbouring backdrop.
* The edge is the 50 % `(left+right)/2` crossing, interpolated between the two
  straddling pixels (`x = (i+0.5) + (mid-a)/(b-a)`). It must be unique in the
  `BAND = 5` capture-px window with both plateaus seen, or it is skipped with a
  reason.
* Edges below `MIN_CONTRAST = 10` levels are skipped; all 56 quad edges clear it
  and 54 are measured (13/13/14/14). R20 right and R24 left are skipped: their
  2-DS-px gap is about one capture pixel wide and the sharpening kernel pushes
  that sample more than the 12-level plateau tolerance off grey. The x=128
  boundary is measured once; screen-boundary and backdrop left/top edges are
  skipped with a reason.
* Statistics: mean and population standard deviation; significance is
  mean ± std/sqrt(n).

Runs: `<capture.png>` (`hw`), `--native <frame.ppm>` (`desmume`), and
`--desmume <frame.ppm> <capture.png>` (both).

## 3. Commands and raw output

### 3.1 Build the ROM and render the DeSmuME frame

```
$ cd tools/ds_alpha_test && make clean && make
main.c
linking ds-alpha-test.elf
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... ds-alpha-test.nds
$ sha256sum ds-alpha-test.nds
83e06e97e7b0e8d8e148da9aa2aa36b1238376e385fa187e6957073f90223c01  ds-alpha-test.nds
$ O=/tmp/eq4j-evidence
$ mkdir -p "$O"
$ "$HOME/3ds/twlhd-desmume-harness/harness" tools/ds_alpha_test/ds-alpha-test.nds 1 --out "$O/h.txt" --ppm "$O/h.ppm" >"$O/out" 2>"$O/err"; echo "harness_exit=$?"
harness_exit=0
$ grep -E "harness:" "$O/err"
harness: captured frame 1: 16 polygons, 64 vertices (first non-empty 3D frame: 1)
$ sha256sum "$O/h.ppm"
2fa0e1c971a33206c645a84c9bd802dc13e3acd1b27de0ac609c6c97e08a747e  /tmp/eq4j-evidence/h.ppm
```

### 3.2 DeSmuME native frame (`--native`)

`python3 tools/ds_alpha_test/measure_edges.py --native "$O/h.ppm"` prints, in
full:

```
mode=native
mapping scale_x=1 scale_y=1 offset_x=0 offset_y=0
backdrop black level=0.0000
backdrop grey level=134.0000
edges_measured=57 edges_skipped=7
edge rect=black kind=backdrop side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=black kind=backdrop side=right nominal=128.0000 desmume=128.0000 desmume-nominal=+0.0000
edge rect=black kind=backdrop side=top nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=black kind=backdrop side=bottom nominal=192.0000 skipped=on the DS screen boundary (capture crop)
edge rect=grey kind=backdrop side=left nominal=128.0000 skipped=backdrop left/top edge is the screen boundary or the duplicate side of an internal boundary
edge rect=grey kind=backdrop side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=grey kind=backdrop side=top nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=grey kind=backdrop side=bottom nominal=192.0000 skipped=on the DS screen boundary (capture crop)
edge rect=L4 kind=quad side=left nominal=2.0000 desmume=2.0000 desmume-nominal=+0.0000
edge rect=L4 kind=quad side=right nominal=18.0000 desmume=18.0000 desmume-nominal=+0.0000
edge rect=L4 kind=quad side=top nominal=32.0000 desmume=33.0000 desmume-nominal=+1.0000
edge rect=L4 kind=quad side=bottom nominal=160.0000 desmume=161.0000 desmume-nominal=+1.0000
edge rect=L8 kind=quad side=left nominal=20.0000 desmume=20.0000 desmume-nominal=+0.0000
edge rect=L8 kind=quad side=right nominal=36.0000 desmume=36.0000 desmume-nominal=+0.0000
edge rect=L8 kind=quad side=top nominal=32.0000 desmume=33.0000 desmume-nominal=+1.0000
edge rect=L8 kind=quad side=bottom nominal=160.0000 desmume=161.0000 desmume-nominal=+1.0000
edge rect=L12 kind=quad side=left nominal=38.0000 desmume=38.0000 desmume-nominal=+0.0000
edge rect=L12 kind=quad side=right nominal=54.0000 desmume=54.0000 desmume-nominal=+0.0000
edge rect=L12 kind=quad side=top nominal=32.0000 desmume=33.0000 desmume-nominal=+1.0000
edge rect=L12 kind=quad side=bottom nominal=160.0000 desmume=161.0000 desmume-nominal=+1.0000
edge rect=L16 kind=quad side=left nominal=56.0000 desmume=56.0000 desmume-nominal=+0.0000
edge rect=L16 kind=quad side=right nominal=72.0000 desmume=72.0000 desmume-nominal=+0.0000
edge rect=L16 kind=quad side=top nominal=32.0000 desmume=33.0000 desmume-nominal=+1.0000
edge rect=L16 kind=quad side=bottom nominal=160.0000 desmume=161.0000 desmume-nominal=+1.0000
edge rect=L20 kind=quad side=left nominal=74.0000 desmume=74.0000 desmume-nominal=+0.0000
edge rect=L20 kind=quad side=right nominal=90.0000 desmume=90.0000 desmume-nominal=+0.0000
edge rect=L20 kind=quad side=top nominal=32.0000 desmume=33.0000 desmume-nominal=+1.0000
edge rect=L20 kind=quad side=bottom nominal=160.0000 desmume=161.0000 desmume-nominal=+1.0000
edge rect=L24 kind=quad side=left nominal=92.0000 desmume=92.0000 desmume-nominal=+0.0000
edge rect=L24 kind=quad side=right nominal=108.0000 desmume=108.0000 desmume-nominal=+0.0000
edge rect=L24 kind=quad side=top nominal=32.0000 desmume=33.0000 desmume-nominal=+1.0000
edge rect=L24 kind=quad side=bottom nominal=160.0000 desmume=161.0000 desmume-nominal=+1.0000
edge rect=L28 kind=quad side=left nominal=110.0000 desmume=110.0000 desmume-nominal=+0.0000
edge rect=L28 kind=quad side=right nominal=126.0000 desmume=126.0000 desmume-nominal=+0.0000
edge rect=L28 kind=quad side=top nominal=32.0000 desmume=33.0000 desmume-nominal=+1.0000
edge rect=L28 kind=quad side=bottom nominal=160.0000 desmume=161.0000 desmume-nominal=+1.0000
edge rect=R4 kind=quad side=left nominal=130.0000 desmume=130.0000 desmume-nominal=+0.0000
edge rect=R4 kind=quad side=right nominal=146.0000 desmume=146.0000 desmume-nominal=+0.0000
edge rect=R4 kind=quad side=top nominal=32.0000 desmume=33.0000 desmume-nominal=+1.0000
edge rect=R4 kind=quad side=bottom nominal=160.0000 desmume=161.0000 desmume-nominal=+1.0000
edge rect=R8 kind=quad side=left nominal=148.0000 desmume=148.0000 desmume-nominal=+0.0000
edge rect=R8 kind=quad side=right nominal=164.0000 desmume=164.0000 desmume-nominal=+0.0000
edge rect=R8 kind=quad side=top nominal=32.0000 desmume=33.0000 desmume-nominal=+1.0000
edge rect=R8 kind=quad side=bottom nominal=160.0000 desmume=161.0000 desmume-nominal=+1.0000
edge rect=R12 kind=quad side=left nominal=166.0000 desmume=166.0000 desmume-nominal=+0.0000
edge rect=R12 kind=quad side=right nominal=182.0000 desmume=182.0000 desmume-nominal=+0.0000
edge rect=R12 kind=quad side=top nominal=32.0000 desmume=33.0000 desmume-nominal=+1.0000
edge rect=R12 kind=quad side=bottom nominal=160.0000 desmume=161.0000 desmume-nominal=+1.0000
edge rect=R16 kind=quad side=left nominal=184.0000 desmume=184.0000 desmume-nominal=+0.0000
edge rect=R16 kind=quad side=right nominal=200.0000 desmume=200.0000 desmume-nominal=+0.0000
edge rect=R16 kind=quad side=top nominal=32.0000 desmume=33.0000 desmume-nominal=+1.0000
edge rect=R16 kind=quad side=bottom nominal=160.0000 desmume=161.0000 desmume-nominal=+1.0000
edge rect=R20 kind=quad side=left nominal=202.0000 desmume=202.0000 desmume-nominal=+0.0000
edge rect=R20 kind=quad side=right nominal=218.0000 desmume=218.0000 desmume-nominal=+0.0000
edge rect=R20 kind=quad side=top nominal=32.0000 desmume=33.0000 desmume-nominal=+1.0000
edge rect=R20 kind=quad side=bottom nominal=160.0000 desmume=161.0000 desmume-nominal=+1.0000
edge rect=R24 kind=quad side=left nominal=220.0000 desmume=220.0000 desmume-nominal=+0.0000
edge rect=R24 kind=quad side=right nominal=236.0000 desmume=236.0000 desmume-nominal=+0.0000
edge rect=R24 kind=quad side=top nominal=32.0000 desmume=33.0000 desmume-nominal=+1.0000
edge rect=R24 kind=quad side=bottom nominal=160.0000 desmume=161.0000 desmume-nominal=+1.0000
edge rect=R28 kind=quad side=left nominal=238.0000 desmume=238.0000 desmume-nominal=+0.0000
edge rect=R28 kind=quad side=right nominal=254.0000 desmume=254.0000 desmume-nominal=+0.0000
edge rect=R28 kind=quad side=top nominal=32.0000 desmume=33.0000 desmume-nominal=+1.0000
edge rect=R28 kind=quad side=bottom nominal=160.0000 desmume=161.0000 desmume-nominal=+1.0000
stat x_left desmume-nominal mean=+0.0000 std=0.0000 n=14
stat x_right desmume-nominal mean=+0.0000 std=0.0000 n=14
stat y_top desmume-nominal mean=+1.0000 std=0.0000 n=14
stat y_bottom desmume-nominal mean=+1.0000 std=0.0000 n=14
stat width desmume-nominal mean=+0.0000 std=0.0000 n=14
stat height desmume-nominal mean=+0.0000 std=0.0000 n=14
```

### 3.3 Hardware capture, committed/default mapping

`python3 tools/ds_alpha_test/measure_edges.py
tools/ds_alpha_test/fixtures/al1_3_top400x240.png` prints (`mode=capture`,
`mapping scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0`,
`edges_measured=55 edges_skipped=9`):

```
stat x_left hw-nominal mean=-0.1069 std=0.0553 n=13
stat x_right hw-nominal mean=-0.0879 std=0.0348 n=13
stat y_top hw-nominal mean=-0.0734 std=0.0250 n=14
stat y_bottom hw-nominal mean=-0.0772 std=0.0229 n=14
stat width hw-nominal mean=+0.0179 std=0.0575 n=12
stat height hw-nominal mean=-0.0038 std=0.0460 n=14
```

The committed `capture_mapping.json` is exactly the "current" mapping
`(scale 1.25, 1.25, offset 40, 0)` (full run output: §3.4); a temporary JSON with
those values reproduces the default run byte for byte:

```
$ printf '{"scale_x":1.25,"scale_y":1.25,"offset_x":40,"offset_y":0}\n' > "$O/tmp.json"
$ cat "$O/tmp.json"
{"scale_x":1.25,"scale_y":1.25,"offset_x":40,"offset_y":0}
$ python3 tools/ds_alpha_test/measure_edges.py --mapping "$O/tmp.json" --desmume "$O/h.ppm" tools/ds_alpha_test/fixtures/al1_3_top400x240.png > "$O/tmp.out"
$ python3 tools/ds_alpha_test/measure_edges.py --desmume "$O/h.ppm" tools/ds_alpha_test/fixtures/al1_3_top400x240.png > "$O/def.out"
$ diff "$O/tmp.out" "$O/def.out"; echo "diff_exit=$?"
diff_exit=0
$ sha256sum "$O/tmp.out" "$O/def.out"
fb6762fb057586b38eee79731243ebbb663695759a5d13be788ce410fee34a73  /tmp/eq4j-evidence/tmp.out
fb6762fb057586b38eee79731243ebbb663695759a5d13be788ce410fee34a73  /tmp/eq4j-evidence/def.out
```

### 3.4 Combined comparison, committed mapping — full raw output

`python3 tools/ds_alpha_test/measure_edges.py --desmume "$O/h.ppm"
tools/ds_alpha_test/fixtures/al1_3_top400x240.png`:

```
mode=compare
mapping scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
backdrop black level=0.0000
backdrop grey level=132.6667
edges_measured=57 edges_skipped=7
edge rect=black kind=backdrop side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=black kind=backdrop side=right nominal=128.0000 hw=127.9399 hw-nominal=-0.0601 desmume=128.0000 desmume-nominal=+0.0000 hw-desmume=-0.0601
edge rect=black kind=backdrop side=top nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=black kind=backdrop side=bottom nominal=192.0000 skipped=on the DS screen boundary (capture crop)
edge rect=grey kind=backdrop side=left nominal=128.0000 skipped=backdrop left/top edge is the screen boundary or the duplicate side of an internal boundary
edge rect=grey kind=backdrop side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=grey kind=backdrop side=top nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=grey kind=backdrop side=bottom nominal=192.0000 skipped=on the DS screen boundary (capture crop)
edge rect=L4 kind=quad side=left nominal=2.0000 hw=1.9846 hw-nominal=-0.0154 desmume=2.0000 desmume-nominal=+0.0000 hw-desmume=-0.0154
edge rect=L4 kind=quad side=right nominal=18.0000 hw=17.8705 hw-nominal=-0.1295 desmume=18.0000 desmume-nominal=+0.0000 hw-desmume=-0.1295
edge rect=L4 kind=quad side=top nominal=32.0000 hw=31.9837 hw-nominal=-0.0163 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-1.0163
edge rect=L4 kind=quad side=bottom nominal=160.0000 hw=159.8632 hw-nominal=-0.1368 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.1368
edge rect=L8 kind=quad side=left nominal=20.0000 hw=19.9353 hw-nominal=-0.0647 desmume=20.0000 desmume-nominal=+0.0000 hw-desmume=-0.0647
edge rect=L8 kind=quad side=right nominal=36.0000 hw=35.9271 hw-nominal=-0.0729 desmume=36.0000 desmume-nominal=+0.0000 hw-desmume=-0.0729
edge rect=L8 kind=quad side=top nominal=32.0000 hw=31.9456 hw-nominal=-0.0544 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-1.0544
edge rect=L8 kind=quad side=bottom nominal=160.0000 hw=159.9271 hw-nominal=-0.0729 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0729
edge rect=L12 kind=quad side=left nominal=38.0000 hw=37.8216 hw-nominal=-0.1784 desmume=38.0000 desmume-nominal=+0.0000 hw-desmume=-0.1784
edge rect=L12 kind=quad side=right nominal=54.0000 hw=53.8905 hw-nominal=-0.1095 desmume=54.0000 desmume-nominal=+0.0000 hw-desmume=-0.1095
edge rect=L12 kind=quad side=top nominal=32.0000 hw=31.9235 hw-nominal=-0.0765 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-1.0765
edge rect=L12 kind=quad side=bottom nominal=160.0000 hw=159.9197 hw-nominal=-0.0803 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0803
edge rect=L16 kind=quad side=left nominal=56.0000 hw=55.9119 hw-nominal=-0.0881 desmume=56.0000 desmume-nominal=+0.0000 hw-desmume=-0.0881
edge rect=L16 kind=quad side=right nominal=72.0000 hw=71.9304 hw-nominal=-0.0696 desmume=72.0000 desmume-nominal=+0.0000 hw-desmume=-0.0696
edge rect=L16 kind=quad side=top nominal=32.0000 hw=31.9401 hw-nominal=-0.0599 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-1.0599
edge rect=L16 kind=quad side=bottom nominal=160.0000 hw=159.9229 hw-nominal=-0.0771 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0771
edge rect=L20 kind=quad side=left nominal=74.0000 hw=73.8222 hw-nominal=-0.1778 desmume=74.0000 desmume-nominal=+0.0000 hw-desmume=-0.1778
edge rect=L20 kind=quad side=right nominal=90.0000 hw=89.8903 hw-nominal=-0.1097 desmume=90.0000 desmume-nominal=+0.0000 hw-desmume=-0.1097
edge rect=L20 kind=quad side=top nominal=32.0000 hw=31.9321 hw-nominal=-0.0679 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-1.0679
edge rect=L20 kind=quad side=bottom nominal=160.0000 hw=159.9260 hw-nominal=-0.0740 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0740
edge rect=L24 kind=quad side=left nominal=92.0000 hw=91.9173 hw-nominal=-0.0827 desmume=92.0000 desmume-nominal=+0.0000 hw-desmume=-0.0827
edge rect=L24 kind=quad side=right nominal=108.0000 hw=107.9201 hw-nominal=-0.0799 desmume=108.0000 desmume-nominal=+0.0000 hw-desmume=-0.0799
edge rect=L24 kind=quad side=top nominal=32.0000 hw=31.9283 hw-nominal=-0.0717 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-1.0717
edge rect=L24 kind=quad side=bottom nominal=160.0000 hw=159.9199 hw-nominal=-0.0801 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0801
edge rect=L28 kind=quad side=left nominal=110.0000 hw=109.8273 hw-nominal=-0.1727 desmume=110.0000 desmume-nominal=+0.0000 hw-desmume=-0.1727
edge rect=L28 kind=quad side=right nominal=126.0000 hw=125.8740 hw-nominal=-0.1260 desmume=126.0000 desmume-nominal=+0.0000 hw-desmume=-0.1260
edge rect=L28 kind=quad side=top nominal=32.0000 hw=31.9310 hw-nominal=-0.0690 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-1.0690
edge rect=L28 kind=quad side=bottom nominal=160.0000 hw=159.9310 hw-nominal=-0.0690 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0690
edge rect=R4 kind=quad side=left nominal=130.0000 hw=130.0000 hw-nominal=-0.0000 desmume=130.0000 desmume-nominal=+0.0000 hw-desmume=-0.0000
edge rect=R4 kind=quad side=right nominal=146.0000 hw=145.9500 hw-nominal=-0.0500 desmume=146.0000 desmume-nominal=+0.0000 hw-desmume=-0.0500
edge rect=R4 kind=quad side=top nominal=32.0000 hw=31.8667 hw-nominal=-0.1333 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-1.1333
edge rect=R4 kind=quad side=bottom nominal=160.0000 hw=159.9636 hw-nominal=-0.0364 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0364
edge rect=R8 kind=quad side=left nominal=148.0000 hw=147.8974 hw-nominal=-0.1026 desmume=148.0000 desmume-nominal=+0.0000 hw-desmume=-0.1026
edge rect=R8 kind=quad side=right nominal=164.0000 hw=163.9618 hw-nominal=-0.0382 desmume=164.0000 desmume-nominal=+0.0000 hw-desmume=-0.0382
edge rect=R8 kind=quad side=top nominal=32.0000 hw=31.8974 hw-nominal=-0.1026 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-1.1026
edge rect=R8 kind=quad side=bottom nominal=160.0000 hw=159.9625 hw-nominal=-0.0375 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0375
edge rect=R12 kind=quad side=left nominal=166.0000 hw=165.8547 hw-nominal=-0.1453 desmume=166.0000 desmume-nominal=+0.0000 hw-desmume=-0.1453
edge rect=R12 kind=quad side=right nominal=182.0000 hw=181.9592 hw-nominal=-0.0408 desmume=182.0000 desmume-nominal=+0.0000 hw-desmume=-0.0408
edge rect=R12 kind=quad side=top nominal=32.0000 hw=31.9213 hw-nominal=-0.0787 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-1.0787
edge rect=R12 kind=quad side=bottom nominal=160.0000 hw=159.9213 hw-nominal=-0.0787 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0787
edge rect=R16 kind=quad side=left nominal=184.0000 hw=183.8954 hw-nominal=-0.1046 desmume=184.0000 desmume-nominal=+0.0000 hw-desmume=-0.1046
edge rect=R16 kind=quad side=right nominal=200.0000 hw=199.9235 hw-nominal=-0.0765 desmume=200.0000 desmume-nominal=+0.0000 hw-desmume=-0.0765
edge rect=R16 kind=quad side=top nominal=32.0000 hw=31.9359 hw-nominal=-0.0641 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-1.0641
edge rect=R16 kind=quad side=bottom nominal=160.0000 hw=159.9072 hw-nominal=-0.0928 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0928
edge rect=R20 kind=quad side=left nominal=202.0000 hw=201.8840 hw-nominal=-0.1160 desmume=202.0000 desmume-nominal=+0.0000 hw-desmume=-0.1160
edge rect=R20 kind=quad side=right nominal=218.0000 skipped=plateau level not observed on its side (left True, right False)
edge rect=R20 kind=quad side=top nominal=32.0000 hw=31.9107 hw-nominal=-0.0893 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-1.0893
edge rect=R20 kind=quad side=bottom nominal=160.0000 hw=159.9107 hw-nominal=-0.0893 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0893
edge rect=R24 kind=quad side=left nominal=220.0000 skipped=plateau level not observed on its side (left False, right True)
edge rect=R24 kind=quad side=right nominal=236.0000 hw=235.9170 hw-nominal=-0.0830 desmume=236.0000 desmume-nominal=+0.0000 hw-desmume=-0.0830
edge rect=R24 kind=quad side=top nominal=32.0000 hw=31.9226 hw-nominal=-0.0774 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-1.0774
edge rect=R24 kind=quad side=bottom nominal=160.0000 hw=159.9175 hw-nominal=-0.0825 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0825
edge rect=R28 kind=quad side=left nominal=238.0000 hw=237.8582 hw-nominal=-0.1418 desmume=238.0000 desmume-nominal=+0.0000 hw-desmume=-0.1418
edge rect=R28 kind=quad side=right nominal=254.0000 hw=253.8434 hw-nominal=-0.1566 desmume=254.0000 desmume-nominal=+0.0000 hw-desmume=-0.1566
edge rect=R28 kind=quad side=top nominal=32.0000 hw=31.9340 hw-nominal=-0.0660 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-1.0660
edge rect=R28 kind=quad side=bottom nominal=160.0000 hw=159.9269 hw-nominal=-0.0731 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0731
stat x_left hw-nominal mean=-0.1069 std=0.0553 n=13
stat x_left desmume-nominal mean=+0.0000 std=0.0000 n=14
stat x_left hw-desmume mean=-0.1069 std=0.0553 n=13
stat x_right hw-nominal mean=-0.0879 std=0.0348 n=13
stat x_right desmume-nominal mean=+0.0000 std=0.0000 n=14
stat x_right hw-desmume mean=-0.0879 std=0.0348 n=13
stat y_top hw-nominal mean=-0.0734 std=0.0250 n=14
stat y_top desmume-nominal mean=+1.0000 std=0.0000 n=14
stat y_top hw-desmume mean=-1.0734 std=0.0250 n=14
stat y_bottom hw-nominal mean=-0.0772 std=0.0229 n=14
stat y_bottom desmume-nominal mean=+1.0000 std=0.0000 n=14
stat y_bottom hw-desmume mean=-1.0772 std=0.0229 n=14
stat width hw-nominal mean=+0.0179 std=0.0575 n=12
stat width desmume-nominal mean=+0.0000 std=0.0000 n=14
stat width hw-desmume mean=+0.0179 std=0.0575 n=12
stat height hw-nominal mean=-0.0038 std=0.0460 n=14
stat height desmume-nominal mean=+0.0000 std=0.0000 n=14
stat height hw-desmume mean=-0.0038 std=0.0460 n=14
```

### 3.5 Alternative mapping (EQ-4g `fit_mapping.py` fit)

Temporary mapping (`docs/reports/eq4g-capture-offset.md` §4) and the complete
`--mapping "$O/fit.json" --desmume "$O/h.ppm" <fixture>` output:

```
$ printf '{"scale_x":1.250383,"scale_y":1.250790,"offset_x":39.933172,"offset_y":-0.168453}\n' > "$O/fit.json"
$ cat "$O/fit.json"
{"scale_x":1.250383,"scale_y":1.250790,"offset_x":39.933172,"offset_y":-0.168453}
$ python3 tools/ds_alpha_test/measure_edges.py --mapping "$O/fit.json" --desmume "$O/h.ppm" tools/ds_alpha_test/fixtures/al1_3_top400x240.png
mode=compare
mapping scale_x=1.25038 scale_y=1.25079 offset_x=39.9332 offset_y=-0.168453
backdrop black level=0.0000
backdrop grey level=132.6667
edges_measured=57 edges_skipped=7
edge rect=black kind=backdrop side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=black kind=backdrop side=right nominal=128.0000 hw=127.9541 hw-nominal=-0.0459 desmume=128.0000 desmume-nominal=+0.0000 hw-desmume=-0.0459
edge rect=black kind=backdrop side=top nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=black kind=backdrop side=bottom nominal=192.0000 skipped=on the DS screen boundary (capture crop)
edge rect=grey kind=backdrop side=left nominal=128.0000 skipped=backdrop left/top edge is the screen boundary or the duplicate side of an internal boundary
edge rect=grey kind=backdrop side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=grey kind=backdrop side=top nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=grey kind=backdrop side=bottom nominal=192.0000 skipped=on the DS screen boundary (capture crop)
edge rect=L4 kind=quad side=left nominal=2.0000 hw=2.0375 hw-nominal=+0.0375 desmume=2.0000 desmume-nominal=+0.0000 hw-desmume=+0.0375
edge rect=L4 kind=quad side=right nominal=18.0000 hw=17.9185 hw-nominal=-0.0815 desmume=18.0000 desmume-nominal=+0.0000 hw-desmume=-0.0815
edge rect=L4 kind=quad side=top nominal=32.0000 hw=32.0981 hw-nominal=+0.0981 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-0.9019
edge rect=L4 kind=quad side=bottom nominal=160.0000 hw=159.8969 hw-nominal=-0.1031 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.1031
edge rect=L8 kind=quad side=left nominal=20.0000 hw=19.9826 hw-nominal=-0.0174 desmume=20.0000 desmume-nominal=+0.0000 hw-desmume=-0.0174
edge rect=L8 kind=quad side=right nominal=36.0000 hw=35.9695 hw-nominal=-0.0305 desmume=36.0000 desmume-nominal=+0.0000 hw-desmume=-0.0305
edge rect=L8 kind=quad side=top nominal=32.0000 hw=32.0601 hw-nominal=+0.0601 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-0.9399
edge rect=L8 kind=quad side=bottom nominal=160.0000 hw=159.9607 hw-nominal=-0.0393 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0393
edge rect=L12 kind=quad side=left nominal=38.0000 hw=37.8635 hw-nominal=-0.1365 desmume=38.0000 desmume-nominal=+0.0000 hw-desmume=-0.1365
edge rect=L12 kind=quad side=right nominal=54.0000 hw=53.9275 hw-nominal=-0.0725 desmume=54.0000 desmume-nominal=+0.0000 hw-desmume=-0.0725
edge rect=L12 kind=quad side=top nominal=32.0000 hw=32.0380 hw-nominal=+0.0380 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-0.9620
edge rect=L12 kind=quad side=bottom nominal=160.0000 hw=159.9534 hw-nominal=-0.0466 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0466
edge rect=L16 kind=quad side=left nominal=56.0000 hw=55.9482 hw-nominal=-0.0518 desmume=56.0000 desmume-nominal=+0.0000 hw-desmume=-0.0518
edge rect=L16 kind=quad side=right nominal=72.0000 hw=71.9618 hw-nominal=-0.0382 desmume=72.0000 desmume-nominal=+0.0000 hw-desmume=-0.0382
edge rect=L16 kind=quad side=top nominal=32.0000 hw=32.0547 hw-nominal=+0.0547 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-0.9453
edge rect=L16 kind=quad side=bottom nominal=160.0000 hw=159.9565 hw-nominal=-0.0435 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0435
edge rect=L20 kind=quad side=left nominal=74.0000 hw=73.8530 hw-nominal=-0.1470 desmume=74.0000 desmume-nominal=+0.0000 hw-desmume=-0.1470
edge rect=L20 kind=quad side=right nominal=90.0000 hw=89.9162 hw-nominal=-0.0838 desmume=90.0000 desmume-nominal=+0.0000 hw-desmume=-0.0838
edge rect=L20 kind=quad side=top nominal=32.0000 hw=32.0466 hw-nominal=+0.0466 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-0.9534
edge rect=L20 kind=quad side=bottom nominal=160.0000 hw=159.9596 hw-nominal=-0.0404 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0404
edge rect=L24 kind=quad side=left nominal=92.0000 hw=91.9425 hw-nominal=-0.0575 desmume=92.0000 desmume-nominal=+0.0000 hw-desmume=-0.0575
edge rect=L24 kind=quad side=right nominal=108.0000 hw=107.9405 hw-nominal=-0.0595 desmume=108.0000 desmume-nominal=+0.0000 hw-desmume=-0.0595
edge rect=L24 kind=quad side=top nominal=32.0000 hw=32.0428 hw-nominal=+0.0428 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-0.9572
edge rect=L24 kind=quad side=bottom nominal=160.0000 hw=159.9536 hw-nominal=-0.0464 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0464
edge rect=L28 kind=quad side=left nominal=110.0000 hw=109.8471 hw-nominal=-0.1529 desmume=110.0000 desmume-nominal=+0.0000 hw-desmume=-0.1529
edge rect=L28 kind=quad side=right nominal=126.0000 hw=125.8888 hw-nominal=-0.1112 desmume=126.0000 desmume-nominal=+0.0000 hw-desmume=-0.1112
edge rect=L28 kind=quad side=top nominal=32.0000 hw=32.0455 hw-nominal=+0.0455 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-0.9545
edge rect=L28 kind=quad side=bottom nominal=160.0000 hw=159.9646 hw-nominal=-0.0354 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0354
edge rect=R4 kind=quad side=left nominal=130.0000 hw=130.0136 hw-nominal=+0.0136 desmume=130.0000 desmume-nominal=+0.0000 hw-desmume=+0.0136
edge rect=R4 kind=quad side=right nominal=146.0000 hw=145.9587 hw-nominal=-0.0413 desmume=146.0000 desmume-nominal=+0.0000 hw-desmume=-0.0413
edge rect=R4 kind=quad side=top nominal=32.0000 hw=31.9812 hw-nominal=-0.0188 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-1.0188
edge rect=R4 kind=quad side=bottom nominal=160.0000 hw=159.9973 hw-nominal=-0.0027 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0027
edge rect=R8 kind=quad side=left nominal=148.0000 hw=147.9056 hw-nominal=-0.0944 desmume=148.0000 desmume-nominal=+0.0000 hw-desmume=-0.0944
edge rect=R8 kind=quad side=right nominal=164.0000 hw=163.9650 hw-nominal=-0.0350 desmume=164.0000 desmume-nominal=+0.0000 hw-desmume=-0.0350
edge rect=R8 kind=quad side=top nominal=32.0000 hw=32.0120 hw-nominal=+0.0120 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-0.9880
edge rect=R8 kind=quad side=bottom nominal=160.0000 hw=159.9961 hw-nominal=-0.0039 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0039
edge rect=R12 kind=quad side=left nominal=166.0000 hw=165.8574 hw-nominal=-0.1426 desmume=166.0000 desmume-nominal=+0.0000 hw-desmume=-0.1426
edge rect=R12 kind=quad side=right nominal=182.0000 hw=181.9569 hw-nominal=-0.0431 desmume=182.0000 desmume-nominal=+0.0000 hw-desmume=-0.0431
edge rect=R12 kind=quad side=top nominal=32.0000 hw=32.0358 hw-nominal=+0.0358 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-0.9642
edge rect=R12 kind=quad side=bottom nominal=160.0000 hw=159.9550 hw-nominal=-0.0450 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0450
edge rect=R16 kind=quad side=left nominal=184.0000 hw=183.8925 hw-nominal=-0.1075 desmume=184.0000 desmume-nominal=+0.0000 hw-desmume=-0.1075
edge rect=R16 kind=quad side=right nominal=200.0000 hw=199.9157 hw-nominal=-0.0843 desmume=200.0000 desmume-nominal=+0.0000 hw-desmume=-0.0843
edge rect=R16 kind=quad side=top nominal=32.0000 hw=32.0504 hw-nominal=+0.0504 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-0.9496
edge rect=R16 kind=quad side=bottom nominal=160.0000 hw=159.9409 hw-nominal=-0.0591 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0591
edge rect=R20 kind=quad side=left nominal=202.0000 hw=201.8756 hw-nominal=-0.1244 desmume=202.0000 desmume-nominal=+0.0000 hw-desmume=-0.1244
edge rect=R20 kind=quad side=right nominal=218.0000 skipped=plateau level not observed on its side (left True, right False)
edge rect=R20 kind=quad side=top nominal=32.0000 hw=32.0252 hw-nominal=+0.0252 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-0.9748
edge rect=R20 kind=quad side=bottom nominal=160.0000 hw=159.9443 hw-nominal=-0.0557 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0557
edge rect=R24 kind=quad side=left nominal=220.0000 skipped=plateau level not observed on its side (left False, right True)
edge rect=R24 kind=quad side=right nominal=236.0000 hw=235.8982 hw-nominal=-0.1018 desmume=236.0000 desmume-nominal=+0.0000 hw-desmume=-0.1018
edge rect=R24 kind=quad side=top nominal=32.0000 hw=32.0395 hw-nominal=+0.0395 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-0.9605
edge rect=R24 kind=quad side=bottom nominal=160.0000 hw=159.9512 hw-nominal=-0.0488 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0488
edge rect=R28 kind=quad side=left nominal=238.0000 hw=237.8388 hw-nominal=-0.1612 desmume=238.0000 desmume-nominal=+0.0000 hw-desmume=-0.1612
edge rect=R28 kind=quad side=right nominal=254.0000 hw=253.8191 hw-nominal=-0.1809 desmume=254.0000 desmume-nominal=+0.0000 hw-desmume=-0.1809
edge rect=R28 kind=quad side=top nominal=32.0000 hw=32.0485 hw-nominal=+0.0485 desmume=33.0000 desmume-nominal=+1.0000 hw-desmume=-0.9515
edge rect=R28 kind=quad side=bottom nominal=160.0000 hw=159.9605 hw-nominal=-0.0395 desmume=161.0000 desmume-nominal=+1.0000 hw-desmume=-1.0395
stat x_left hw-nominal mean=-0.0878 std=0.0641 n=13
stat x_left desmume-nominal mean=+0.0000 std=0.0000 n=14
stat x_left hw-desmume mean=-0.0878 std=0.0641 n=13
stat x_right hw-nominal mean=-0.0741 std=0.0400 n=13
stat x_right desmume-nominal mean=+0.0000 std=0.0000 n=14
stat x_right hw-desmume mean=-0.0741 std=0.0400 n=13
stat y_top hw-nominal mean=+0.0413 std=0.0250 n=14
stat y_top desmume-nominal mean=+1.0000 std=0.0000 n=14
stat y_top hw-desmume mean=-0.9587 std=0.0250 n=14
stat y_bottom hw-nominal mean=-0.0435 std=0.0229 n=14
stat y_bottom desmume-nominal mean=+1.0000 std=0.0000 n=14
stat y_bottom hw-desmume mean=-1.0435 std=0.0229 n=14
stat width hw-nominal mean=+0.0130 std=0.0575 n=12
stat width desmume-nominal mean=+0.0000 std=0.0000 n=14
stat width hw-desmume mean=+0.0130 std=0.0575 n=12
stat height hw-nominal mean=-0.0848 std=0.0460 n=14
stat height desmume-nominal mean=+0.0000 std=0.0000 n=14
stat height hw-desmume mean=-0.0848 std=0.0460 n=14
```

The conclusion does not depend on the mapping: x stays at about -0.07 to -0.09
DS px and y at about -1.0 (hw-minus-DeSmuME `-0.9587`/`-1.0435`) under both,
an order of magnitude more in y than EQ-4g's <=0.135 DS px mapping difference.

## 4. Per-quad values (hardware, committed mapping)

The per-quad `hw-nominal` values are the `hw-nominal=` fields of §3.4 (14 quads
× four sides; R20 right and R24 left skipped), and §5 aggregates them per axis.
The one measured backdrop boundary (nominal x=128) is `hw=127.9399` (`-0.0601`)
in hardware and `128.0000` in DeSmuME. DeSmuME is exactly nominal on
left/right/width/height and exactly `+1.0000` on top/bottom for every quad
(§3.2).

## 5. Per-axis statistics (from §3.4 and §3.5)

Mean ± population standard deviation (and ± std/sqrt(n)); n = measured edges.

| class | hw-nominal | hw-DesmuME | n |
|---|---|---|---|
| left | -0.1069 ± 0.0553 (-0.1069 ± 0.0153) | -0.1069 ± 0.0553 | 13 |
| right | -0.0879 ± 0.0348 (-0.0879 ± 0.0097) | -0.0879 ± 0.0348 | 13 |
| top | -0.0734 ± 0.0250 (-0.0734 ± 0.0067) | -1.0734 ± 0.0250 (-1.0734 ± 0.0067) | 14 |
| bottom | -0.0772 ± 0.0229 (-0.0772 ± 0.0061) | -1.0772 ± 0.0229 (-1.0772 ± 0.0061) | 14 |
| width | +0.0179 ± 0.0575 (+0.0179 ± 0.0166) | +0.0179 ± 0.0575 | 12 |
| height | -0.0038 ± 0.0460 (-0.0038 ± 0.0123) | -0.0038 ± 0.0460 | 14 |


## 6. Interpretation

**FACT (quoted output only).**

* Under the committed mapping, hardware 3D edges land at the exact `quads.json`
  positions within about 0.1 DS px: the largest per-class mean error is
  `-0.1069` DS px; width and height change by `+0.0179 ± 0.0575` and
  `-0.0038 ± 0.0460` DS px. This residual is mapping-relative: the committed
  mapping was fitted by `fit_mapping.py` against another ROM's nominal geometry,
  so a uniform hardware raster shift would be absorbed into `offset_x/offset_y`.
* DeSmuME is exactly nominal on left/right (`0.0000 ± 0.0000`), exactly
  `+1.0000` on top/bottom (`1.0000 ± 0.0000`) and `0.0000` on width/height.
* Against DeSmuME, hardware is `-0.1069 ± 0.0553` (left), `-0.0879 ± 0.0348`
  (right), `-1.0734 ± 0.0250` (top) and `-1.0772 ± 0.0229` (bottom) DS px; the y
  difference is about ten times x and survives the alternative mapping
  (`-0.9587`/`-1.0435`).
* The residual x error (about -0.09 DS px) matches EQ-4g's mapping uncertainty
  (<=0.135 DS px) and flips sign with the mapping (y becomes
  `+0.0413`/`-0.0435`), so this probe does not resolve it. A symmetric resampling
  blur would inflate width; the observed width/height change is none beyond
  noise, and the one-sided y shift is a translation, not blur. The DeSmuME frame
  is not resampled (std 0), so `hw-DesmuME = -1.07` is not blur.

**HYPOTHESIS.** The hardware rasteriser places axis-aligned quad edges at
nominal while DeSmuME's places the horizontal edges one DS pixel lower and the
vertical edges on nominal, so the ~1 px dy-negative offset of EQ-4f/EQ-4g is a
real vertical placement difference **between hardware and DeSmuME**, not a
hardware shift, not a capture-mapping artefact and not resampling blur (it fits
"`dy` negative for all class centroids, `dx` mixed").

**UNVERIFIED.** This probe cannot separate an absolute hardware raster shift
from the fitted mapping offset: `offset_x/offset_y` were fitted against another
ROM's nominal geometry, so a uniform hardware raster shift would be absorbed
there. Only the cross-ROM mapping consistency (§3.5's alternative-mapping run)
and the hw-minus-DeSmuME difference are measured. Nor can it say *which side* is
displaced: the ROM has no horizontal edge at the very top/bottom scanline (the
backdrops fill the screen), so a whole-frame one-row DeSmuME shift is
indistinguishable from rasterising the quads one row low. It also does not
cover other consoles/sessions, the true resampler kernel, textured or
non-axis-aligned geometry, the PICA arm for this ROM, or the DS rasteriser's
rounding convention.

## 7. What would separate the remaining causes

* Render the same ROM with a second emulator (e.g. melonDS) and the repo's PICA
  path: a +1 row in every emulator points at a rasteriser convention, a +1 in
  PICA only at the repo renderer.
* Add a one-row marker at the very top/bottom scanline to anchor the frame
  origin, separating "quads one pixel low" from "whole frame one row low".
* Move the quads by +-1 DS px in y; test a second console/session, a rotated
  quad and a textured quad.

## 8. Basis and limits

One console (one New 3DS, DS mode), one capture session, one ROM, one DeSmuME
harness render built from the repo this round; the hardware capture is fixture
`tools/ds_alpha_test/fixtures/al1_3_top400x240.png`
(sha256 `d6267d821d8492ff5f91c58e1065af0094f054c39c18cdb55d6af81f78ef4232`).
The mapping is the committed `capture_mapping.json`; the alternative is the
EQ-4g fit from another ROM's capture, so using it assumes the mapping transfers
across captures of the same console/session (the conclusion does not depend on
it). The committed mapping was likewise fitted against another ROM's nominal
geometry, so the §3.4 hw-vs-nominal residuals are mapping-relative and cannot
separate an absolute hardware raster shift from the fitted offset; only the
cross-ROM mapping consistency (§3.5) and the hw-minus-DeSmuME difference (§3.4)
are mapping-independent. The "all 56 quad edges clear the contrast threshold"
statement holds for this ROM's `[31,31,31]` white over `[0,0,0]`/`[16,16,16]`
backdrops; a darker quad could fall below `MIN_CONTRAST = 10`.

Tools and tests: `tools/ds_alpha_test/measure_edges.py` and
`tools/ds_alpha_test/test_measure_edges.py` (16 tests); the root
`test-ds-alpha-test` target discovers `test_*.py`, so `make test` runs them.

## 9. Size audit

FACT — the total diff against `main` is below the 90 KB limit:

```
$ git diff main | wc -c
89987
```
