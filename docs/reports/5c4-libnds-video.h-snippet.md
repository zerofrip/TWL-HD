# /opt/devkitpro/libnds/include/nds/arm9/video.h snippets

## MODE_0_3D / videoSetMode / ENABLE_3D (lines 424-559)
   424	#define	REG_DISPCNT		(*(vu32*)0x04000000)
   425	#define	REG_DISPCNT_SUB	(*(vu32*)0x04001000)
   426	
   427	#define ENABLE_3D    (1<<3)
   428	#define DISPLAY_ENABLE_SHIFT 8
   429	#define DISPLAY_BG0_ACTIVE    (1 << 8)
   430		MODE_0_3D    = (0x10000 | DISPLAY_BG0_ACTIVE | ENABLE_3D), /**< \brief  3 2D backgrounds 1 3D background (Main engine only)*/
   431	void videoSetMode( u32 mode)  { REG_DISPCNT = mode; }
   432	
   433	static inline
   434	/** \brief  the sub 2D engine video mode
   435	*    \param mode the video mode to set
   436	*/

## vramSetBankA / VRAM_A_TEXTURE (lines 360-396)
   360	*/
   361	void vramRestorePrimaryBanks(u32 vramTemp);
   362	
   363	/** \brief  Restore the E,F,G bank modes.
   364		\param vramTemp restores the E,F,G bank modes to the value encoded in vramTemp (returned from vramSetBanks_EFG)
   365	*/
   366	void vramRestoreBanks_EFG(u32 vramTemp);
   367	
   368	static inline
   369	/** \brief  Set bank A to the indicated mapping.
   370		\param a the mapping of the bank
   371	*/
   372	void vramSetBankA(VRAM_A_TYPE a) { VRAM_A_CR = VRAM_ENABLE | a; }
   373	
   374	static inline
   375	/** \brief  Set bank B to the indicated mapping.
   376		\param b the mapping of the bank
   377	*/
   378	void vramSetBankB(VRAM_B_TYPE b) { VRAM_B_CR = VRAM_ENABLE | b; }
   379	
   380	static inline
   381	/** \brief  Set bank C to the indicated mapping.
   382		\param c the mapping of the bank
   383	*/
   384	void vramSetBankC(VRAM_C_TYPE c) { VRAM_C_CR = VRAM_ENABLE | c; }
   385	
   386	static inline
   387	/** \brief  Set bank D to the indicated mapping.
   388		\param d the mapping of the bank
   389	*/
   390	void vramSetBankD(VRAM_D_TYPE d) { VRAM_D_CR = VRAM_ENABLE | d; }
   391	
   392	static inline
   393	/** \brief  Set bank E to the indicated mapping.
   394		\param e the mapping of the bank
   395	*/
   396	void vramSetBankE(VRAM_E_TYPE e) { VRAM_E_CR = VRAM_ENABLE | e; }

## vramSetBankE / VRAM_E_TEX_PALETTE (lines 230-240)
   230		VRAM_D_TEXTURE_SLOT3 		= 3 | VRAM_OFFSET(3)	//!< maps vram bank d to 3d texture slot 3.
   231	} VRAM_D_TYPE;
   232	
   233	//! Allowed VRAM bank E modes
   234	typedef enum {
   235		VRAM_E_LCD					= 0,		//!< maps vram bank e to lcd.
   236		VRAM_E_MAIN_BG				= 1,		//!< maps vram bank e to main engine background first half of slot 0.
   237		VRAM_E_MAIN_SPRITE			= 2,		//!< maps vram bank e to main engine sprites first half of slot 0.
   238		VRAM_E_TEX_PALETTE			= 3,		//!< maps vram bank e to 3d texture palette slot 0-3.
   239		VRAM_E_BG_EXT_PALETTE		= 4,		//!< maps vram bank e to main engine background extended palette.
   240	} VRAM_E_TYPE;
