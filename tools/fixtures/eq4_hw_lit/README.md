# tools/fixtures/eq4_hw_lit — EQ-4zi DS-mode lighting hardware capture

**Test data, not code**, committed so `tools/eq4_hw_lit/test_hw_lit.py` can
replay the measurement without the console. Numbers and commands:
`docs/reports/eq4zi-lit-hw-result.md`.

Basis: one console, one capture session. Each PNG is the top-left 400x240 crop
of the first of three byte-identical-in-measurement captures of the DS ROM
`twlhd-lit1.nds` (`LIT=1`, scene `lit_a`) or `twlhd-lit2.nds` (`LIT=2`, scene
`lit_b`). The expected JSON is the output of `tools/hw/predict_lit.py` for the
matching `tools/hw/gen_lit_frames.py` frame, with the machine-specific `frame`
and `tool` fields removed. No ROM is committed.

| File | SHA-256 |
|---|---|
| `hw_lit1_a_top400x240.png` | `e84b3f1bb4d4dcac3497ef7d7936fe25d217bb773940ba9b2da3f3f6c4a2ddc7` |
| `hw_lit2_a_top400x240.png` | `dcd5a85e84ef5b28f8b0a9b7f240e91dfc515181748c1d93fc62d33a3c4a596c` |
| `expected_lit_a.json` | `5d28bc4b8f9fd6e0e6a1684cee19e35390564258684ad8a5b45fdfb4429476d7` |
| `expected_lit_b.json` | `ac25bf665bc5c57ad3d1b2776c6f366344c0b3fa365481eebb08a01c82613601` |
