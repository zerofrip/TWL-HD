# `/opt/devkitpro/libnds/include/nds/system.h` snippet

## ARM9 power bits (`POWER_*`) — lines 78-99

```
    78	#define PM_ARM9_DIRECT BIT(16)
    79	//! Power Management control bits
    80	typedef enum
    81	{
    82		PM_SOUND_AMP		= BIT(0),		//!< Power the sound hardware (needed to hear stuff in GBA mode too).
    83		PM_SOUND_MUTE		= BIT(1),		//!< Mute the main speakers, headphone output will still work.
    84		PM_BACKLIGHT_BOTTOM	= BIT(2),		//!< Enable the bottom backlight if set.
    85		PM_BACKLIGHT_TOP	= BIT(3),		//!< Enable the top backlight if set.
    86		PM_SYSTEM_PWR		= BIT(6),		//!< Turn the power *off* if set.
    87	
    88	#ifdef ARM9
    89		POWER_LCD		= PM_ARM9_DIRECT | POWCNT_LCD,			//!<	Controls the power for both LCD screens.
    90		POWER_2D_A		= PM_ARM9_DIRECT | POWCNT_2D_GFX_A,		//!<	Controls the power for the main 2D core.
    91		POWER_MATRIX	= PM_ARM9_DIRECT | POWCNT_3D_RENDER,	//!<	Controls the power for the 3D matrix.
    92		POWER_3D_CORE	= PM_ARM9_DIRECT | POWCNT_3D_GEOMETRY,	//!<	Controls the power for the main 3D core.
    93		POWER_2D_B		= PM_ARM9_DIRECT | POWCNT_2D_GFX_B,		//!<	Controls the power for the sub 2D core.
    94		POWER_SWAP_LCDS	= PM_ARM9_DIRECT | POWCNT_LCD_SWAP,		//!<	Controls which screen should use the main core.
    95		POWER_ALL_2D	= PM_ARM9_DIRECT | POWER_LCD | POWER_2D_A | POWER_2D_B,			//!< power just 2D hardware.
    96		POWER_ALL		= PM_ARM9_DIRECT | POWER_ALL_2D | POWER_3D_CORE | POWER_MATRIX	//!< power everything.
    97	#endif
    98	}PM_Bits;
```

## `lcdSwap` / `lcdMainOnTop` — lines 130-134

```
   130	//!	Switches the screens.
   131	static inline void lcdSwap(void) { pmGfxLcdSwap(); }
   132	
   133	//!	Forces the main core to display on the top.
   134	static inline void lcdMainOnTop(void) { pmGfxSetLcdLayout(PmLcdLayout_TopIsA); }
```

---

# `/opt/devkitpro/libnds/include/nds/arm9/videoGL.h` snippets

## `GL_MAX_DEPTH` / `fixed12d3` — lines 82-87

```
    82	typedef u16 fixed12d3; /*!< \brief Used for depth (glClearDepth, glCutoffDepth) */
    83	
    84	
    85	#define intto12d3(n)    ((n) << 3) /*!< \brief convert int to fixed12d3 */
    86	#define floatto12d3(n)  ((fixed12d3)((n) * (1 << 3))) /*!< \brief convert int to fixed12d3 */
    87	#define GL_MAX_DEPTH      0x7FFF /*!< \brief the maximum value for type fixed12d3 */
```

## `POLY_ALPHA` — lines 530-535

```
   530	GL_STATIC_INL
   531	/*! \fn  u32 POLY_ALPHA(u32 n)
   532	\brief used in glPolyFmt() to set the alpha level for the following polygons, set to 0 for wireframe mode
   533	\param n the level of alpha (0-31)
   534	\return value for hw register*/
   535	 u32 POLY_ALPHA(u32 n) { return (u32)((n) << 16); }
```

## `glPolyFmt` — lines 770-776

```
   770	GL_STATIC_INL
   771	/*! \fn  void glPolyFmt(u32 params)
   772	\brief Set the parameters for polygons rendered on the current frame<BR>
   773	<A HREF="http://problemkaputt.de/gbatek.htm#ds3dpolygonattributes">GBATEK http://problemkaputt.de/gbatek.htm#ds3dpolygonattributes</A>
   774	\param params the paramters to set for the polygons for the current frame. valid paramters are enumerated in GL_POLY_FORMAT_ENUM and in the functions POLY_ALPHA() and POLY_ID() */
   775	void glPolyFmt(u32 params) {
   776		GFX_POLY_FORMAT = params;
   777	}
```

## `glEnable` / `glDisable` — lines 779-792

```
   779	GL_STATIC_INL
   780	/*! \fn  void glEnable(int bits)
   781	\brief Enables various gl states (blend, alpha test, etc..)
   782	\param bits bit mask of desired attributes, attributes are enumerated in DISP3DCNT_ENUM */
   783	void glEnable(int bits) {
   784		GFX_CONTROL |= bits;
   785	}
   786	
   787	GL_STATIC_INL
   788	/*! \fn   void glDisable(int bits)
   789	\brief Disables various gl states (blend, alpha test, etc..)
   790	\param bits bit mask of desired attributes, attributes are enumerated in DISP3DCNT_ENUM */
   791	void glDisable(int bits) {
   792		GFX_CONTROL &= ~bits;
   793	}
```

## `glClearDepth` — lines 555-561

```
   555	GL_STATIC_INL
   556	/*! \fn  void glClearDepth(fixed12d3 depth)
   557	\brief reset the depth buffer to this value; generally set this to GL_MAX_DEPTH.<BR>
   558	<A HREF="http://problemkaputt.de/gbatek.htm#ds3drearplane">GBATEK http://problemkaputt.de/gbatek.htm#ds3drearplane</A>
   559	\param depth Something to do with the depth buffer, generally set this to GL_MAX_DEPTH */
   560	 void glClearDepth(fixed12d3 depth) { GFX_CLEAR_DEPTH = depth; }
```

## `glClearColor` / `glClearPolyID` — lines 1296-1313

```
  1296	GL_STATIC_INL
  1297	/*! \fn void glClearColor(u8 red, u8 green, u8 blue, u8 alpha)
  1298	\brief sets the color of the rear-plane(a.k.a Clear Color/Plane)
  1299	\param red component (0-31)
  1300	\param green component (0-31)
  1301	\param blue component (0-31)
  1302	\param alpha from 0(clear) to 31(opaque)*/
  1303	void glClearColor(u8 red, u8 green, u8 blue, u8 alpha) {
  1304		GFX_CLEAR_COLOR = glCurClearColor = ( glCurClearColor & 0xFFE08000) | (0x7FFF & RGB15(red, green, blue)) | ((alpha & 0x1F) << 16);
  1305	}
  1306	
  1307	GL_STATIC_INL
  1308	/*! \fn void glClearPolyID(u8 ID)
  1309	\brief sets the polygon ID of the rear-plane(a.k.a. Clear/Color Plane), useful for antialiasing and edge coloring
  1310	\param ID the polygon ID to give the rear-plane */
  1311	void glClearPolyID(u8 ID) {
  1312		GFX_CLEAR_COLOR = glCurClearColor = ( glCurClearColor & 0xC0FFFFFF) | (( ID & 0x3F ) << 24 );
  1313	}
```
