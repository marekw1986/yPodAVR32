# ------------------------------------------------------------
# AVR32 toolchain
# ------------------------------------------------------------

TOOLCHAIN = /opt/microchip/avr32-gnu-toolchain-linux_x86_64

CC      = $(TOOLCHAIN)/bin/avr32-gcc
SIZE    = $(TOOLCHAIN)/bin/avr32-size
OBJCOPY = $(TOOLCHAIN)/bin/avr32-objcopy


# ------------------------------------------------------------
# ASF
# ------------------------------------------------------------

ASF = /opt/microchip/xdk-asf-3.50.0

INCLUDES = \
	-I$(ASF)/avr32/utils/header_files \
	-I$(ASF)/avr32/utils/preprocessor


# ------------------------------------------------------------
# MCU
# ------------------------------------------------------------

MCUFLAGS = \
	-march=ucr2 \
	-D__AVR32_UC3A1512__


# ------------------------------------------------------------
# Compiler flags
# ------------------------------------------------------------

CFLAGS = \
	$(MCUFLAGS) \
	$(INCLUDES) \
	-Os \
	-g \
	-Wall


LDFLAGS = \
	$(MCUFLAGS)


# ------------------------------------------------------------
# Project
# ------------------------------------------------------------

TARGET = test

SRCS = main.c
OBJS = $(SRCS:.c=.o)


# ------------------------------------------------------------
# Build rules
# ------------------------------------------------------------

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
	rm -f *.o *.elf *.bin *.hex


.PHONY: all clean
