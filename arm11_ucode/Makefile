TARGET  := twlhd_ucode
CC      := arm-none-eabi-gcc
AS      := arm-none-eabi-as
OBJCOPY := arm-none-eabi-objcopy
NM      := arm-none-eabi-nm
CFLAGS  := -mcpu=mpcore -mfloat-abi=soft -nostartfiles -nostdlib \
            -fno-builtin -Wall -O2
ASFLAGS := -mcpu=mpcore
LDFLAGS := -T arm11ucode.ld -nostartfiles -nostdlib

SRCS    := source/main.c source/ucode_entry.s
OBJS    := build/main.o build/ucode_entry.o

.PHONY: all clean

all: build $(TARGET).uc11

build:
	mkdir -p build

build/main.o: source/main.c
	$(CC) $(CFLAGS) -c $< -o $@

build/ucode_entry.o: source/ucode_entry.s
	$(AS) $(ASFLAGS) $< -o $@

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@
	@echo "Checking that _start is at the blob base..."
	@$(NM) -n $(TARGET).elf | awk '$$NF == "_start" {addr=$$1} END { if (addr != "00201000") { print "Error: _start is not at base address 0x00201000 (saw 0x" addr ")"; exit 1 } }'
	@echo "Entry-point check passed."

$(TARGET).uc11: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf build $(TARGET).elf $(TARGET).uc11
