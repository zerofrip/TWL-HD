<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4c — DS top-screen scale/offset from the EQ-4b capture (2026-11-22)

**Scope:** documentation and one host analyser fix. The measurement is a
passive re-analysis of the 2026-11-22 cc3dsfs capture of the
`tools/ds_scale_test` ROM; **nothing was sent to the console** and no hardware
action was taken. Added/changed: this report,
`tools/ds_scale_test/measure_scale.py` (corner detection + `*_check` keys),
`tools/ds_scale_test/test_measure_scale.py` (real-capture regression),
`tools/ds_scale_test/fixtures/sc1_5_crop400x480.png` (committed capture crop),
`tools/ds_scale_test/README.md`, and three append-only Update blocks in
`docs/EQUALITY_CHECK_DESIGN.md`. `$HOME/3ds/cc3dsfs` was read only.

**Labels.** FACT = a command and its raw complete output quoted verbatim below
(or a file:line opened for this report). HARDWARE = a measured capture of the
console. HYPOTHESIS / UNVERIFIED = not measured; stated as such. Nothing
outside the quoted output is FACT, and no cause is inferred from it.

## 1. Setup

FACT (HARDWARE, quoted from the owner's run record): the ROM is
`tools/ds_scale_test/ds-scale-test.nds`, sha256
`76afec0633e8783c46e1aa5b8eb797d5b7ceaa11a1cb51aceaf084382a2c042a` (the same
hash is quoted in `tools/ds_scale_test/README.md` §5). It was run on a New 3DS
in DS mode, with the cc3dsfs capture board (Opt. N3DS) attached, on 2026-11-22;
the analysed frame is `sc1_5`; eight consecutive frames gave identical values.
The capture procedure is the EQ-4b one (`docs/reports/eq4b-cc3dsfs-ds-capture.md`
§3).

FACT — the committed fixture is a byte-identical copy of the owner's 400×480
crop of that frame:

```
$ cp "$HOME/3ds/hwtest-staging/cc3d-out/sc1_5_crop400x480.png" tools/ds_scale_test/fixtures/sc1_5_crop400x480.png
$ cmp "$HOME/3ds/hwtest-staging/cc3d-out/sc1_5_crop400x480.png" tools/ds_scale_test/fixtures/sc1_5_crop400x480.png && echo identical
identical
$ sha256sum tools/ds_scale_test/fixtures/sc1_5_crop400x480.png
badbe06090eb98b11e301640ef9098670da9a940b4d05a38b3857a035e9a1a1a  tools/ds_scale_test/fixtures/sc1_5_crop400x480.png
```

## 2. Raw data (quoted verbatim)

**Raw measurement (owner's run record, HARDWARE):**

```
$ python3 tools/ds_scale_test/measure_scale.py sc1_5.png
measure_scale: corner marker(s) not found: corner_green corner_blue
frame_found=1 corner_red=1 corner_green=0 corner_blue=0 corner_yellow=1 bbox_x0=30 bbox_y0=0 bbox_x1=269 bbox_y1=239 scale_x=0.937500 scale_y=1.250000 offset_x=30 offset_y=0 strip_h_contrast=89.387044 strip_v_contrast=155.725260 (one key=value per line in the real output)
```

**Independent patch dump (owner's run record, python/PIL, 3-pixel sampling,
pixel (x,y) RGB), quoted verbatim:**

```
TL [[(255,251,255),(255,251,255)],[(255,251,255),(255,8,8)]] at (30,0); TR [[(255,251,255),(255,251,255)],[(8,251,8),(8,251,8)]] at (264,0); BL [[(255,251,255),(0,0,255)],[(255,251,255),(0,0,255)]] at (30,234); BR [[(255,251,0),(255,251,0)],[(255,251,0),(255,251,0)]] at (264,234); centre (150,120)=(255,251,255), (149,120)=(251,255,255), (151,120)=(255,255,251). Top non-black x 30..269, y 0..239. Colour-class bboxes with tolerance 24: red (31,2,266,237), green (31,2,267,237), blue (32,2,265,257), yellow (41,50,267,252) — i.e. these class bboxes are polluted by other colours inside the tolerance (anti-colour near-matches, strips, gradient), which is likely why corner detection failed. Observed colour facts: white = (255,251,255) (G is 251, not 255), pure red corner (255,8,8), green corner (8,251,8), blue (0,0,255), yellow (255,251,0). Fixture file (copy it into tools/ds_scale_test/fixtures/, 400x480 crop of the capture, sha256 prefix is printed by `sha256sum`): $HOME/3ds/hwtest-staging/cc3d-out/sc1_5_crop400x480.png
```

The last sentence of the dump ("which is likely why corner detection failed")
is the owner's interpretation; this report treats it as HYPOTHESIS, not FACT.

## 3. Measured result (FACT where quoted)

FACT — the fixed analyser on the committed fixture, raw complete output:

```
$ python3 tools/ds_scale_test/measure_scale.py tools/ds_scale_test/fixtures/sc1_5_crop400x480.png
frame_found=1
corner_red=1
corner_green=1
corner_blue=1
corner_yellow=1
bbox_x0=30
bbox_y0=0
bbox_x1=269
bbox_y1=239
scale_x=0.937500
scale_y=1.250000
offset_x=30
offset_y=0
strip_h_contrast=89.161784
strip_v_contrast=156.618924
scale_x_check=0.001524
scale_y_check=-0.002747
offset_x_check=0.304878
offset_y_check=0.263736
```

So, read only from the quoted output and the quoted patch dump:

- The DS 256×192 top screen occupies **240×240 capture px at x 30..269,
  y 0..239**: `bbox_x0=30 bbox_y0=0 bbox_x1=269 bbox_y1=239`, and the dump
  states "Top non-black x 30..269, y 0..239".
- **capture px per DS px: 0.9375 in x, 1.25 in y** (`scale_x=0.937500`,
  `scale_y=1.250000`); offset `(30, 0)`; the four corners are found.
- The marker-centre cross-check agrees with the frame bbox within 1 capture px
  (`offset_x_check=0.304878`, `offset_y_check=0.263736`,
  `scale_x_check=0.001524`, `scale_y_check=-0.002747`).
- The 1-px centre cross lands at capture x = 150: the dump quotes the centre
  samples at x=149,150,151, and the tool's mapping
  `offset_x + (ds_x+0.5)*scale_x - 0.5` puts DS column 128 at
  30 + 128.5·0.9375 - 0.5 ≈ 150; the three quoted centre samples differ only
  in which channel is 251.

**Observation (both outputs quoted above):** the owner's run and the fixture
run agree on frame, corners, bbox, scale and offset, but not on the two strip
contrasts: 89.387044 / 155.725260 versus 89.161784 / 156.618924. The cause is
UNVERIFIED; the fixture is the 400×480 crop of the same frame name, but the
owner's quoted run analysed `sc1_5.png` (1280×720), and no command for creating
the crop was recorded.

## 4. HYPOTHESES (not measured; candidate causes only)

1. **HYPOTHESIS:** in DS mode the New 3DS shows the DS picture uniformly 1.25×
   as a 320×240 image centred in the 400×240 top LCD. This would explain the
   measured y scale (240/192 = 1.25) and a 1.25× image width of 320; it does
   **not** explain the measured x scale 0.9375 on its own (320·0.75 = 240).
2. **HYPOTHESIS:** the capture path or window squeezes the horizontal axis by
   0.75, which combined with the 1.25 DS upscale gives 0.75 × 1.25 = 0.9375.
   Supporting raw observation: the fixture's whole visible window content is
   0.75× narrower than a 400-wide layout would put it (picture starts at
   x=30 rather than the 40 that a 320-wide centred DS image would start at;
   the EQ-4b report's raw bottom-screen bbox starts at x=31). The cause is not
   established here; §6 checks the cc3dsfs source and does not find a setting
   that produces a 0.75 horizontal squeeze.
3. **HYPOTHESIS:** the colour path is not identity. "Full" white is
   (255,251,255) — green 251, not 255 — and "zero" shows as 0..8 (red corner
   (255,8,8), green (8,251,8)). The 5-bit expansion `v5<<3 | v5>>2` would give
   255 for v5=31 and 0 for v5=0. Candidate causes (none established): the
   console's DS-mode LCD path, the capture board's firmware/colour conversion,
   or cc3dsfs's shader/colour-emulation setting. A raw pixel row from the
   fixture shows the capture is not a straight per-pixel sample: one colour
   appears spread over interleaved 1-px columns.

   FACT — raw row y=5, x=30..40, quoted output:

   ```
   $ python3 -c "from PIL import Image;im=Image.open('tools/ds_scale_test/fixtures/sc1_5_crop400x480.png').convert('RGB');print([im.getpixel((x,5)) for x in range(30,41)])"
   [(255, 251, 255), (0, 255, 40), (4, 0, 255), (255, 0, 0), (0, 255, 0), (0, 0, 255), (247, 0, 0), (0, 255, 0), (0, 0, 255), (0, 0, 0), (0, 0, 0)]
   ```

   The TL red marker's 8 DS px therefore span alternating red/green/blue
   columns in the capture; the analyser's nearest-block rule merges the
   same-colour columns (gap ≤ 4 px) rather than hypothesising a cause.

## 5. Consequence for EQ-3 (PLAN, not a result)

`docs/EQUALITY_CHECK_DESIGN.md` EQ-3 (the same-stream software-rasteriser
picture comparison) compares our side with a 256×192 DS reference. The EQ-4b
capture route measured here must have its capture mapping undone **before**
any position or tolerance comparison:

- capture px per DS px: 0.9375 in x, 1.25 in y; offset (30, 0) at the measured
  frame.
- inverse mapping: `ds_x = (capture_x - 30 + 0.5)/0.9375 - 0.5`,
  `ds_y = (capture_y + 0.5)/1.25 - 0.5`, then compare in DS coordinates.
- an offset tolerance expressed in capture px must be divided by 0.9375 (x) or
  1.25 (y) before it is compared with a DS-space tolerance; a 1 DS px
  tolerance is only 0.9375 capture px horizontally.

This is a plan only. No change to `tools/eq3_compare.py` is made in this task;
EQ-3 stays host-only.

## 6. Part (C) — cc3dsfs source read (read-only)

All commands below were run in `$HOME/3ds/cc3dsfs`; that checkout was not
modified, built or run (§7). Outputs are quoted verbatim.

### 6.1 Sizes and the DS crop table

```
$ grep -n "define TOP_WIDTH_3DS\|define BOT_WIDTH_3DS\|define HEIGHT_3DS\|define WIDTH_DS\|define HEIGHT_DS\|define TOP_SCALED_DS_WIDTH_3DS\|define TOP_SPECIAL_DS_WIDTH_3DS" include/hw_defs.hpp
10:#define TOP_WIDTH_3DS 400
11:#define BOT_WIDTH_3DS 320
12:#define HEIGHT_3DS 240
14:#define TOP_SPECIAL_DS_WIDTH_3DS 384
17:#define TOP_SCALED_DS_WIDTH_3DS 320
20:#define WIDTH_DS 256
21:#define HEIGHT_DS 192
```

```
$ sed -n '7,14p;160,167p' source/frontend.cpp
const CropData default_3ds_crop = {
.top_width = TOP_WIDTH_3DS, .top_height = HEIGHT_3DS,
.top_x = 0, .top_y = 0,
.bot_width = BOT_WIDTH_3DS, .bot_height = HEIGHT_3DS,
.bot_x = 0, .bot_y = 0,
.allowed_joint = true, .allowed_top = true, .allowed_bottom = true,
.allowed_3ds = true, .allowed_ds = false, .is_game_specific = false,
.name = "3DS"};
const CropData default_ds_crop = {
.top_width = WIDTH_DS, .top_height = HEIGHT_DS,
.top_x = 0, .top_y = 0,
.bot_width = WIDTH_DS, .bot_height = HEIGHT_DS,
.bot_x = 0, .bot_y = 0,
.allowed_joint = true, .allowed_top = true, .allowed_bottom = true,
.allowed_3ds = false, .allowed_ds = true, .is_game_specific = false,
.name = "DS"};
```

("3DS" = 400×240 top, 320×240 bottom; "DS" = 256×192 both.)

### 6.2 The Optimize New 3DS board is always treated as a 3DS

```
$ sed -n '138,140p' source/CaptureDeviceSpecific/Optimize_3DS/cypress_optimize_3ds_acquisition.cpp
static CaptureDevice _cypress_optimize_3ds_create_device(const cyop_device_usb_device* usb_device_desc, std::string serial, std::string path) {
	return CaptureDevice(serial, usb_device_desc->name, usb_device_desc->long_name, CAPTURE_CONN_CYPRESS_OPTIMIZE, (void*)usb_device_desc, true, true, true, HEIGHT_3DS, TOP_WIDTH_3DS + BOT_WIDTH_3DS, HEIGHT_3DS, (TOP_WIDTH_3DS * 2) + BOT_WIDTH_3DS, OPTIMIZE_3DS_AUDIO_BUFFER_MAX_SIZE * 8, 90, BOT_WIDTH_3DS, 0, BOT_WIDTH_3DS + TOP_WIDTH_3DS, 0, 0, 0, false, OPTIMIZE_RGB565_FORMAT, 0x200, path);
}
```

The sixth constructor argument is `bool is_3ds` (`include/capture_structs.hpp:370`),
so this board is constructed with `is_3ds = true`. The mode flag follows from it:

```
$ sed -n '1148,1151p' source/frontend.cpp
void update_connected_3ds_ds(FrontendData* frontend_data, const CaptureDevice &old_cc_device, const CaptureDevice &new_cc_device) {
	bool old_connected = frontend_data->display_data.last_connected_ds;
	frontend_data->display_data.last_connected_ds = !new_cc_device.is_3ds;
	bool changed_type = old_connected != frontend_data->display_data.last_connected_ds;
```

So for this board `last_connected_ds` is false even while the console runs a DS
ROM, and `getShownScreenSize`/`get_crop_data_vector` select the **3DS** crop
table (`possible_crops`, whose index 0 is `default_3ds_crop` = 400×240 top,
320×240 bottom), not the DS table (source/WindowScreen.cpp:1804-1837). This is
consistent with the capture: the fixture is a 400×480 crop whose top screen is
240 rows tall.

### 6.3 Default layout and window size

```
$ sed -n '561,566p' source/frontend.cpp
void sanitize_enabled_info(ScreenInfo &top_bot_info, ScreenInfo &top_info, ScreenInfo &bot_info) {
	if((top_bot_info.window_enabled) || (top_info.window_enabled) || (bot_info.window_enabled))
		return;
	top_bot_info.window_enabled = true;
}
$ grep -n "info.crop_kind = 0\|info.scaling = DEFAULT_WINDOW_SCALING_VALUE\|info.bottom_pos = UNDER_TOP\|info.top_par = 0\|info.bot_par = 0" source/frontend.cpp
569:	info.crop_kind = 0;
572:	info.scaling = DEFAULT_WINDOW_SCALING_VALUE;
574:	info.bottom_pos = UNDER_TOP;
605:	info.top_par = 0;
606:	info.bot_par = 0;
```

So with nothing enabled, `sanitize_enabled_info` enables the joint window
(`top_bot_info.window_enabled = true`), and `reset_screen_info` leaves
`crop_kind = 0` (the "3DS" crop), `scaling = DEFAULT_WINDOW_SCALING_VALUE`,
`bottom_pos = UNDER_TOP` and `top_par = 0`.

```
$ sed -n '12,16p' include/display_structs.hpp
#define DEFAULT_NO_SCALING_VALUE -1

#define DEFAULT_WINDOW_SCALING_VALUE 1.0
#define MIN_WINDOW_SCALING_VALUE 1.0
#define MAX_WINDOW_SCALING_VALUE 45.0
```

```
$ sed -n '1634,1641p;1660,1661p' source/WindowScreen.cpp
void WindowScreen::resize_window_and_out_rects(bool do_work) {
	sf::Vector2f top_screen_size = getShownScreenSize(true, &this->loaded_info);
	sf::Vector2f bot_screen_size = getShownScreenSize(false, &this->loaded_info);
	int top_width = (int)top_screen_size.x;
	int top_height = (int)top_screen_size.y;
	float top_scaling = (float)this->loaded_info.scaling;
	int bot_width = (int)bot_screen_size.x;
	int bot_height = (int)bot_screen_size.y;
	get_par_size(top_width, top_height, top_scaling, this->possible_pars[this->loaded_info.top_par], get_divide_3d_par(true, &this->loaded_info));
	get_par_size(bot_width, bot_height, bot_scaling, this->possible_pars[this->loaded_info.bot_par], get_divide_3d_par(false, &this->loaded_info));
```

(`possible_pars[0]` is `base_par`, width multiplier 1.0 / divisor 1.0, so the
default `top_par = 0` is no stretch: source/frontend.cpp:335-336, 260-263.)
`set_position_screens` then computes the joint window's `m_width`/`m_height`
from those rects:

```
$ sed -n '1266,1272p' source/WindowScreen.cpp
	int bot_end_y = bot_screen_y + offset_y + bot_screen_height;
	this->m_width = top_end_x;
	this->m_height = top_end_y;
	if(bot_end_x > this->m_width)
		this->m_width = bot_end_x;
	if(bot_end_y > this->m_height)
		this->m_height = bot_end_y;
```

and the window is created at that size:

```
$ sed -n '540,545p' source/WindowScreen.cpp
			this->update_screen_settings();
			if(this->loaded_info.have_titlebar)
				this->m_win.create(sf::VideoMode({(unsigned int)this->m_width, (unsigned int)this->m_height}), this->title_factory());
			else
				this->m_win.create(sf::VideoMode({(unsigned int)this->m_width, (unsigned int)this->m_height}), this->title_factory(), sf::Style::None);
			this->update_view_size();
```

**Source-level conclusion (FACT from the quoted code):** with no profile and no
overrides, the default layout for the Optimize New 3DS board is the single
joint window (bottom under top, `bottom_pos = UNDER_TOP`), whose top region is
the 400×240 "3DS" crop drawn at scale 1.0, i.e. **an unscaled 400×240 top
screen** (400×480 window buffer). The DS-mode 256×192 crop table is never used
for this board because `is_3ds` is hard-coded true (§6.2).

### 6.4 Command line and config settings

```
$ sed -n '1092,1112p' source/cc3dsfs.cpp
		if(parse_int_arg(i, argc, argv, override_data.override_top_bot_data.pos_y, "--pos_y_both"))
			continue;
		if(parse_double_arg(i, argc, argv, override_data.override_top_bot_data.scaling, "--scaling_both"))
			continue;
		if(parse_int_arg(i, argc, argv, override_data.override_top_bot_data.enabled, "--enabled_both"))
			continue;
		if(parse_int_arg(i, argc, argv, override_data.override_top_data.pos_x, "--pos_x_top"))
			continue;
		if(parse_int_arg(i, argc, argv, override_data.override_top_data.pos_y, "--pos_y_top"))
			continue;
		if(parse_double_arg(i, argc, argv, override_data.override_top_data.scaling, "--scaling_top"))
			continue;
		if(parse_int_arg(i, argc, argv, override_data.override_top_data.enabled, "--enabled_top"))
			continue;
		if(parse_int_arg(i, argc, argv, override_data.override_bot_data.pos_x, "--pos_x_low"))
			continue;
		if(parse_int_arg(i, argc, argv, override_data.override_bot_data.pos_y, "--pos_y_low"))
			continue;
		if(parse_double_arg(i, argc, argv, override_data.override_bot_data.scaling, "--scaling_low"))
			continue;
		if(parse_int_arg(i, argc, argv, override_data.override_bot_data.enabled, "--enabled_low"))
```

```
$ sed -n '1166,1176p' source/cc3dsfs.cpp
		ActualConsoleOutText("  --scaling_both    Overrides the scale factor for the window with both screens.");
		ActualConsoleOutText("  --enabled_both    Overrides the presence of the window with both screens.");
		ActualConsoleOutText("                    1 On, 0 Off.");
		ActualConsoleOutText("  --pos_x_top       Set default x position for the top screen's window.");
		ActualConsoleOutText("  --pos_y_top       Set default y position for the top screen's window.");
		ActualConsoleOutText("  --scaling_top     Overrides the top screen window's scale factor.");
		ActualConsoleOutText("  --enabled_top     Overrides the presence of the top screen's window.");
		ActualConsoleOutText("                    1 On, 0 Off.");
		ActualConsoleOutText("  --pos_x_low       Set default x position for the bottom screen's window.");
		ActualConsoleOutText("  --pos_y_low       Set default y position for the bottom screen's window.");
		ActualConsoleOutText("  --scaling_low     Overrides the bottom screen window's scale factor.");
```

```
$ grep -n '(base + "crop")\|(base + "crop_ds")\|(base + "scale")' source/frontend.cpp
664:	if(key == (base + "crop")) {
668:	if(key == (base + "crop_ds")) {
676:	if(key == (base + "scale")) {
```

```
$ grep -n "width_multiplier" source/frontend.cpp | head -30
251:.width_multiplier = 1.0, .width_divisor = 1.0,
256:.width_multiplier = 8.0, .width_divisor = 7.0,
261:.width_multiplier = 8.0, .width_divisor = 7.0,
266:.width_multiplier = 4.0, .width_divisor = 3.0,
271:.width_multiplier = 4.0, .width_divisor = 3.0,
276:.width_multiplier = 5.0, .width_divisor = 3.0,
281:.width_multiplier = 5.0, .width_divisor = 3.0,
286:.width_multiplier = 16.0, .width_divisor = 9.0,
291:.width_multiplier = 16.0, .width_divisor = 9.0,
296:.width_multiplier = 1.0, .width_divisor = 1.0,
301:.width_multiplier = 1.0, .width_divisor = 1.0,
306:.width_multiplier = 4.0, .width_divisor = 3.0,
311:.width_multiplier = 4.0, .width_divisor = 3.0,
316:.width_multiplier = 5.0, .width_divisor = 3.0,
321:.width_multiplier = 5.0, .width_divisor = 3.0,
326:.width_multiplier = 16.0, .width_divisor = 9.0,
331:.width_multiplier = 16.0, .width_divisor = 9.0,
```

(The PAR table is 1:1, 8:7, 4:3, 5:3, 16:9, each in horizontal/vertical and
non-fit/fit variants; the default index 0 is 1:1.)

```
$ sed -n '630,637p' source/frontend.cpp
void override_set_data_to_screen_info(override_win_data &override_win, ScreenInfo &info) {
	info.initial_pos_x = override_win.pos_x;
	info.initial_pos_y = override_win.pos_y;
	if(override_win.enabled != DEFAULT_NO_ENABLED_VALUE)
		info.window_enabled = override_win.enabled;
	if((override_win.scaling != DEFAULT_NO_SCALING_VALUE) && (override_win.scaling >= MIN_WINDOW_SCALING_VALUE) && (override_win.scaling <= MAX_WINDOW_SCALING_VALUE))
		info.scaling = override_win.scaling;
}
```

A saved profile can carry the same values (source/frontend.cpp:675-682:
`<base>crop`, `<base>crop_ds`, `<base>scale`; `<base>scale` is the same single
`info.scaling`). Consequences read from the quoted code:

```
$ sed -n '990,992p' source/frontend.cpp
void get_par_size(int &width, int &height, float multiplier_factor, const PARData *correction_factor, bool divide_3d_par) {
	width = (int)(width * multiplier_factor);
	height = (int)(height * multiplier_factor);
```

- `--scaling_both N` scales the joint window; `--scaling_top N` scales the
  separate top-screen window. Both set the same `info.scaling`, which is
  applied to **both** axes (`get_par_size(width, height, scaling, ...)`).
- There is no window/crop/CLI option in the code read that scales x and y by
  different factors apart from the explicit pixel-aspect ("PAR") menu entries,
  and the default PAR is 1:1. No entry in the quoted PAR table (or uniform
  scale value) is a horizontal 3:4 factor.
- The DS-mode crop table (256×192) is not reachable for this board in any
  layout, because `is_3ds` is hard-coded true (§6.2).

### 6.5 Proposed command line (NOT run)

The default is already an unscaled 400×240 top region (§6.3); to make it
explicit and to avoid a second window, the proposed command is a variant of
`tools/cc3dsfs_capture/cc_shots.sh:12`:

```
$HOME/3ds/hwtest-staging/cc3dsfs-build/bin/cc3dsfs --auto_connect --no_audio --no_auto_save --enabled_both 0 --enabled_top 1 --scaling_top 1
```

This selects only the top-screen window, default crop "3DS" (400×240) and scale
1.0. `--scaling_both 1` (keeping the joint window) is the equivalent for the
layout that produced the EQ-4b capture. Neither is expected to remove the 0.75
squeeze observed at 0.9375/1.25, because the source has no such squeeze; if the
squeeze persists, it is upstream of the window layout.

### 6.6 UNVERIFIED and the smallest passive experiment

**UNVERIFIED:** where the 0.75 horizontal squeeze enters. The source read above
shows the default layout draws the captured 400×240 top region 1:1 into a
400×480 joint window, so the source alone does not explain a 0.75× x-squeeze;
whether the capture board's DS-mode frames already contain it, or it arises in
the console's DS-mode LCD path, cannot be decided from the source. (The fixture
also shows the whole visible window content shifted by the same 0.75 factor,
e.g. picture x=30 instead of the 40 a centred 320-wide image would give, which
is consistent with a squeezed window/client area rather than only squeezed
frames; the cause is not established.)

**Smallest experiment (passive capture; needs owner approval because it uses
the console, but sends nothing to it):**

```
xvfb-run -a -s "-screen 0 1280x720x24" $HOME/3ds/hwtest-staging/cc3dsfs-build/bin/cc3dsfs --auto_connect --no_audio --no_auto_save --enabled_both 0 --enabled_top 1 --scaling_top 1
```

Run the `tools/ds_scale_test` ROM as in the EQ-4b procedure, take one root
screenshot with `xwd -root` (the existing `tools/cc3dsfs_capture/cc_shots.sh`
does this), and measure it with
`python3 tools/ds_scale_test/measure_scale.py <png>`. If the top-screen window
is 400 px wide and the DS picture still measures 240 px wide, the squeeze is in
the frames/console path; if the window or picture measures 320 px wide, the
squeeze was in the earlier window layout.

## 7. Scope and privacy

FACT — the cc3dsfs checkout is unmodified:

```
$ git -C "$HOME/3ds/cc3dsfs" status --short
(no output; exit status 0)
```

FACT — no literal home-directory path is added by this task:

```
$ grep -rn -E '/(home|Users)/' tools/ds_scale_test docs/reports/eq4c-ds-scale-measurement.md
(no output; exit status 1)
$ git diff -U0 docs/EQUALITY_CHECK_DESIGN.md | grep -E '^\+.*/(home|Users)/'
(no output; exit status 1)
```

`docs/EQUALITY_CHECK_DESIGN.md` carries literal home paths on three lines from
earlier work (lines 5, 6 and 808; not quoted here). Those lines are untouched
(that file's diff is 12 added lines, 0 removed), and the second command shows
the appended Update blocks add none.

No `git worktree` command was run. No hardware command was run in this task.
