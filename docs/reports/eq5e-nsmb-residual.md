# EQ-5e: residual difference of the NSMB stage-select render after EQ-5d

Labels: FACT = measured here; SOURCE-READ = read in source; INFERENCE = derived from FACT
items; HYPOTHESIS; UNVERIFIED. Only numbers are recorded; game data stays local.

## Setup
FACT: same frame as EQ-5b (swap 5320 of the stage-select dump, DS stack model, culling on),
arm11 build of main after EQ-5d, rendered through Azahar; reference = the DeSmuME 3D-only
framebuffer of swap 5320 (256x192) scaled to 320x240. All numbers are the mean absolute
difference over R/G/B unless noted.

## Distribution of the difference (bilinear x1.25 reference)
FACT: whole image mean 12.22. Histogram of the per-pixel difference (10-level bins, 76800
px): 0-9: 51815, 10-19: 10761, 20-29: 5828, 30-39: 3051, 40-49: 1646, 50-59: 1006,
60-69: 694, 70-79: 506, 80-89: 432, 90 and above: 1061.
FACT: pixels with a difference of at least 30 are 8396 px (10.9 percent of the image) and
carry 50 percent of the total error.
FACT: edge pixels (gradient of the reference above 60, 17476 px) have a mean difference
of 31.0, the other pixels 6.7.

## Reference scaling and alignment
FACT: the choice of the resize filter for the reference changes the mean R/G/B difference
only slightly: nearest 14.7 / 13.4 / 10.6, bilinear 13.7 / 12.7 / 10.3, bicubic
14.3 / 13.2 / 10.5, Lanczos 14.8 / 13.7 / 10.8.
FACT: shifting our render by one pixel in x and y brings the mean (R/G/B averaged)
difference to the bilinear reference from 12.44 to 8.71 and to the nearest reference from
13.17 to 8.67 (search over -3..3 px in both axes, 312x232 inner region). Against the
hardware capture the best shift is (0, 0) (19.18, unshifted 19.18).
SOURCE-READ (EQ-4p report): the alignment of our render was calibrated against hardware
captures, not against the scaled DeSmuME image.
INFERENCE: part of the edge error against the DeSmuME shot is a one-pixel alignment
between the scaled reference and our render. Upper bound: the one-pixel shift lowers the
whole-image mean by 3.73 (12.44 to 8.71) while edge pixels carry about 58 percent of the
total error (17476 x 31.0 of about 939 thousand), so at most about half of the edge error
is explained; the shift was not measured on edge pixels separately.
HYPOTHESIS: the remaining flat-area difference (6.7) is colour / shading / texture-filter
difference; not analysed.
UNVERIFIED: whether the DS-native 256x192 comparison (our render reduced instead of the
reference enlarged) gives the same picture.

## Conclusion
- FACT: the difference is below 10 (bin 0-9) on 67.5 percent of the pixels (51815 of 76800).
- INFERENCE (edge mean 31.0 versus 6.7, about 58 percent of the total error on edge
  pixels): the large differences sit mostly on edges; the count of edge pixels among the
  8396 px with a difference of at least 30 was not measured.
- INFERENCE: remaining colour differences are best judged on flat areas (mean 6.7).
- No code changed.
