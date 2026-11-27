# tools/fixtures/eq4_hw_texfmt — EQ-4zj DS-mode texture-format capture

**Test data, not code**, committed so `tools/eq4_hw_texfmt/test_hw_texfmt.py`
can replay the measurement without the console. Numbers and commands:
`docs/reports/eq4zj-texfmt-hw-result.md`.

Basis: one console, one capture session. The PNG is the top-left 400x240 crop of
the first of three captures (all three gave identical per-quad numbers) of the
DS ROM built with `TEXFMT=1` (`twlhd-arm9v2-texfmt.nds`). No ROM is committed.

| File | SHA-256 |
|---|---|
| `hw_texfmt_top400x240.png` | `0f02cc8b8125dfd177633209050c52c111f13054bf68041d9c5080c97c1ad39f` |
