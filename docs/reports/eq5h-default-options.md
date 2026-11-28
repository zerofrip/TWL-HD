# EQ-5h: can culling and the DS stack model become the arm11 default?

Labels: FACT = measured here; SOURCE-READ = read in source/docs; INFERENCE = derived from
FACT items; HYPOTHESIS; UNVERIFIED. Only numbers are recorded.

## Question
SOURCE-READ (EQ-5a, 5b, 5d, 5g reports): the culled, DS-stack render agrees with hardware
and DeSmuME on two retail titles, and the unculled NSMB stage-select frame (swap 5320)
needs 10707 clip vertices, above the capacity of 8192. Should `TWLHD_ARM11_GX_OPTS` default to 0x3u (DS stack + culling)?

## Regression check on the repository's earlier frames
FACT: ten earlier v2 frames (the Run 18-21 hardware-test frames and the c100/c170/cube/
lit_a/ctl_full demo frames) were rendered through Azahar with the current main build at
the default option 0, and with option 0x3u; the top screens (400x240) were compared.
| frame | px differing (0 vs 0x3) | non-dark px at default | non-dark px at 0x3 |
|---|---|---|---|
| tex, formats, tex_ctl, tex_over, behaviour | 0 | 33000, 57600, 33000, 4743, 51200 | same |
| c100 | 18506 | 18506 | 0 |
| c170 | 31542 | 31542 | 0 |
| cube | 12898 | 12898 | 0 |
| lit_a | 33000 | 33000 | 0 |
| ctl_full | 12316 | 12316 | 0 |
FACT: with option 0x1u (culling only) the same five frames are blank as well (0 non-dark
px), so culling, not the stack model, empties them.
FACT: the POLYGON_ATTR writes of c100, cube, lit_a and ctl_full all have the culling
bits (6-7) equal to 0 (100, 1, 4 and 1 writes; c170 was not scanned).
SOURCE-READ (interpreter, EQ-4zn rule): cull mode 0 means "draw nothing".
INFERENCE: c100, cube, lit_a and ctl_full carry polygon attributes that mean "render
nothing" under the DS rule, so enabling culling by default would blank them.
UNVERIFIED: the attribute cause for c170 (blank at 0x1u and 0x3u, attributes not scanned).
SOURCE-READ (EQ-5f, EQ-5g reports): the retail frames of EQ-5a and EQ-5g render with 0x3u.

## Conclusion
- FACT: the default stays 0u in this step; no code changed.
- INFERENCE: the retail replay needs 0x3u (or the culling fallback below), the older test
  frames need 0u, so the choice has to be made per frame or per build, not globally.
- HYPOTHESIS (proposal, not implemented): when the unculled interpretation overflows the clip buffer,
  re-interpret with culling instead of falling back to the built-in scene; and select the
  options per run (build flag or a setting file on the SD card).
