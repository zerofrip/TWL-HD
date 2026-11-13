#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM is not set - install the devkitPro 3ds-dev package and export DEVKITARM")
endif

ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO is not set - install the devkitPro 3ds-dev package and export DEVKITPRO")
endif

ifeq ($(wildcard $(DEVKITPRO)/libctru/include/3ds.h),)
$(error "libctru not found - install the devkitPro 3ds-dev package (dkp-pacman -S 3ds-dev)")
endif

include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing binary data
# INCLUDES is a list of directories containing extra header files
# GRAPHICS is a list of directories containing files to be processed by tex3ds
#
# All directories are specified relative to the project directory where
# the makefile is found.
#---------------------------------------------------------------------------------
TARGET		:=	twlhd-arm11
BUILD		:=	build
SOURCES		:=	source ../common
DATA		:=	data
SHADERS		:=	shaders
INCLUDES	:=	include ../common
GRAPHICS	:=	gfx

APP_TITLE	:=	TWL-HD
APP_DESCRIPTION	:=	DS 3D capture research
APP_AUTHOR	:=	TWL-HD contributors

#---------------------------------------------------------------------------------
# Diagnostic build (make -C arm11 DIAG=1): separate build dir, different
# entry point. Sits after the default TARGET/BUILD assignment so it overrides
# them.
#
# HARDWARE UNVERIFIED (build-verified only); this build's purpose is
# real-hardware bisection. Do not deploy.
#---------------------------------------------------------------------------------
DIAG	?=	0
ifeq ($(DIAG),1)
TARGET	:=	twlhd-arm11-diag
BUILD	:=	build-diag
endif

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfpu=vfp -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -Wextra -Werror -O2 -mword-relocations \
		-ffunction-sections \
		$(ARCH)

CFLAGS		+=	$(INCLUDE) -D__3DS__

CXXFLAGS	:=	$(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS		:=	-g $(ARCH)
LDFLAGS		=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS		:=	-lcitro3d -lctru -lm
LIBDIRS		:=	$(CTRULIB)

# Build ID stamp for the ARM11 3DSX. Shown on the bottom-screen console so the
# owner can verify the right .3dsx started. Short git hash (always used, even on
# a dirty tree) with a "-diag" suffix; missing git resolves to "nogit-diag".
TWLHD_GIT_HASH := $(shell git -C $(CURDIR) rev-parse --short HEAD 2>/dev/null)
ifeq ($(strip $(TWLHD_GIT_HASH)),)
TWLHD_BUILD_ID_STR := nogit-diag
else
TWLHD_BUILD_ID_STR := $(TWLHD_GIT_HASH)-diag
endif
CFLAGS += -DTWLHD_BUILD_ID=\"$(TWLHD_BUILD_ID_STR)\"

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir)) \
			$(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir)) \
			$(foreach dir,$(SHADERS),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
# main.c and diag_main.c both define main(); each build takes exactly one.
ifeq ($(DIAG),1)
CFILES		:=	$(filter-out main.c,$(CFILES))
else
CFILES		:=	$(filter-out diag_main.c,$(CFILES))
endif
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))
PNGFILES	:=	$(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.png)))
PICAFILES	:=	$(foreach dir,$(SHADERS),$(notdir $(wildcard $(dir)/*.v.pica)))

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

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES)) \
			$(PICAFILES:.v.pica=.shbin.o)

export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export OFILES		:=	$(OFILES_BIN) $(OFILES_SRC)

export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES))) \
			$(PICAFILES:.v.pica=_shbin.h)

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

# Rebuild main.o when the build ID changes (git hash moves or becomes unknown).
ifeq ($(filter clean,$(MAKECMDGOALS)),)
$(shell mkdir -p $(BUILD))
TWLHD_BUILD_ID_STAMP := $(TWLHD_BUILD_ID_STR)
ifneq ($(strip $(TWLHD_BUILD_ID_STAMP)),$(strip $(shell cat $(BUILD)/buildid.stamp 2>/dev/null)))
$(shell rm -f $(BUILD)/main.o $(BUILD)/main.d)
endif
$(file >$(BUILD)/buildid.stamp,$(strip $(TWLHD_BUILD_ID_STAMP)))
endif

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).elf $(TARGET).smdh

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).3dsx	:	$(OUTPUT).elf
$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	:	$(HFILES_BIN)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h	:	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
.PRECIOUS	:	%.shbin
#---------------------------------------------------------------------------------
%.shbin.o %_shbin.h : %.shbin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
