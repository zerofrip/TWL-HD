# SPDX-License-Identifier: Apache-2.0
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM is not set - install the devkitPro nds-dev package and export DEVKITARM")
endif

ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO is not set - install the devkitPro nds-dev package and export DEVKITPRO")
endif

ifeq ($(wildcard $(DEVKITPRO)/libnds/include/nds.h),)
$(error "libnds not found - install the devkitPro nds-dev package (dkp-pacman -S nds-dev)")
endif

include $(DEVKITARM)/ds_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing extra header files
#---------------------------------------------------------------------------------
TARGET		:=	twlhd-arm9v2
BUILD		:=	build
SOURCES		:=	source ../common
DATA		:=	data
INCLUDES	:=	include ../common
GRAPHICS	:=	gfx

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-mthumb -mthumb-interwork

CFLAGS	:=	-g -Wall -O2\
		-march=armv5te -mtune=arm946e-s -fomit-frame-pointer\
		-ffast-math \
		$(ARCH)

CFLAGS		+=	$(INCLUDE) -DARM9
CXXFLAGS	:=	$(CFLAGS) -fno-rtti -fno-exceptions

ASFLAGS		:=	-g $(ARCH)
LDFLAGS		=	-specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS		:=	-lnds9 -lfat
LIBDIRS		:=	$(LIBNDS)
NM		:=	arm-none-eabi-nm

#---------------------------------------------------------------------------------
# Optional test build: dump /twlhd_frame_v2.bin at a fixed frame instead of
# waiting for KEY_A. AUTODUMP_FRAME=0 (the default, and the empty value) keeps
# the normal ROM byte-identical. The conditional must live at top level so the
# build-directory sub-make (which inherits the variable through MAKEFLAGS)
# resolves the same TARGET, BUILD and CFLAGS.
#---------------------------------------------------------------------------------
AUTODUMP_FRAME	?=	0
AUTODUMP_FRAME_STRIPPED	:=	$(strip $(AUTODUMP_FRAME))
ifeq ($(AUTODUMP_FRAME_STRIPPED),)
AUTODUMP_FRAME_STRIPPED	:=	0
endif
ifneq ($(AUTODUMP_FRAME_STRIPPED),0)
AUTODUMP_FRAME_OK := $(shell echo '$(AUTODUMP_FRAME_STRIPPED)' | grep -Eq '^[1-9][0-9]*$$' && test $(AUTODUMP_FRAME_STRIPPED) -le 65535 && echo ok)
ifeq ($(AUTODUMP_FRAME_OK),)
$(error AUTODUMP_FRAME must be empty, 0, or a positive decimal integer <= 65535 (got: $(AUTODUMP_FRAME)))
endif
TARGET		:=	twlhd-arm9v2-autodump
BUILD		:=	build-autodump
CFLAGS		+=	-DTWLHD_DEMO_AUTODUMP_FRAME=$(AUTODUMP_FRAME_STRIPPED)
# CXXFLAGS was expanded from CFLAGS with := above, so it needs the define too.
CXXFLAGS	+=	-DTWLHD_DEMO_AUTODUMP_FRAME=$(AUTODUMP_FRAME_STRIPPED)
endif

#---------------------------------------------------------------------------------
# Optional viewport build: VP_X1/VP_Y1/VP_X2/VP_Y2 override the demo's
# glViewport so a non-default-viewport capture can be produced. All four must
# be set together; when none are set TARGET, BUILD and CFLAGS stay untouched so
# the default ROM is byte-identical. This block runs after the AUTODUMP block,
# so AUTODUMP_FRAME and the viewport compose into one variant ROM.
#---------------------------------------------------------------------------------
VP_X1_STRIPPED	:=	$(strip $(VP_X1))
VP_Y1_STRIPPED	:=	$(strip $(VP_Y1))
VP_X2_STRIPPED	:=	$(strip $(VP_X2))
VP_Y2_STRIPPED	:=	$(strip $(VP_Y2))
VP_SET_COUNT	:=	$(words $(VP_X1_STRIPPED) $(VP_Y1_STRIPPED) $(VP_X2_STRIPPED) $(VP_Y2_STRIPPED))
ifeq ($(VP_SET_COUNT),0)
else ifneq ($(VP_SET_COUNT),4)
$(error VP_X1, VP_Y1, VP_X2 and VP_Y2 must all be set together (got: $(VP_X1), $(VP_Y1), $(VP_X2), $(VP_Y2)))
else
VP_OK := $(shell echo '$(VP_X1_STRIPPED)' | grep -Eq '^[0-9]+$$' && echo '$(VP_Y1_STRIPPED)' | grep -Eq '^[0-9]+$$' && echo '$(VP_X2_STRIPPED)' | grep -Eq '^[0-9]+$$' && echo '$(VP_Y2_STRIPPED)' | grep -Eq '^[0-9]+$$' && test $(VP_X1_STRIPPED) -le $(VP_X2_STRIPPED) && test $(VP_Y1_STRIPPED) -le $(VP_Y2_STRIPPED) && test $(VP_X2_STRIPPED) -le 255 && test $(VP_Y2_STRIPPED) -le 191 && echo ok)
ifeq ($(VP_OK),)
$(error VP_X1/VP_Y1/VP_X2/VP_Y2 must be decimal integers with 0 <= x1 <= x2 <= 255 and 0 <= y1 <= y2 <= 191 (got: $(VP_X1), $(VP_Y1), $(VP_X2), $(VP_Y2)))
endif
TARGET		:=	twlhd-arm9v2-vp
BUILD		:=	build-vp
VP_FLAGS	:=	-DTWLHD_DEMO_VP_X1=$(VP_X1_STRIPPED) -DTWLHD_DEMO_VP_Y1=$(VP_Y1_STRIPPED) -DTWLHD_DEMO_VP_X2=$(VP_X2_STRIPPED) -DTWLHD_DEMO_VP_Y2=$(VP_Y2_STRIPPED)
CFLAGS		+=	$(VP_FLAGS)
# CXXFLAGS was expanded from CFLAGS with := above, so it needs the defines too.
CXXFLAGS	+=	$(VP_FLAGS)
ifneq ($(AUTODUMP_FRAME_STRIPPED),0)
TARGET		:=	twlhd-arm9v2-vp-autodump
BUILD		:=	build-vp-autodump
endif
endif

#---------------------------------------------------------------------------------
# Optional multi-cube build: CUBES=1..170 draws that many cubes in a 17-column
# grid. Unset (or 1) leaves TARGET, BUILD and CFLAGS untouched so the default
# ROM is byte-identical. This block runs after the AUTODUMP and VP blocks, so
# the suffixes compose (e.g. twlhd-arm9v2-autodump-c64 in
# build-autodump-c64).
#---------------------------------------------------------------------------------
CUBES_STRIPPED	:=	$(strip $(CUBES))
ifneq ($(CUBES_STRIPPED),)
CUBES_OK := $(shell echo '$(CUBES_STRIPPED)' | grep -Eq '^[1-9][0-9]*$$' && test $(CUBES_STRIPPED) -le 170 && echo ok)
ifeq ($(CUBES_OK),)
$(error CUBES must be empty or a decimal integer in 1..170 (got: $(CUBES)))
endif
ifneq ($(CUBES_STRIPPED),1)
TARGET		:=	$(TARGET)-c$(CUBES_STRIPPED)
BUILD		:=	$(BUILD)-c$(CUBES_STRIPPED)
CFLAGS		+=	-DTWLHD_DEMO_CUBES=$(CUBES_STRIPPED)
# CXXFLAGS was expanded from CFLAGS with := above, so it needs the define too.
CXXFLAGS	+=	-DTWLHD_DEMO_CUBES=$(CUBES_STRIPPED)
endif
endif

#---------------------------------------------------------------------------------
# Optional primitive-scene build: SCENE=1..4 draws triangles, a triangle strip,
# a quad strip or four alpha-labelled quads instead of the cube. Unset (or 0)
# leaves TARGET, BUILD and
# CFLAGS untouched so the default ROM is byte-identical. This block runs after
# the AUTODUMP, VP and CUBES blocks, so the suffixes compose; SCENE and
# CUBES > 1 are mutually exclusive and rejected by demo_frame.cpp.
#---------------------------------------------------------------------------------
SCENE_STRIPPED	:=	$(strip $(SCENE))
ifneq ($(SCENE_STRIPPED),)
SCENE_OK := $(shell echo '$(SCENE_STRIPPED)' | grep -Eq '^[0-5]$$' && echo ok)
ifeq ($(SCENE_OK),)
$(error SCENE must be empty or a decimal integer in 0..5 (got: $(SCENE)))
endif
ifneq ($(SCENE_STRIPPED),0)
TARGET		:=	$(TARGET)-s$(SCENE_STRIPPED)
BUILD		:=	$(BUILD)-s$(SCENE_STRIPPED)
CFLAGS		+=	-DTWLHD_DEMO_SCENE=$(SCENE_STRIPPED)
# CXXFLAGS was expanded from CFLAGS with := above, so it needs the define too.
CXXFLAGS	+=	-DTWLHD_DEMO_SCENE=$(SCENE_STRIPPED)
endif
endif

#---------------------------------------------------------------------------------
# Optional diagnostic build: DIAGSCREEN=1..6 produces a variant ROM that changes
# exactly one suspected display factor so the owner can bisect on real hardware.
# Unset/empty/0 leaves TARGET, BUILD and CFLAGS untouched so the default ROM is
# byte-identical. The suffix composes after SCENE/AUTODUMP/VP/CUBES.
#---------------------------------------------------------------------------------
DIAGSCREEN	?=	0
DIAGSCREEN_STRIPPED	:=	$(strip $(DIAGSCREEN))
ifeq ($(DIAGSCREEN_STRIPPED),)
DIAGSCREEN_STRIPPED	:=	0
endif
ifneq ($(DIAGSCREEN),)
ifneq ($(DIAGSCREEN),$(DIAGSCREEN_STRIPPED))
$(error DIAGSCREEN must be empty, 0, or a decimal integer in 1..6 (got: $(DIAGSCREEN)))
endif
endif
ifneq ($(DIAGSCREEN_STRIPPED),0)
DIAGSCREEN_OK := $(shell echo '$(DIAGSCREEN_STRIPPED)' | grep -Eq '^[1-6]$$' && echo ok)
ifeq ($(DIAGSCREEN_OK),)
$(error DIAGSCREEN must be empty, 0, or a decimal integer in 1..6 (got: $(DIAGSCREEN)))
endif
TARGET		:=	$(TARGET)-diag$(DIAGSCREEN_STRIPPED)
BUILD		:=	$(BUILD)-diag$(DIAGSCREEN_STRIPPED)
CFLAGS		+=	-DTWLHD_DEMO_DIAGSCREEN=$(DIAGSCREEN_STRIPPED)
# CXXFLAGS was expanded from CFLAGS with := above, so it needs the define too.
CXXFLAGS	+=	-DTWLHD_DEMO_DIAGSCREEN=$(DIAGSCREEN_STRIPPED)
endif

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir)) \
			$(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

# Create the build directory during parsing so that dry-run invocations can
# recurse into it. Skip this for clean, which removes the directory anyway.
ifeq ($(filter clean,$(MAKECMDGOALS)),)
$(shell mkdir -p $(BUILD))

# make cannot see -D changes in the object prerequisites, so record the define
# set in the variant BUILD directory and drop stale objects when it changes.
TWLHD_FLAGS_STAMP := AUTODUMP=$(AUTODUMP_FRAME_STRIPPED) $(VP_FLAGS) TWLHD_DEMO_CUBES=$(if $(CUBES_STRIPPED),$(CUBES_STRIPPED),1) TWLHD_DEMO_SCENE=$(if $(SCENE_STRIPPED),$(SCENE_STRIPPED),0) TWLHD_DEMO_DIAGSCREEN=$(DIAGSCREEN_STRIPPED)
ifneq ($(strip $(TWLHD_FLAGS_STAMP)),$(strip $(shell cat $(BUILD)/flags.stamp 2>/dev/null)))
$(shell rm -f $(BUILD)/*.o $(BUILD)/*.d)
endif
$(file >$(BUILD)/flags.stamp,$(strip $(TWLHD_FLAGS_STAMP)))
endif

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.bin)))
PNGFILES	:=	$(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.png)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES)) $(PNGFILES:.png=.o)

export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export OFILES		:=	$(OFILES_BIN) $(OFILES_SRC)

export HFILES		:=	$(PNGFILES:.png=.h) $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE		:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS		:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean all

#---------------------------------------------------------------------------------
all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) build-autodump $(TARGET).elf $(TARGET).nds $(TARGET).arm9 $(TARGET).guard $(TARGET).guard.nm
	@rm -f twlhd-arm9v2-autodump.elf twlhd-arm9v2-autodump.nds twlhd-arm9v2-autodump.arm9 twlhd-arm9v2-autodump.guard twlhd-arm9v2-autodump.guard.nm
	@rm -fr build-vp build-vp-autodump
	@rm -f twlhd-arm9v2-vp*.elf twlhd-arm9v2-vp*.nds twlhd-arm9v2-vp*.arm9 twlhd-arm9v2-vp*.guard twlhd-arm9v2-vp*.guard.nm twlhd-arm9v2-vp*.map
	@rm -fr build-c* build-*c*
	@rm -f twlhd-arm9v2*c*.elf twlhd-arm9v2*c*.nds twlhd-arm9v2*c*.arm9 twlhd-arm9v2*c*.guard twlhd-arm9v2*c*.guard.nm twlhd-arm9v2*c*.map
	@rm -fr build-s[0-9]* build-*-s[0-9]*
	@rm -f twlhd-arm9v2*-s[0-9]*.elf twlhd-arm9v2*-s[0-9]*.nds twlhd-arm9v2*-s[0-9]*.arm9 twlhd-arm9v2*-s[0-9]*.guard twlhd-arm9v2*-s[0-9]*.guard.nm twlhd-arm9v2*-s[0-9]*.map
	@rm -fr build-diag* build-*-diag*
	@rm -f twlhd-arm9v2*-diag*.elf twlhd-arm9v2*-diag*.nds twlhd-arm9v2*-diag*.arm9 twlhd-arm9v2*-diag*.guard twlhd-arm9v2*-diag*.guard.nm twlhd-arm9v2*-diag*.map

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).nds	:	$(OUTPUT).elf $(OUTPUT).guard

$(OUTPUT).elf	:	$(OFILES)

# Post-link check: function-local static proxies would emit __cxa_guard_acquire
# and a guard variable. The proxies are stateless, so these must be absent.
# The stamp file prevents ndstool from re-running on every make invocation.
$(OUTPUT).guard: $(OUTPUT).elf
	@echo "Checking for guard variables..."
	@$(NM) -C $< > $@.nm 2>&1; \
	nm_exit=$$?; \
	if [ $$nm_exit -ne 0 ]; then \
		echo "Error: $(NM) failed on $< (exit $$nm_exit)"; \
		rm -f $@.nm; \
		exit 1; \
	fi; \
	if grep -qE '__cxa_guard_acquire|guard variable for twlhd_shim::' $@.nm; then \
		echo "Error: $< contains guard variables (reintroduced a function-local static?)"; \
		rm -f $@.nm; \
		exit 1; \
	fi; \
	rm -f $@.nm; \
	touch $@
	@echo "Guard-variable check passed."

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h	:	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
%.s %.h	: %.png %.grit
#---------------------------------------------------------------------------------
	grit $< -fts -o$*

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
