# EQ-5f: the arm11 build on the console - NSMB stage-select frame

Labels: FACT = measured here; SOURCE-READ = read in source/docs; INFERENCE = derived from
FACT items; HYPOTHESIS; UNVERIFIED. Only numbers are recorded; game data stays local.

## Run (hardware, Run 22)
FACT: New 3DS, build `twlhd-arm11-gx3.3dsx` from main 138c6e5 (option
`TWLHD_ARM11_GX_OPTS=0x3u`: DS stack model and culling; includes EQ-5d, the alpha-0
discard), netloaded with 3dslink. The SD file `/twlhd_frame.bin` was backed up (1128 B),
replaced by the dumped NSMB stage-select frame (swap 5320, 297356 B, read-back identical),
the app was loaded with the A button (input redirection) and the top screen was captured
through NTR-HR (400x240, columns 40..359 compared). Afterwards the original file was
restored (read-back identical). No NAND, system or boot-chain access.

## Results (mean absolute difference R/G/B over 320x240)
| pair | R / G / B |
|---|---|
| console capture vs Azahar render of the same frame and build | 7.5 / 5.1 / 6.4 |
| console capture vs DeSmuME 3D-only shot | 13.1 / 13.0 / 12.2 |
| Azahar render vs DeSmuME 3D-only shot | 13.7 / 12.7 / 10.3 |
FACT: the console capture contains no black pixel (0 px with all channels at most 2), so
the top strip of EQ-5b (4054 black px before EQ-5d) is gone on the console too.
FACT: the best alignment shift between console capture and Azahar render is (0, 0) over a
+-3 px search.
INFERENCE: the console renders this frame like the emulator (mean difference
7.5 / 5.1 / 6.4, best shift (0, 0)).
HYPOTHESIS: the remaining 5-7 levels between them come from the JPEG compression of the
NTR-HR capture (see the UNVERIFIED point below).
HYPOTHESIS: the console agrees with the DeSmuME 3D layer to the same degree as the emulator
does (about 13), so the residual of EQ-5e applies to the console as well.

## Open points
- UNVERIFIED: JPEG noise level of NTR-HR (no reference capture of a flat known image was
  taken in this run).
- UNVERIFIED: SM64DS and other scenes on the console with this build.
