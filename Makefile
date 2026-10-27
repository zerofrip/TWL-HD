.PHONY: all arm9 arm11 clean

all: arm9 arm11

arm9:
	$(MAKE) -C arm9

arm11:
	$(MAKE) -C arm11

clean:
	$(MAKE) -C arm9 clean
	$(MAKE) -C arm11 clean
