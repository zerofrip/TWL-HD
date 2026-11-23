# tools/fixtures

`formats_top_0_run20.ppm` — Run 20 hardware capture of the 8-quad texture-format
frame (`twlhd_frame_v2_formats.bin`), read-only input copied verbatim so the
tolerance regression test can use the real capture.

- origin: `$HOME/3ds/hwtest-staging/ntr_live/run20/formats_top_0.ppm`
  (New 3DS NTR live capture, Run 20, 2026-11-14 16:13; see
  `docs/PHASE5B_PLAN.md` "Run 20" and `docs/reports/tex-formats-tolerance.md`)
- size: 288015 bytes (400x240 P6 PPM, under the 300 KB fixture cap)
- sha256: `4a2451a3b760afac828888e881465834e532872524cb3968b9a9745bd2547988`

No SPDX header is possible on a binary PPM; licensing follows the repository
(Apache-2.0 for the tooling, the capture itself is test evidence from the
project's own console run).
