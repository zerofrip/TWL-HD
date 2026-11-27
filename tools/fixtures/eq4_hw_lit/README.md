# tools/fixtures/eq4_hw_lit — EQ-4zi DS-mode lighting hardware capture

**Test data, not code**, committed so `tools/eq4_hw_lit/test_hw_lit.py` can
replay the measurement without the console. Numbers and commands:
`docs/reports/eq4zi-lit-hw-result.md`.

Basis: one console, one capture session. Each PNG is the top-left 400x240 crop
of the first of three byte-identical-in-measurement captures of the DS ROM
`twlhd-lit1.nds` (`LIT=1`, scene `lit_a`) or `twlhd-lit2.nds` (`LIT=2`, scene
`lit_b`) or `twlhd-lit3.nds` (`LIT=3`, scene `lit_c`). The expected JSON is the output of `tools/hw/predict_lit.py` for the
matching `tools/hw/gen_lit_frames.py` frame, with the machine-specific `frame`
and `tool` fields removed. No ROM is committed.

| File | SHA-256 |
|---|---|
| `expected_lit_a.json` | `eb7557c77bc8d17f34933047caeace7775d511a7d580fa1382be51710b917878` |
| `expected_lit_b.json` | `8da9a9eca150d8826da985b9d334b9ec91df1a29993eb62a12385de22dbe1075` |
| `expected_lit_c.json` | `5ed1cf332a4fd632266847457ceb007f7a2e101f14c12d8069ae0f1f41cf5795` |
| `hw_lit1_a_top400x240.png` | `e84b3f1bb4d4dcac3497ef7d7936fe25d217bb773940ba9b2da3f3f6c4a2ddc7` |
| `hw_lit2_a_top400x240.png` | `dcd5a85e84ef5b28f8b0a9b7f240e91dfc515181748c1d93fc62d33a3c4a596c` |
| `hw_lit3_a_top400x240.png` | `a46c149645e1c7715a1f2d11ffa9310fe92250638a98bc8fd075e19b68b3605e` |
| `expected_lit_c_separate.json` | `9680f70dc3fb1d101d094d68c4c96f82998251e4aafb31633f1a5b930513d25f` |

`expected_lit_c_separate.json` is `expected_lit_c.json` with the two probe
quads changed to the values the former separate-truncation model predicted
(0 and 107); it is only used as a negative test. `expected_lit_*.json` are the
current interpreter's predictions (diffuse and ambient summed before
truncation).
