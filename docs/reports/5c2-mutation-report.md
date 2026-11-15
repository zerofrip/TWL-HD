# 5C-2 mutation report — `twlhd_gx_rec_serialize_tex` + `twlhd_tex_source`

**Date:** 2026-11-15 (repair round 2)  
**Scope:** new code only

- `common/twlhd_gx_record.c` between the `BEGIN 5C-2 serialize_tex` and
  `END 5C-2 serialize_tex` markers (the new `twlhd_gx_rec_serialize_tex`
  function).
- `common/twlhd_tex_source.c` (whole file).

Pre-existing `twlhd_gx_rec_serialize()` and other code outside the markers
are not mutated.

**Command line:**

```bash
python3 docs/reports/5c2_mutate_gx_record_tex.py > docs/reports/5c2-mutation-run.txt 2>&1
```

The runner defaults to `max_workers = os.cpu_count()` and gives every compile
and every test run a 20-second timeout. It writes each mutant into its own
directory under `tests/host/build/mut/` and never modifies the real files in
`common/`.

**Test binary:** `tests/host/build/test_gx_record_tex` (no CLI/golden step for
this module).

## Results

| Class | Count |
|---|---|---|
| Killed by test | 52 |
| Killed by build | 6 |
| Killed by timeout | 0 |
| Survived | 7 |
| **Total** | **65** |

Kill distribution:

| Step | Count | Share |
|---|---|---|
| C test | 52 | 80.0 % |
| Build | 6 | 9.2 % |
| Timeout | 0 | 0.0 % |
| Survived | 7 | 10.8 % |

## Survivor classification

Each survivor is explained below with a concrete argument. Line numbers are
from the delivered source.

### `common/twlhd_tex_source.c`

| Line | Mutant | Classification |
|---|---|---|
| 20 | `if (!ctx \|\| !ram) return 2;` | **Equivalent under the API contract.** The function promises only `0 = success, nonzero = error`; callers (including the test harness) check `!= 0`. The magnitude of the error return is not observable. |
| 22 | `if (length == 0u) return 2;` | **Equivalent under the API contract.** Same reasoning: any nonzero return rejects a zero-length registration. |
| 37 | `if (!src) return 2;` | **Equivalent under the API contract.** Same reasoning for a null read context. |

### `common/twlhd_gx_record.c` (new serialize function)

| Line | Mutant | Classification |
|---|---|---|
| 201 | `if (cap_result != TWLHD_TEX_CAP_ERR_NULL)` → `if (cap_result == ...)` | **Equivalent (unreachable branch).** `twlhd_tex_capture_plan()` cannot return `ERR_NULL` here: `rec`, `limits` and `read` are validated before the call, and `rec->gxcm` is never NULL. The branch is therefore never taken. |
| 202 | `if (cap_result == TWLHD_TEX_CAP_ERR_CAPACITY)` → `if (cap_result != ...)` | **Equivalent (unreachable branch).** `twlhd_tex_capture_plan()` never returns `ERR_CAPACITY`; it drops records instead. The branch is never taken. |
| 202 | `return TWLHD_V2_ERR_SIZE;` → `return TWLHD_V2_ERR_NULL;` | **Equivalent (unreachable branch).** Same as above: the capacity branch is unreachable. |
| 203 | `return TWLHD_V2_ERR_TXDS;` → `return TWLHD_V2_ERR_NULL;` | **Equivalent (unreachable branch).** The fallback is only reachable if `plan()` returns an error other than `ERR_NULL` or `ERR_CAPACITY`, which it does not. |

## Newly killed mutants

These mutants either did not exist before the limits check or changed
classification because the targeted-mutant needles shifted onto the new code.

| Location | Mutant | Killing test |
|---|---|---|
| `twlhd_gx_record.c:193` | `>` → `>=` on `max_txdt_bytes` | every default-limits success test (`test_one_textured_quad`, `test_large_texture_budget`, etc.) — default `max_txdt_bytes == TWLHD_GX_REC_TEX_TXDT_MAX`, so `>=` wrongly rejects it. |
| `twlhd_gx_record.c:193` | `\|\|` → `&&` | `test_limits_above_scratch_caps` TXDT case: with `&&`, the TXDT limit is over-cap but the PLDT limit is not, so the check does not fire and the test expects `ERR_SIZE`. |
| `twlhd_gx_record.c:194` | `>` → `>=` on `max_pldt_bytes` | `test_limits_above_scratch_caps` PLDT case: default `max_pldt_bytes == TWLHD_GX_REC_TEX_PLDT_MAX`, so `>=` wrongly rejects it. Also killed by any default-limits success path that exercises a palette. |
| `twlhd_gx_record.c:194` | `TWLHD_GX_REC_TEX_PLDT_MAX` → `TWLHD_GX_REC_TEX_TXDT_MAX` (targeted "serialize apply PLDT capacity -> TXDT capacity") | Previously classified as equivalent under default budgets; the targeted needle now lands on the new limits check at line 194, and `test_limits_above_scratch_caps` PLDT case catches it (`32769 > 131072` is false, so the call wrongly succeeds). |

## Mutants killed in earlier rounds and still killed

| Location | Mutant | Killing test |
|---|---|---|
| `twlhd_tex_source.c:39` | `for (i = 0; i <= src->count; i++)` | `test_source_oob_full_table` (ASan abort on `regions[16]`) and `test_source_poison_slot` (poisoned `regions[count]`). |
| `twlhd_gx_record.c:210` | `TWLHD_V2_TXDS_MAX_RECORDS - 1u` entries capacity | `test_256_distinct_keys` (256 kept keys need the full 256-entry table). |
| `twlhd_gx_record.c:214` | `return TWLHD_V2_ERR_TXDT;` → `return TWLHD_V2_ERR_NULL;` | `test_apply_read_error_txdt` (stateful mock succeeds on probe, fails on copy). |

## Notes

- The generic mutant generator produced constant and operator-swap mutants
  inside the scoped function and the whole backend file. The targeted set added
  pinned mutations for kind/base/length routing, the region-table bound, the
  overflow predicate, the file-flag OR, and error-code returns.
- The new limits validation in `twlhd_gx_rec_serialize_tex` (repair-round-2
  defect 3, lines 193-196) is a **sanctioned production behaviour change**.
  It rejects `limits->max_txdt_bytes > TWLHD_GX_REC_TEX_TXDT_MAX` or
  `limits->max_pldt_bytes > TWLHD_GX_REC_TEX_PLDT_MAX` up front with
  `TWLHD_V2_ERR_SIZE`, leaving `out`, `*out_size`, `*stats_out` and recorder
  flags untouched. Before this check was added, `test_limits_above_scratch_caps`
  failed (both TXDT and PLDT cases returned `TWLHD_V2_OK`); adding the check
  made the test pass, confirming the test bites.
  This check added three operator-swap mutants (`>`→`>=` ×2,
  `||`→`&&`) and shifted the targeted mutant needles; all are killed by the
  strengthened/extended tests.
- A missing targeted mutant substring aborts the runner with exit code 2, so a
  source change cannot silently shrink the mutant set.
- The runner restores the original `common/twlhd_gx_record.c` and
  `common/twlhd_tex_source.c` contents after the run; the final line of the run
  log is `common/ restored: YES`.
