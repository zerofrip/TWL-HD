# tools/fixtures/eq4_hw_probes — EQ-4zl/m/n DS-mode probe captures

**Test data, not code**, committed so `tools/eq4_hw_probes/test_hw_probes_capture.py`
can replay the measurements without the console. Numbers and commands:
`docs/reports/eq4zl-zm-zn-probes-hw-result.md`.

Basis: one console, one capture session. Each PNG is the top-left 400x240 crop of
the first of three captures of the DS ROM built with `TEXBEH=1`, `GRAD=1` or
`CULL=1`. No ROM is committed.

| File | ROM | SHA-256 |
|---|---|---|
| `hw_texbeh_top400x240.png` | `TEXBEH=1` | `c044a14fbc1fc893f9d45985084e41031eeddc215bf028aaddc499673c6ffacb` |
| `hw_grad_top400x240.png` | `GRAD=1` | `4c46a7d259ce3b78a11ac79f186ea42423a0889fb2edbce88b1f3f381bac7c34` |
| `hw_cull_top400x240.png` | `CULL=1` | `a19f7730ce49ecf0167d96d2db29580950c9b7c47ae58964a6cfd274e1179915` |
