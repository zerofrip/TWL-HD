.PHONY: all arm7 arm9 arm11 arm11_ucode rtcom_rom clean

all: arm7 arm9 arm11 arm11_ucode

arm7:
	$(MAKE) -C arm7

arm9:
	$(MAKE) -C arm9

arm11:
	$(MAKE) -C arm11

arm11_ucode:
	$(MAKE) -C arm11_ucode

# Pack the ARM9 ELF with THIS project's custom ARM7 binary.  The default
# `make -C arm9` target still links the stock devkitPro ARM7 for Phase 1/2 users.
rtcom_rom: arm9 arm7
	ndstool -9 arm9/twlhd-arm9.elf -7 arm7/twlhd_arm7.elf -c twlhd-rtcom.nds

clean:
	$(MAKE) -C arm7 clean
	$(MAKE) -C arm9 clean
	$(MAKE) -C arm11 clean
	$(MAKE) -C arm11_ucode clean
	rm -f twlhd-rtcom.nds
