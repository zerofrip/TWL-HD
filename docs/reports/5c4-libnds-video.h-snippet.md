# `/opt/devkitpro/libnds/include/nds/arm9/video.h` snippets

## Display control registers (`REG_DISPCNT`, `ENABLE_3D`, `DISPLAY_BG0_ACTIVE`) — lines 420-432

```
   420	void vramSetBankI(VRAM_I_TYPE i) { VRAM_I_CR = VRAM_ENABLE | i; }
   421	
   422	
   423	// Display control registers
   424	#define	REG_DISPCNT		(*(vu32*)0x04000000)
   425	#define	REG_DISPCNT_SUB	(*(vu32*)0x04001000)
   426	
   427	#define ENABLE_3D    (1<<3)
   428	#define DISPLAY_ENABLE_SHIFT 8
   429	#define DISPLAY_BG0_ACTIVE    (1 << 8)
   430	#define DISPLAY_BG1_ACTIVE    (1 << 9)
   431	#define DISPLAY_BG2_ACTIVE    (1 << 10)
   432	#define DISPLAY_BG3_ACTIVE    (1 << 11)
```

## `MODE_0_3D` and related modes — lines 472-483

```
   472		MODE_3_2D    =  0x10003, /**< \brief  4 2D backgrounds */
   473		MODE_4_2D    =  0x10004, /**< \brief  4 2D backgrounds */
   474		MODE_5_2D    =  0x10005, /**< \brief  4 2D backgrounds */
   475		MODE_6_2D    =  0x10006, /**< \brief  4 2D backgrounds */
   476		MODE_0_3D    = (0x10000 | DISPLAY_BG0_ACTIVE | ENABLE_3D), /**< \brief  3 2D backgrounds 1 3D background (Main engine only)*/
   477		MODE_1_3D    = (0x10001 | DISPLAY_BG0_ACTIVE | ENABLE_3D), /**< \brief  3 2D backgrounds 1 3D background (Main engine only)*/
   478		MODE_2_3D    = (0x10002 | DISPLAY_BG0_ACTIVE | ENABLE_3D), /**< \brief  3 2D backgrounds 1 3D background (Main engine only)*/
   479		MODE_3_3D    = (0x10003 | DISPLAY_BG0_ACTIVE | ENABLE_3D), /**< \brief  3 2D backgrounds 1 3D background (Main engine only)*/
   480		MODE_4_3D    = (0x10004 | DISPLAY_BG0_ACTIVE | ENABLE_3D), /**< \brief  3 2D backgrounds 1 3D background (Main engine only)*/
   481		MODE_5_3D    = (0x10005 | DISPLAY_BG0_ACTIVE | ENABLE_3D), /**< \brief  3 2D backgrounds 1 3D background (Main engine only)*/
   482		MODE_6_3D    = (0x10006 | DISPLAY_BG0_ACTIVE | ENABLE_3D), /**< \brief  3 2D backgrounds 1 3D background (Main engine only)*/
   483	
   484		MODE_FIFO    = (3<<16),		/**< \brief  video display from main memory */
```

## `videoSetMode` — lines 533-537

```
   533	/** \brief  the main 2D engine video mode
   534	*    \param mode the video mode to set
   535	*/
   536	void videoSetMode( u32 mode)  { REG_DISPCNT = mode; }
```

## VRAM bank A modes (`VRAM_A_TEXTURE` etc.) — lines 155-174

```
   155	//! Allowed VRAM bank A modes
   156	typedef enum {
   157		VRAM_A_LCD						= 0,					//!< maps vram bank a to lcd.
   158		VRAM_A_MAIN_BG					= 1,					//!< maps vram bank a to main engine background slot 0.
   159		VRAM_A_MAIN_BG_0x06000000		= 1 | VRAM_OFFSET(0),	//!< maps vram bank a to main engine background slot 0.
   160		VRAM_A_MAIN_BG_0x06020000		= 1 | VRAM_OFFSET(1),	//!< maps vram bank a to main engine background slot 1.
   161		VRAM_A_MAIN_BG_0x06040000		= 1 | VRAM_OFFSET(2),	//!< maps vram bank a to main engine background slot 2.
   162		VRAM_A_MAIN_BG_0x06060000		= 1 | VRAM_OFFSET(3),	//!< maps vram bank a to main engine background slot 3.
   163		VRAM_A_MAIN_SPRITE				= 2,					//!< maps vram bank a to main engine sprites slot 0.
   164		VRAM_A_MAIN_SPRITE_0x06400000	= 2 | VRAM_OFFSET(0),	//!< maps vram bank a to main engine sprites slot 0.
   165		VRAM_A_MAIN_SPRITE_0x06420000	= 2 | VRAM_OFFSET(1),	//!< maps vram bank a to main engine sprites slot 1.
   166		VRAM_A_TEXTURE					= 3,					//!< maps vram bank a to 3d texture slot 0.
   167		VRAM_A_TEXTURE_SLOT0			= 3 | VRAM_OFFSET(0),	//!< maps vram bank a to 3d texture slot 0.
   168		VRAM_A_TEXTURE_SLOT1			= 3 | VRAM_OFFSET(1),	//!< maps vram bank a to 3d texture slot 1.
   169		VRAM_A_TEXTURE_SLOT2			= 3 | VRAM_OFFSET(2),	//!< maps vram bank a to 3d texture slot 2.
   170		VRAM_A_TEXTURE_SLOT3			= 3 | VRAM_OFFSET(3)	//!< maps vram bank a to 3d texture slot 3.
   171	} VRAM_A_TYPE;
```

## `vramSetBankA` — lines 368-372

```
   368	static inline
   369	/** \brief  Set bank A to the indicated mapping.
   370		\param a the mapping of the bank
   371	*/
   372	void vramSetBankA(VRAM_A_TYPE a) { VRAM_A_CR = VRAM_ENABLE | a; }
```

## VRAM bank E modes (`VRAM_E_TEX_PALETTE` etc.) — lines 233-242

```
   233	//! Allowed VRAM bank E modes
   234	typedef enum {
   235		VRAM_E_LCD					= 0,		//!< maps vram bank e to lcd.
   236		VRAM_E_MAIN_BG				= 1,		//!< maps vram bank e to main engine background first half of slot 0.
   237		VRAM_E_MAIN_SPRITE			= 2,		//!< maps vram bank e to main engine sprites first half of slot 0.
   238		VRAM_E_TEX_PALETTE			= 3,		//!< maps vram bank e to 3d texture palette slot 0-3.
   239		VRAM_E_BG_EXT_PALETTE		= 4,		//!< maps vram bank e to main engine background extended palette.
   240	} VRAM_E_TYPE;
```

## `vramSetBankE` — lines 392-396

```
   392	static inline
   393	/** \brief  Set bank E to the indicated mapping.
   394		\param e the mapping of the bank
   395	*/
   396	void vramSetBankE(VRAM_E_TYPE e) { VRAM_E_CR = VRAM_ENABLE | e; }
```
