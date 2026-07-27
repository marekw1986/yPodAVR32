TOOLCHAIN = /opt/microchip/avr32-gnu-toolchain-linux_x86_64

CC      = $(TOOLCHAIN)/bin/avr32-gcc
OBJCOPY = $(TOOLCHAIN)/bin/avr32-objcopy
SIZE    = $(TOOLCHAIN)/bin/avr32-size

ASF = /opt/microchip/xdk-asf-3.50.0

CFLAGS = \
    -march=ucr2 \
    -mpart=uc3a1512 \
    -D__AVR32_UC3A1512__ \
    -Os \
    -g \
    -Wall \
    -ffunction-sections \
    -fdata-sections \
    -mrelax \
    -I$(ASF)/avr32/utils/header_files

LDFLAGS = \
    -march=ucr2 \
    -mpart=uc3a1512 \
    -mrelax \
    -Wl,--defsym,__heap_size__=0x400 \
    -Wl,-T,link_uc3a1512.lds \
    -Wl,--gc-sections\
	-Wl,-Map,test.map,--cref

TARGET = test

SRCS = main.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET).elf $(TARGET).bin $(TARGET).hex

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^
	$(SIZE) $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

clean:
	rm -f *.o *.elf *.bin *.hex *.map

.PHONY: all clean
