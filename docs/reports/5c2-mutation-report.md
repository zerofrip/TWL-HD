# 5C-2 mutation report — `twlhd_gx_rec_serialize_tex` + `twlhd_tex_source`

**Date:** 2026-11-15  
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
|---|---|
| Killed by test | 41 |
| Killed by build | 6 |
| Killed by timeout | 0 |
| Survived | 13 |
| **Total** | **60** |

Kill distribution:

| Step | Count | Share |
|---|---|---|
| C test | 41 | 68.3 % |
| Build | 6 | 10.0 % |
| Timeout | 0 | 0.0 % |
| Survived | 13 | 21.7 % |

## Survivor classification

Each survivor is explained below with a concrete argument. Line numbers are
from the delivered source.

### `common/twlhd_tex_source.c`

| Line | Mutant | Classification |
|---|---|---|
| 20 | `if (!ctx \|\| !ram) return 2;` | **Equivalent under the API contract.** The function promises only `0 = success, nonzero = error`; callers (including the test harness) check `!= 0`. The magnitude of the error return is not observable. |
| 22 | `if (length == 0u) return 2;` | **Equivalent under the API contract.** Same reasoning: any nonzero return rejects a zero-length registration. |
| 37 | `if (!src) return 2;` | **Equivalent under the API contract.** Same reasoning for a null read context. |
| 39 | `for (i = 0; i <= src->count; i++)` | **Latent bug.** The mutant scans one element past the active region table. The existing tests fill the table to `TWLHD_TEX_SOURCE_REGION_MAX` and ASan does not flag this particular intra-object array access; the slot past `count` is either zero or the wrong `kind`, so no false match is observed. A test that poisons `regions[count]` with a matching kind/base/length would kill it. |

### `common/twlhd_gx_record.c` (new serialize function)

| Line | Mutant | Classification |
|---|---|---|
| 196 | `if (cap_result != TWLHD_TEX_CAP_ERR_NULL)` → `if (cap_result == ...)` | **Equivalent (unreachable branch).** `twlhd_tex_capture_plan()` cannot return `ERR_NULL` here: `rec`, `limits` and `read` are validated before the call, and `rec->gxcm` is never NULL. The branch is therefore never taken. |
| 197 | `if (cap_result == TWLHD_TEX_CAP_ERR_CAPACITY)` → `if (cap_result != ...)` | **Equivalent (unreachable branch).** `twlhd_tex_capture_plan()` never returns `ERR_CAPACITY`; it drops records instead. The branch is never taken. |
| 197 | `return TWLHD_V2_ERR_SIZE;` → `return TWLHD_V2_ERR_NULL;` | **Equivalent (unreachable branch).** Same as above: the capacity branch is unreachable. |
| 198 | `return TWLHD_V2_ERR_TXDS;` → `return TWLHD_V2_ERR_NULL;` | **Equivalent (unreachable branch).** The fallback is only reachable if `plan()` returns an error other than `ERR_NULL` or `ERR_CAPACITY`, which it does not. |
| 204 | `TWLHD_GX_REC_TEX_PLDT_MAX` passed as the TXDT capacity to `twlhd_tex_capture_apply()` | **Equivalent under default budgets.** With the default budgets, `TXDT_MAX (131,072 B) >= PLDT_MAX (32,768 B)`, so every palette that fits in the PLDT buffer also fits in the TXDT buffer. A build with `PLDT_MAX > TXDT_MAX` would distinguish the mutant. |
| 205 | `TWLHD_V2_TXDS_MAX_RECORDS - 1u` entries capacity | **Latent.** The tests use at most two distinct texture keys; 255 entries is still sufficient. A test with 256 distinct kept keys would kill it. |
| 207 | `if (cap_result != TWLHD_TEX_CAP_ERR_NULL)` → `if (cap_result == ...)` | **Equivalent (unreachable branch).** `twlhd_tex_capture_apply()` cannot return `ERR_NULL` here: `plan`, `read`, `txdt`, `pldt` and `entries` are all non-NULL when this path is reached. |
| 208 | `if (cap_result == TWLHD_TEX_CAP_ERR_CAPACITY)` → `if (cap_result != ...)` | **Latent.** The fixed scratch buffers are sized to match the default limits, and the largest default-budget test texture (131,072 B) fits exactly. Killing this requires a deliberate limits-vs-scratch mismatch, e.g. full-cap limits with default scratch. |
| 209 | `return TWLHD_V2_ERR_TXDT;` → `return TWLHD_V2_ERR_NULL;` | **Latent.** Only reachable if `apply()` returns `ERR_PLAN` or `ERR_READ`, which would require a corrupted plan or a read failure after planning. No existing test exercises that path. |

## Notes

- The generic mutant generator produced constant and operator-swap mutants
  inside the scoped function and the whole backend file. The targeted set added
  pinned mutations for kind/base/length routing, the region-table bound, the
  overflow predicate, the file-flag OR, and error-code returns.
- A missing targeted mutant substring aborts the runner with exit code 2, so a
  source change cannot silently shrink the mutant set.
- The runner restores the original `common/twlhd_gx_record.c` and
  `common/twlhd_tex_source.c` contents after the run; the final line of the run
  log is `common/ restored: YES`.
