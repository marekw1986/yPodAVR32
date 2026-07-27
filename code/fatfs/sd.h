#ifndef SD_H
#define SD_H

#define FAIL    0

/* Init ERROR code definitions */
#define E_COMMAND_ACK     0x80
#define E_INIT_TIMEOUT    0x81

/* ---- Board wiring : AT32UC3A1512, SPI0 ---- */
#define SD_SPI          AVR32_SPI0

/* MISO/MOSI/SCK go to the SPI0 peripheral function.
 * Verify these three macros exist in your toolchain's
 * <avr32/uc3a1512.h> -- some packages expose more than one
 * mux option (_0_0, _0_1 ...) for the same signal; pick the
 * one that matches the pins actually wired on your board. */
#define SD_PIN_MISO_FUNC()   AVR32_SPI0_MISO_0_0_PIN, AVR32_SPI0_MISO_0_0_FUNCTION
#define SD_PIN_MOSI_FUNC()   AVR32_SPI0_MOSI_0_0_PIN, AVR32_SPI0_MOSI_0_0_FUNCTION
#define SD_PIN_SCK_FUNC()    AVR32_SPI0_SCK_0_0_PIN,  AVR32_SPI0_SCK_0_0_FUNCTION

/* CS is driven manually as plain GPIO (see rationale in .c file),
 * NOT muxed to the SPI0 NPCS0 peripheral function, even though
 * NPCS0 lands on the same physical pin (PA10). */
#define SD_PIN_CS       AVR32_PIN_PA10

/* Card detect switch -- adjust to your wiring */
#define SD_PIN_CD       AVR32_PIN_PA11

/* PBA clock feeding SPI0 on your board, in Hz.
 * MUST match your actual PM/OSC configuration or the computed
 * SCBR divisors (and therefore the SPI bit rate) will be wrong. */
#ifndef FPBA_HZ
#define FPBA_HZ         48000000UL
#endif

extern unsigned long sdsread, sd_bytesread;
extern char sd_led;

typedef unsigned long LBA;     /* logic block address, 32 bit wide */

int sd_init(void);                       /* initializes the SD/MMC memory device */
int sd_getCD(void);                      /* check card presence */
int sd_getWP(void);                      /* check write protection tab */
int sd_readSECTOR(LBA, char *);          /* reads a block of data */
int sd_writeSECTOR(LBA, const char *);   /* writes a block of data */

#endif /* SD_H */
