# EQ-4zw: the 96 px that culling still changes (SM64DS frame 19180)

Labels: FACT = measured here; SOURCE-READ = read in the DeSmuME source; HYPOTHESIS; UNVERIFIED.
Setup: VTX_DIFF implemented (EQ-4zu), DS stack model, arm11 capacity 8192 (EQ-4zv).

## Where the 96 px are
FACT: culling changes 96 px in 6 connected groups (px, bbox in the 320x240 top image):
63 (135..150, 183..192), 12 (169..176, 188..190), 10 (81..88, 93..95), 6 (73..78, 93),
4 (150, 181..184), 1 (0, 25). 79 of the 96 px lie in the groups of 63, 12 and 4 px

HYPOTHESIS: these groups are small thin triangles; the culled triangles of frame 19180 that
overlap group 63 have w about 7 and span 1-10 px (from the interpreter output of that
frame), but the overlap was not checked for every group.

## Which side matches the references on those pixels
FACT (mean absolute difference over R/G/B on the differing px, hw = mean of 4 captures,
ds = one DeSmuME screenshot): group 63: hw 57.8 (no cull) vs 73.8 (cull), ds 69.4 vs 98.9;
group 12: hw 41.5 vs 36.3, ds 39.0 vs 27.9; groups 10+6: hw 32.7 vs 46.6, ds 31.6 vs 45.5;
group 4: hw 27.2 vs 31.7. So in the largest group the references agree more with the
no-cull image; in group 12 the culled image agrees more.
HYPOTHESIS: in the largest group the triangles our rule removes are drawn by hw/DeSmuME,
or something else differs there.

## Quad facing test (rejected, not committed)
SOURCE-READ: DeSmuME computes the facing from the shoelace sum over all vertices of the
polygon; our rule uses the first three. FACT: an experiment using the full quad shoelace
changed only one polygon on this frame (culled polygons 699 -> 700), so it does not explain
the 96 px. The change was not kept.

## Polygon level comparison with the DeSmuME cull log (sm64_live4, frames 7540..7659)
FACT: matching our culled triangles to the DeSmuME CULLED polygons by vertex w
(tolerance 3), dump frame 7540+f-1 aligns with log frame f (offset 0 gives 384 common
triangles at f=30; offsets -1/+1 give 225/308). On f=30: ours culled 751 triangles,
DeSmuME 486 (as fan triangles); 384 common, 367 ours-only; 12 of the 367 lie fully inside
the screen (x/w and y/w within -1..1), the other 355 do not.
SOURCE-READ: DeSmuME discards polygons fully outside the clip volume before the facing
test (gfx3d.cpp, clipping before the facing code), so it does not log them.
HYPOTHESIS: most of the 355 are such polygons (not counted separately).
Of the 12: 3 are found in
DeSmuME CULLED lines with the same sign (one is the other diagonal of a culled quad; two
appear in log frame 75, same facing sign), 9 have no DeSmuME log line with those w values
in any of the 120 logged frames.
UNVERIFIED: why these 9 are absent (DeSmuME discarding them before the cull step, or a
pose/time difference).

## Conclusion
- FACT: the differing px are 96 px in 6 groups; the full quad facing test changes 1
  polygon; 9 of the 12 fully-on-screen ours-only triangles have no DeSmuME log match.
- HYPOTHESIS: no systematic rule error; the differences are a few small polygons.
  Counter-indication: group 63 (63 of the 96 px), where both references agree more with the
  no-cull image.
- UNVERIFIED: the cause of the 96 px (the 9 unmatched triangles above are the next lead).
- Culling stays opt-in. No code changed in this step.
