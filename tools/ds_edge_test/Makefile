# SPDX-License-Identifier: Apache-2.0
#---------------------------------------------------------------------------------
# Static DS top-screen 3D edge-row origin test ROM. Same devkitARM/libnds
# flow as arm9/ and arm9v2/; no TWL-HD GX or common/ code is linked.
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
TARGET		:=	ds-edge-test
BUILD		:=	build
SOURCES		:=	source
INCLUDES	:=	include

#---------------------------------------------------------------------------------
ARCH	:=	-mthumb -mthumb-interwork

CFLAGS	:=	-g -Wall -O2\
		-march=armv5te -mtune=arm946e-s -fomit-frame-pointer\
		-ffast-math \
		$(ARCH)

CFLAGS		+=	$(INCLUDE) -DARM9

ASFLAGS		:=	-g $(ARCH)
LDFLAGS		=	-specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS		:=	-lnds9
LIBDIRS		:=	$(LIBNDS)

#---------------------------------------------------------------------------------
# Recorder variant: the same static scene, but every GFX_* / MATRIX_* write goes
# through common/twlhd_gx_record.c and the completed frame is written to
# /twlhd_frame_v2.bin (record/main.cpp; the dump is arm9v2/source/main.cpp
# :240-268). It links common/ code and libfat, so the plain-ROM target and the
# "no common/ code" assertion in test_quads.py stay scoped to ds-edge-test.
#---------------------------------------------------------------------------------
RECORD	?=	0
ifeq ($(RECORD),1)
TARGET		:=	ds-edge-record
BUILD		:=	build-record
SOURCES		:=	record ../../common
INCLUDES	:=	include ../../common ../../arm9v2/include
LIBS		:=	-lnds9 -lfat
CXXFLAGS	:=	$(CFLAGS) -fno-rtti -fno-exceptions
endif

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES		:=	$(OFILES_SRC)

export INCLUDE		:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS		:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean all record

#---------------------------------------------------------------------------------
all: $(BUILD)

# Recursive top-level make so RECORD=1 reaches the build-directory sub-make.
record:
	@$(MAKE) --no-print-directory -C $(CURDIR) RECORD=1 all

include/quads.h: quads.json gen_quads.py
	python3 $(CURDIR)/gen_quads.py $(CURDIR)/quads.json $(CURDIR)/include/quads.h

$(BUILD): include/quads.h
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr build build-record $(TARGET).elf $(TARGET).nds $(TARGET).arm9 \
		ds-edge-record.elf ds-edge-record.nds ds-edge-record.arm9

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
$(OUTPUT).nds	:	$(OUTPUT).elf
$(OUTPUT).elf	:	$(OFILES)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
