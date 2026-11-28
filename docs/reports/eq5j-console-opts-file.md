# EQ-5j: the options file on the console

Labels: FACT = measured here; SOURCE-READ = read in source/docs; INFERENCE = derived from
FACT items; HYPOTHESIS; UNVERIFIED. Only numbers are recorded.

## Run (hardware, Run 22c)
FACT: arm11 build of main 74b0c6e with the default option 0 (EQ-5i), netloaded. SD: the
NSMB stage-select frame (swap 5320, 297356 B) as `/twlhd_frame.bin` and a new 19-byte file
`/twlhd_opts.txt` containing `gx = cull ds_stack` (both read back identical). A button
(input redirection), top screen captured through NTR-HR.
FACT: afterwards `/twlhd_opts.txt` was deleted, the original `/twlhd_frame.bin` was restored
(1128 B, sha256 prefix 2decd74dead1044f, read back identical) and the SD root listing of
twlhd files is the one seen before the run (`/twlhd_frame.bin`, `/twlhd_frame_v2.bin`).
No NAND, system or boot-chain access.

## Results
FACT: the console capture of this run is pixel-identical to the console capture of EQ-5f
(build with the compile-time option 0x3u): mean absolute difference 0.0 / 0.0 / 0.0 over
the 320x240 region.
FACT: against the Azahar render of the same frame: 7.5 / 5.1 / 6.5 (as in EQ-5f); no black
pixel.
INFERENCE: the options file is read on the console and selects the same interpretation as
the build constant; the default build needs no rebuild for retail frames.
UNVERIFIED: the "opts file invalid" path and the behaviour with the old synthetic frames on
the console.
