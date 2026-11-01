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
