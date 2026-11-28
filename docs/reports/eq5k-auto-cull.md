# EQ-5k: automatic culling when a frame overflows the clip buffer

Labels: FACT = measured here; SOURCE-READ = read in source/docs; INFERENCE = derived from
FACT items; HYPOTHESIS; UNVERIFIED. Only numbers are recorded.

## Problem
SOURCE-READ (EQ-5a, EQ-5b, EQ-5h reports): a retail frame whose unculled interpretation
needs more than `CLIP_VERTEX_CAPACITY` (8192) vertices fell back to the built-in scene
unless culling was requested (NSMB stage select: 10707 unculled, 8046 culled vertices).

## Change
- `common/twlhd_gx_interp.[ch]`: `twlhd_gx_interpret_auto()` calls
  `twlhd_gx_interpret_opts()`; only if that fails with `TWLHD_GX_ERR_CAPACITY` and culling
  is not requested it interprets again with `TWLHD_GX_OPT_CULL` added and reports the flags
  used. Every other result, and every frame that fits, is unchanged. Statistics, viewport
  and output hold the second pass only.
- `arm11/source/main.c`: the interpreter call is `twlhd_gx_interpret_auto(..., g_gx_opts,
  &used_gx_opts)`; a retry writes the trace line "auto cull".
- `tests/host/test_gx_interp.c`: 4 new tests; `tests/host/test_opts.c`: the wiring check
  follows the new call.
FACT: `make test` exits 0.
FACT (mutation): replacing the retry's `flags |= TWLHD_GX_OPT_CULL` by `flags |= 0u` makes
3 of the 4 new tests fail (6 + 2 + 1 failed checks); the mutant was reverted.

## Check through Azahar (build with the default option 0, file `gx = ds_stack` only)
| frame | result |
|---|---|
| NSMB stage select (10707 unculled, 8046 culled) | 0 px differ from the render with `gx = cull ds_stack` (95967 non-dark px) |
| SM64DS 19180 (6171 unculled, fits) | 0 px differ from the main build compiled with option 0x2u |
| c100 (synthetic, no file) | 0 px differ from the earlier default render (18506 non-dark px) |
FACT: the three rows. INFERENCE: frames that fit are unaffected; an overflowing retail frame
renders without a culling entry in the options file.

## Open points
- UNVERIFIED: the console (same code path as the options file, not run on hardware).
- UNVERIFIED: a frame that overflows even after culling still falls back to the built-in
  scene (covered by a unit test of the error return, not rendered).
- SOURCE-READ (`arm11/source/main.c`): the "auto cull" trace is written whenever the retry
  ran, before the existing error handling, so it also appears before that fallback.
