# EQ-5g: the arm11 build on the console - SM64DS settled-scene frame

Labels: FACT = measured here; SOURCE-READ = read in source/docs; INFERENCE = derived from
FACT items; HYPOTHESIS; UNVERIFIED. Only numbers are recorded; game data stays local.

## Run (hardware, Run 22b)
FACT: same build and method as EQ-5f (`twlhd-arm11-gx3.3dsx`, main 138c6e5, options
DS stack + culling, netload, SD frame replaced and restored with read-back, A button,
NTR-HR capture of the top screen, columns 40..359). Frame: SM64DS swap 19180, the frame of
EQ-4zs/4zu (199116 B). Original SD file restored (1128 B, sha256 prefix 2decd74dead1044f).
The references are the earlier DS-mode captures of the same scene (4 captures through the
capture board, taken while the console ran the DS game) and the DeSmuME screenshot.

## Results (mean absolute difference R/G/B, 320x240)
| pair | R / G / B |
|---|---|
| console (arm11 app, NTR capture) vs Azahar render of the same frame and build | 5.8 / 3.7 / 3.3 |
| console (arm11 app) vs DS-mode hardware (mean of 4 captures) | 7.1 / 6.4 / 5.1 |
| Azahar render vs DS-mode hardware (mean of 4 captures) | 9.1 / 7.1 / 4.9 |
| console (arm11 app) vs DeSmuME screenshot | 8.5 / 6.7 / 5.5 |
| Azahar render vs DeSmuME screenshot | 8.0 / 6.1 / 4.7 |
FACT: the best alignment shift between console capture and Azahar render is (0, 0) over
a +-3 px search; the console capture has no black pixel.
FACT: the arm11 app on the console is closer to the DS-mode hardware capture in R and G
than the Azahar render is (7.1 vs 9.1, 6.4 vs 7.1) and slightly worse in B (5.1 vs 4.9).
INFERENCE: on this scene the console render agrees with the emulator render
(5.8 / 3.7 / 3.3) and with the DS hardware at the level of the EQ-4zu table (about 5-9).
HYPOTHESIS: the different capture paths (NTR-HR JPEG against the capture board) account
for part of the 1-2 level differences; not measured.

## Open points
- UNVERIFIED: other scenes and titles on the console with this build.
- UNVERIFIED: the unmodified 3DS-mode scaling of the NTR capture against the capture board
  was not calibrated separately in this run.
