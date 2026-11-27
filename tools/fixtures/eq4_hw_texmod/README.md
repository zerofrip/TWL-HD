# tools/fixtures/eq4_hw_texmod — EQ-4zk DS-mode MODULATE capture

**Test data, not code**, committed so `tools/eq4_hw_texmod/test_hw_texmod.py`
can replay the measurement without the console. Numbers and commands:
`docs/reports/eq4zk-texmod-hw-result.md`.

Basis: one console, one capture session. The PNG is the top-left 400x240 crop of
the first of three captures (identical measurement in all three) of the DS ROM
built with `TEXMOD=1` (`twlhd-arm9v2-texmod.nds`). No ROM is committed.

| File | SHA-256 |
|---|---|
| `hw_texmod_top400x240.png` | `67208da71eaf165fa9424b42e3289e4f4c915f63420862e6a9eae824364c77e3` |
