# 5C-2 mutation report — `twlhd_gx_rec_serialize_tex` + `twlhd_tex_source`

**Date:** 2026-11-15 (repair round 1)  
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
| Killed by test | 48 |
| Killed by build | 6 |
| Killed by timeout | 0 |
| Survived | 8 |
| **Total** | **62** |

Kill distribution:

| Step | Count | Share |
|---|---|---|
| C test | 48 | 77.4 % |
| Build | 6 | 9.7 % |
| Timeout | 0 | 0.0 % |
| Survived | 8 | 12.9 % |

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
| 196 | `if (cap_result != TWLHD_TEX_CAP_ERR_NULL)` → `if (cap_result == ...)` | **Equivalent (unreachable branch).** `twlhd_tex_capture_plan()` cannot return `ERR_NULL` here: `rec`, `limits` and `read` are validated before the call, and `rec->gxcm` is never NULL. The branch is therefore never taken. |
| 197 | `if (cap_result == TWLHD_TEX_CAP_ERR_CAPACITY)` → `if (cap_result != ...)` | **Equivalent (unreachable branch).** `twlhd_tex_capture_plan()` never returns `ERR_CAPACITY`; it drops records instead. The branch is never taken. |
| 197 | `return TWLHD_V2_ERR_SIZE;` → `return TWLHD_V2_ERR_NULL;` | **Equivalent (unreachable branch).** Same as above: the capacity branch is unreachable. |
| 198 | `return TWLHD_V2_ERR_TXDS;` → `return TWLHD_V2_ERR_NULL;` | **Equivalent (unreachable branch).** The fallback is only reachable if `plan()` returns an error other than `ERR_NULL` or `ERR_CAPACITY`, which it does not. |
| 204 | `TWLHD_GX_REC_TEX_PLDT_MAX` passed as the TXDT capacity to `twlhd_tex_capture_apply()` | **Equivalent under default budgets.** With the default budgets, `TXDT_MAX (131,072 B) >= PLDT_MAX (32,768 B)`, so every palette that fits in the PLDT buffer also fits in the TXDT buffer. A build with `PLDT_MAX > TXDT_MAX` would distinguish the mutant. |

## Previously-classified survivors now killed

| Location | Mutant | Killing test |
|---|---|---|
| `twlhd_tex_source.c:39` | `for (i = 0; i <= src->count; i++)` | `test_source_oob_full_table` (ASan abort on `regions[16]`) and `test_source_poison_slot` (poisoned `regions[count]`). |
| `twlhd_gx_record.c:205` | `TWLHD_V2_TXDS_MAX_RECORDS - 1u` entries capacity | `test_256_distinct_keys` (256 kept keys need the full 256-entry table). |
| `twlhd_gx_record.c:209` | `return TWLHD_V2_ERR_TXDT;` → `return TWLHD_V2_ERR_NULL;` | `test_apply_read_error_txdt` (stateful mock succeeds on probe, fails on copy). |

## Notes

- The generic mutant generator produced constant and operator-swap mutants
  inside the scoped function and the whole backend file. The targeted set added
  pinned mutations for kind/base/length routing, the region-table bound, the
  overflow predicate, the file-flag OR, and error-code returns.
- The `result == TWLHD_V2_OK && stats_out` guard added in repair round 1
  produced two extra operator-swap mutants (`&&` → `\|\|`, `==` → `!=`); both
  are killed by the strengthened `test_small_output_buffer`.
- A missing targeted mutant substring aborts the runner with exit code 2, so a
  source change cannot silently shrink the mutant set.
- The runner restores the original `common/twlhd_gx_record.c` and
  `common/twlhd_tex_source.c` contents after the run; the final line of the run
  log is `common/ restored: YES`.
