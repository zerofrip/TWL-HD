# EQ-5i: interpreter options from an SD file (`sdmc:/twlhd_opts.txt`)

Labels: FACT = measured here; SOURCE-READ = read in source/docs; INFERENCE = derived from
FACT items; HYPOTHESIS; UNVERIFIED. Only numbers are recorded.

## Motivation
SOURCE-READ (EQ-5h report): retail frames need DS stack + culling (0x3u), while five of the
repository's older synthetic frames are blank with culling. A build-time constant forces a
rebuild per kind of frame.

## Change
- `common/twlhd_opts.[ch]` (pure C, host-tested): `twlhd_opts_parse()` reads a small text
  file. Lines `gx_opts = <decimal|0xhex>` or `gx = cull ds_stack none` (words separated by
  space or comma), `#` comments, blank lines; last line wins; unknown key, word, bit or a
  malformed number is an error and leaves the output untouched; at most 512 bytes.
- `arm11/source/main.c`: `load_opts_file()` runs at every frame load; a missing file keeps
  the compile-time default `TWLHD_ARM11_GX_OPTS` (0u), an invalid file keeps the default and
  writes the trace line "opts file invalid"; the interpreter now receives `g_gx_opts`.
- `tests/host/test_opts.c` (8 tests) is registered in the Makefile.
FACT: `make test` exits 0 (all suites, including the 8 new ones); `make -C arm11` builds.

## Check through Azahar (build with the default option 0)
| SD contents | frame | non-dark px (top screen) | differs from |
|---|---|---|---|
| frame only | NSMB stage select (swap 5320) | 8162 (built-in scene) | 95968 px from the 0x3u render |
| `gx = cull ds_stack` | same | 95967 | 0 px from the 0x3u render of EQ-5d |
| `gx_opts = 0x3  # same` | same | 95967 | 0 px (identical to the previous row) |
| `gx_opts = banana` | same | 8162 | 0 px from the frame-only run |
| frame only | c100 (synthetic) | 18506 | - |
| `gx = cull ds_stack` | c100 | 0 | 18506 px from the frame-only run |
| `gx = none` | c100 | 18506 | 0 px from the frame-only run |
FACT: the options file switches one build between the retail replay and the old frames.
FACT: an invalid file behaves like no file.
UNVERIFIED: the console (the file is read with `fopen` like the frame file; not run on
hardware); the "opts file invalid" trace line was not read in the Azahar log.

## Usage
Put a one-line `twlhd_opts.txt` next to `twlhd_frame.bin` on the SD card, for example
`gx = cull ds_stack` for retail frames. Remove the file (or write `gx = none`) for the older
test frames.
