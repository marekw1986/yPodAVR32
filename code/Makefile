TOOLCHAIN = /opt/microchip/avr32-gnu-toolchain-linux_x86_64

CC      = $(TOOLCHAIN)/bin/avr32-gcc
OBJCOPY = $(TOOLCHAIN)/bin/avr32-objcopy
SIZE    = $(TOOLCHAIN)/bin/avr32-size

ASF = /opt/microchip/xdk-asf-3.50.0

LIBMAD_DIR = $(ASF)/thirdparty/libmad/libs/at32ucr2/gcc

INCLUDES = \
	-Ifatfs \
	-Iinclude \
	-I$(ASF)/avr32/utils/header_files \
	-I$(ASF)/thirdparty/libmad \
	-I$(ASF)/avr32/utils \
	-I$(ASF)/avr32/utils/preprocessor \
	-I$(ASF)/avr32/drivers \
	-I$(ASF)/avr32/drivers/intc \
	-I$(ASF)/common/utils \

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
    $(INCLUDES) \

LDFLAGS = \
    -march=ucr2 \
    -mpart=uc3a1512 \
    -mrelax \
    -Wl,--defsym,__heap_size__=0x400 \
    -Wl,-T,link_uc3a1512.lds \
    -Wl,--gc-sections \
	-Wl,-Map,$(TARGET).map,--cref
	
LIBS = \
    $(LIBMAD_DIR)/libmad-at32ucr2-balanced_opt.a
    

BUILD = build
TARGET = $(BUILD)/ypod

CSRCS = \
	src/main.c \
	src/system_time.c \
	src/buttons.c \
	fatfs/sd.c \
	fatfs/diskio.c \
	fatfs/ff.c \
	$(ASF)/avr32/drivers/intc/intc.c \
	
ASRCS = \
	$(ASF)/avr32/drivers/intc/exception.S \

OBJS = $(CSRCS:%.c=$(BUILD)/%.o) $(patsubst /%.S,$(BUILD)/%.o,$(ASRCS))

all: $(TARGET).elf $(TARGET).bin $(TARGET).hex

$(TARGET).elf: $(OBJS)
	mkdir -p $(BUILD)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
	$(SIZE) $@

$(BUILD)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
	
$(BUILD)/%.o: /%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
