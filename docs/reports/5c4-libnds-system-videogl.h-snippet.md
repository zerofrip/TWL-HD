# /opt/devkitpro/libnds/include/nds/system.h snippet

## lcdMainOnTop / lcdSwap / POWER_3D_CORE (lines 80-135)
    80	//! Power Management control bits
    81	typedef enum
    82	{
    83		PM_SOUND_AMP		= BIT(0),		//!< Power the sound hardware (needed to hear stuff in GBA mode too).
    84		PM_SOUND_MUTE		= BIT(1),		//!< Mute the main speakers, headphone output will still work.
    85		PM_BACKLIGHT_BOTTOM	= BIT(2),		//!< Enable the bottom backlight if set.
    86		PM_BACKLIGHT_TOP	= BIT(3),		//!< Enable the top backlight if set.
    87		PM_SYSTEM_PWR		= BIT(6),		//!< Turn the power *off* if set.
    88	
    89	#ifdef ARM9
    90		POWER_LCD		= PM_ARM9_DIRECT | POWCNT_LCD,			//!<	Controls the power for both LCD screens.
    91		POWER_2D_A		= PM_ARM9_DIRECT | POWCNT_2D_GFX_A,		//!<	Controls the power for the main 2D core.
    92		POWER_MATRIX	= PM_ARM9_DIRECT | POWCNT_3D_RENDER,	//!<	Controls the power for the 3D matrix.
    93		POWER_3D_CORE	= PM_ARM9_DIRECT | POWCNT_3D_GEOMETRY,	//!<	Controls the power for the main 3D core.
    94		POWER_2D_B		= PM_ARM9_DIRECT | POWCNT_2D_GFX_B,		//!<	Controls the power for the sub 2D core.
    95		POWER_SWAP_LCDS	= PM_ARM9_DIRECT | POWCNT_LCD_SWAP,		//!<	Controls which screen should use the main core.
    96		POWER_ALL_2D	= PM_ARM9_DIRECT | POWER_LCD | POWER_2D_A | POWER_2D_B,			//!< power just 2D hardware.
    97		POWER_ALL		= PM_ARM9_DIRECT | POWER_ALL_2D | POWER_3D_CORE | POWER_MATRIX	//!< power everything.
    98	#endif
    99	}PM_Bits;
   100	
   101	static inline
   102	/*!	Set the LED blink mode
   103		\param bm What to power on.
   104	*/
   105	void ledBlink(int bm) {
   106		pmSetPowerLed((PmLedMode)(bm&3));
   107	}
   108	
   109	//--------------------------------------------------------------
   110	//    ARM9 section
   111	//--------------------------------------------------------------
   112	#ifdef ARM9
   113	
   114	//!	Turns on specified hardware.
   115	/*!	May be called from arm7 or arm9 (arm9 power bits will be ignored by arm7, arm7 power bits
   116		will be passed to the arm7 from the arm9).
   117	
   118		\param bits What to power on.
   119	*/
   120	void powerOn(int bits);
   121	
   122	//!	Turns off specified hardware.
   123	/*!	May be called from arm7 or arm9 (arm9 power bits will be ignored by arm7, arm7 power bits
   124		will be passed to the arm7 from the arm9).
   125	
   126		\param bits What to power on.
   127	*/
   128	void powerOff(int bits);
   129	
   130	//!	Switches the screens.
   131	static inline void lcdSwap(void) { pmGfxLcdSwap(); }
   132	
   133	//!	Forces the main core to display on the top.
   134	static inline void lcdMainOnTop(void) { pmGfxSetLcdLayout(PmLcdLayout_TopIsA); }
   135	

# /opt/devkitpro/libnds/include/nds/arm9/videoGL.h snippets

## glEnable / glClearColor / glClearPolyID / glPolyFmt / POLY_ALPHA (lines 530-790)
   530	GL_STATIC_INL
   531	/*! \fn  u32 POLY_ALPHA(u32 n)
   532	\brief used in glPolyFmt() to set the alpha level for the following polygons, set to 0 for wireframe mode
   533	\param n the level of alpha (0-31)
   534	\return value for hw register*/
   535	 u32 POLY_ALPHA(u32 n) { return (u32)((n) << 16); }
   536	
   537	GL_STATIC_INL
   538	/*! \fn  u32 POLY_ID(u32 n)
   539	\brief used in glPolyFmt() to set the Polygon ID for the following polygons
   540	\param n the ID to set for following polygons (0-63)
   541	\return value for hw register
   542	*/
   543	 u32 POLY_ID(u32 n) { return (u32)((n)<<24); }
   544	
   545	GL_STATIC_INL
   546	/*! \fn  void glPolyFmt(u32 params)
   547	\brief Set the parameters for polygons rendered on the current frame<BR>
   548	<A HREF="http://problemkaputt.de/gbatek.htm#ds3dpolygonattributes">GBATEK http://problemkaputt.de/gbatek.htm#ds3dpolygonattributes</A>
   549	\param params the paramters to set for the polygons for the current frame. valid paramters are enumerated in GL_POLY_FORMAT_ENUM and in the functions POLY_ALPHA() and POLY_ID() */
   550	void glPolyFmt(u32 params) {
   551		GFX_POLY_FORMAT = params;
   552	}
   553	
   554	GL_STATIC_INL
   555	/*! \fn  void glEnable(int bits)
   556	\brief Enables various gl states (blend, alpha test, etc..)
   557	\param bits bit mask of desired attributes, attributes are enumerated in DISP3DCNT_ENUM */
   558	void glEnable(int bits) {
   559		GFX_CONTROL |= bits;
   560	}
   561	
   562	GL_STATIC_INL
   563	/*! \fn   void glDisable(int bits)
   564	\brief Disables various gl states (blend, alpha test, etc..)
   565	\param bits bit mask of desired attributes, attributes are enumerated in DISP3DCNT_ENUM */
   566	void glDisable(int bits) {
   567	/*! \fn void glClearColor(u8 red, u8 green, u8 blue, u8 alpha)
   568	\brief sets the color of the rear-plane(a.k.a Clear Color/Plane)
   569	\param red component (0-31)
   570	\param green component (0-31)
   571	\param blue component (0-31)
   572	\param alpha from 0(clear) to 31(opaque)*/
   573	void glClearColor(u8 red, u8 green, u8 blue, u8 alpha) {
   574		GFX_CLEAR_COLOR = glCurClearColor = ( glCurClearColor & 0xFFE08000) | (0x7FFF & RGB15(red, green, blue)) | ((alpha & 0x1F) << 16);
   575	}
   576	
   577	GL_STATIC_INL
   578	/*! \fn void glClearPolyID(u8 ID)
   579	\brief sets the polygon ID of the rear-plane(a.k.a. Clear/Color Plane), useful for antialiasing and edge coloring
   580	\param ID the polygon ID to give the rear-plane */
   581	void glClearPolyID(u8 ID) {
   582		GFX_CLEAR_COLOR = glCurClearColor = ( glCurClearColor & 0xC0FFFFFF) | (( ID & 0x3F ) << 24 );
   583	}
