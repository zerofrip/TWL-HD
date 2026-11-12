# 5B-1 lighting tooling — mutation evidence

Date: 2026-11-12. Scope: `tools/hw/gen_lit_frames.py`,
`tools/hw/predict_lit.py`, `tools/hw/compare_ppm.py`. Host-only, offline;
no console or network access.

## Method

Scratch copies, never the worktree:

1. A base copy is made under `/tmp/opencode/hw_mut_base`: every `tools/*.py`
   and `tools/hw/*.py`, with `common/` and `tests/` symlinked to the worktree
   (the tests compile `tests/host/v2_dump_cli.c` against `common/`).
2. `common/` and `tests/` are read-only through the symlinks; no repository
   file is edited.
3. For each mutant one textual change is applied to a fresh copy under
   `/tmp/opencode/hw_mut_work`, then the new suite is run from that copy:

   ```sh
   python3 -B -I -m unittest discover -s /tmp/opencode/hw_mut_work/tools \
       -p 'test_hw_lit_*.py'
   ```

4. A mutant is KILLED when that command exits non-zero. The table records the
   first failing test reported by unittest.

Baseline (unmutated scratch copy): exit 0, **27 tests, OK**.

Result: **36 mutants, 36 killed, 0 survived.**

Two candidates were discarded before the final table because they were
equivalent mutants: changing only the `x` component of `NORMAL_60`, or only the
`x` component of `LIGHT_VECTOR 1`, cannot change `-dot(L,N)` when the light
direction and normal both lie in the `z` plane. They were replaced by `z`
component changes, which are killed.

## Mutants — frame generator (`gen_lit_frames.py`)

| # | Mutant | Status | Killer |
|---|---|---|---|
| 1 | `NORMAL_FACING` becomes facing away (`511 << 20` -> `513 << 20`) | KILLED | `test_lit_a_incidence_ordering` |
| 2 | lit_a diffuse (22,22,22) -> (20,20,20) | KILLED | `test_lit_a_matches_hand_computed_integers` (quad 0) |
| 3 | lit_a ambient (4,4,4) -> (5,5,5) | KILLED | `test_lit_a_incidence_ordering` |
| 4 | light direction z `-4088` -> `-4000` | KILLED | `test_lit_a_matches_hand_computed_integers` (quad 0) |
| 5 | emission (4,28,4) -> (5,28,4) | KILLED | `test_lit_b_expected_colours_and_spread` |
| 6 | shininess table all zero | KILLED | `test_lit_b_expected_colours_and_spread` |
| 7 | 60-degree normal z field 256 -> 300 | KILLED | `test_lit_a_matches_hand_computed_integers` (quad 1) |
| 8 | control quad 0 red 31 -> 30 | KILLED | `test_lit_ctl_flat_colours` |
| 9 | lit_a alpha 31 -> 16 | KILLED | `test_alpha_is_31_for_lit_frames` |
| 10 | two-light mask 0x3 -> 0x1 | KILLED | `test_lit_b_expected_colours_and_spread` |
| 11 | second quad corner x 3840 -> 3832 | KILLED | `test_pixel_mapping_literals` |
| 12 | specular quad light colour white -> red | KILLED | `test_lit_b_expected_colours_and_spread` |
| 13 | light 1 direction z `-3544` -> `-3000` | KILLED | `test_lit_b_expected_colours_and_spread` |

## Mutants — predictor (`predict_lit.py`)

| # | Mutant | Status | Killer |
|---|---|---|---|
| 14 | colour rounding loses `+0.5` | KILLED | `test_lit_a_incidence_ordering` |
| 15 | drop the top-screen row flip (`239 - fb_x` -> `fb_x`) | KILLED | `test_fixture_mapping_matches_run7_orientation` |
| 16 | flip the `y'` sign (`1 - y'` -> `1 + y'`) | KILLED | `test_fixture_mapping_matches_run7_orientation` |
| 17 | drop the 0.8 pillarbox factor (use 1.0) | KILLED | `test_pixel_mapping_literals` |
| 18 | `fb_x` scale uses 400 instead of 240 | KILLED | `test_background_out_of_tolerance_fails` |
| 19 | render-target height 400 -> 399 | KILLED | `test_pixel_mapping_literals` |
| 20 | clear colour 24 -> 25 | KILLED | `test_document_structure` |
| 21 | background centre moved 12 -> 30 | KILLED | `test_document_structure` |
| 22 | viewport `hw` divides by 192 instead of 256 | KILLED | `test_pixel_mapping_literals` |
| 23 | `clip_centre` averages 3 components instead of 4 | KILLED | `test_document_structure` |
| 24 | quad stride 6 -> 3 | KILLED | `test_hw_lit_compare` setUpClass |
| 25 | alpha hard-coded 254 instead of 255 | KILLED | `test_lit_a_incidence_ordering` |

## Mutants — comparator (`compare_ppm.py`)

| # | Mutant | Status | Killer |
|---|---|---|---|
| 26 | background check removed | KILLED | `test_background_out_of_tolerance_fails` |
| 27 | quad out-of-tolerance check removed | KILLED | `test_blue_only_deviation_fails` |
| 28 | exit codes 0 and 1 swapped | KILLED | `test_background_out_of_tolerance_fails` |
| 29 | input-error exit 2 -> 1 | KILLED | `test_malformed_ppm_is_input_error` |
| 30 | patch-bounds check loses the 4-pixel radius | KILLED | `test_patch_outside_image_is_input_error` |
| 31 | truncation expected size drops `*3` | KILLED | `test_background_out_of_tolerance_fails` |
| 32 | deviation loses `abs()` | KILLED | `test_negative_deviation_beyond_tolerance_fails` |
| 33 | deviation checks only channel 0 | KILLED | `test_background_out_of_tolerance_fails` |
| 34 | sampled channels returned B,G,R | KILLED | `test_exact_prediction_passes` (lit_b) |
| 35 | PPM maxval accepts 254 only | KILLED | `test_background_out_of_tolerance_fails` |
| 36 | `--tol` ignored (hard-coded 12) | KILLED | `test_tolerance_option_rescues_deviation` |

## Notes

- The suite is the three `tools/test_hw_lit_*.py` files only; the rest of the
  Python suite is not needed to kill these mutants.
- `test_lit_a_matches_hand_computed_integers` recomputes the expected values
  with the integer DeSmuME equation (`>>12`, `>>17`, `>>5`, clamp 31, /31),
  not with the tool, so generator and predictor mutations are killed
  independently.
- The EMULATOR cross-check in `tools/hw/README.md` is separate evidence; this
  report is BUILD/UNIT mutation evidence.
