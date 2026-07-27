#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <avr32/io.h>

#include "sd.h"

unsigned long sd_bytesread, lastsec, init, sdsread = 0;

volatile int readlock, writelock;
char sd_led;

#ifdef USECACHE
#define CACHESECTORS 2
#define CACHESIZE 512
static unsigned char cache[CACHESIZE];
#endif

/* ---- SD card commands ---- */

#define RESET        0   /* a.k.a. GO_IDLE (CMD0) */
#define INIT         1   /* a.k.a. SEND_OP_COND (CMD1) */
#define CMD_SEND_OP_COND 0x01

#define CMD_SEND_IF_COND 0x08

#define READ_SINGLE  17
#define WRITE_SINGLE 24
#define CMD_APP 0x37
#define CMD_SD_SEND_OP_COND 0x29
#define CMD_READ_OCR 0x3a
#define CMD_SET_BLOCKLEN 0x10

/* R1 response bits */
#define R1_IDLE_STATE 0
#define R1_ERASE_RESET 1
#define R1_ILL_COMMAND 2
#define R1_COM_CRC_ERR 3
#define R1_ERASE_SEQ_ERR 4
#define R1_ADDR_ERR 5
#define R1_PARAM_ERR 6

#define SD_RAW_SPEC_1 0
#define SD_RAW_SPEC_2 1
#define SD_RAW_SPEC_SDHC 2

#define R_TIMEOUT 255000
#define W_TIMEOUT 250000
#define I_TIMEOUT 2500

static int sd_raw_card_type;

/* ============================================================
 * Minimal register-level GPIO helpers (no ASF).
 * AVR32 GPIO pins are encoded port*32+bit, e.g. AVR32_PIN_PA10.
 * The GPIO controller exposes plain (rmw) registers plus
 * Set/Clear/Toggle shadow registers for atomic bit ops.
 * ============================================================ */

static inline void gpio_cfg_output(unsigned int pin) {
    unsigned int port = pin >> 5;
    unsigned long mask = 1UL << (pin & 0x1F);
    AVR32_GPIO.port[port].gpers = mask;   /* GPIO (not peripheral) controls pin */
    AVR32_GPIO.port[port].oders = mask;   /* output driver enabled */
}

static inline void gpio_cfg_input(unsigned int pin, int pullup) {
    unsigned int port = pin >> 5;
    unsigned long mask = 1UL << (pin & 0x1F);
    AVR32_GPIO.port[port].gpers = mask;
    AVR32_GPIO.port[port].oderc = mask;
    if (pullup) AVR32_GPIO.port[port].puers = mask;
    else        AVR32_GPIO.port[port].puerc = mask;
}

static inline void gpio_set(unsigned int pin) {
    AVR32_GPIO.port[pin >> 5].ovrs = 1UL << (pin & 0x1F);
}

static inline void gpio_clr(unsigned int pin) {
    AVR32_GPIO.port[pin >> 5].ovrc = 1UL << (pin & 0x1F);
}

static inline int gpio_get(unsigned int pin) {
    return (AVR32_GPIO.port[pin >> 5].pvr >> (pin & 0x1F)) & 1;
}

/* Mux a pin to a peripheral function (A=0,B=1,C=2,D=3) and hand
 * control of the pad from GPIO to the peripheral. Also usable to
 * enable the pad pull-up regardless of GPIO/peripheral mux. */
static inline void gpio_enable_module_pin(unsigned int pin, unsigned int function) {
    unsigned int port = pin >> 5;
    unsigned long mask = 1UL << (pin & 0x1F);

    if (function & 0x1) AVR32_GPIO.port[port].pmr0s = mask;
    else                 AVR32_GPIO.port[port].pmr0c = mask;
    if (function & 0x2) AVR32_GPIO.port[port].pmr1s = mask;
    else                 AVR32_GPIO.port[port].pmr1c = mask;

    AVR32_GPIO.port[port].gperc = mask;   /* peripheral controls pin */
}

static inline void gpio_enable_pin_pull_up(unsigned int pin) {
    AVR32_GPIO.port[pin >> 5].puers = 1UL << (pin & 0x1F);
}

/* ============================================================
 * SPI0 register-level helpers.
 * Field names/masks below match the SPI chapter of the AT32UC3A
 * datasheet (doc32058) and are shared with the AT91/SAM SPI IP.
 * If your toolchain header uses different casing for the struct
 * members (MR vs mr etc.) adjust accordingly -- the bit
 * *positions* are architecturally fixed for this peripheral.
 * ============================================================ */

#define SPI_MR_MSTR      (1UL << 0)
#define SPI_MR_MODFDIS   (1UL << 4)
#define SPI_MR_PCS_SHIFT 16
#define SPI_MR_PCS_NPCS0 (0x0EUL << SPI_MR_PCS_SHIFT)  /* fixed-mode PCS field, unused electrically -- CS is bit-banged */

#define SPI_CR_SPIEN     (1UL << 0)
#define SPI_CR_SPIDIS    (1UL << 1)
#define SPI_CR_SWRST     (1UL << 7)

#define SPI_SR_RDRF      (1UL << 0)
#define SPI_SR_TDRE      (1UL << 1)

#define SPI_CSR_CPOL     (1UL << 0)
#define SPI_CSR_NCPHA    (1UL << 1)   /* mode 0 = CPOL 0, CPHA 0 -> NCPHA = 1 */
#define SPI_CSR_BITS_8   (0UL << 4)
#define SPI_CSR_SCBR_SHIFT 8

static void spi0_set_baud(unsigned long hz) {
    unsigned long scbr = FPBA_HZ / hz;
    if (scbr < 1) scbr = 1;
    if (scbr > 255) scbr = 255;

    SD_SPI.csr0 = SPI_CSR_NCPHA | SPI_CSR_BITS_8 | (scbr << SPI_CSR_SCBR_SHIFT);
}

static void spi0_init_hw(void) {
    /* NOTE: on most UC3A parts the PBA clock to SPI0 is already
     * enabled at reset. If SPI0 doesn't respond, check
     * AVR32_PM.PBAMASK for the SPI0 bit before looking elsewhere. */

    SD_SPI.cr = SPI_CR_SWRST;
    SD_SPI.cr = SPI_CR_SWRST;   /* datasheet recommends writing SWRST twice */

    SD_SPI.mr = SPI_MR_MSTR | SPI_MR_MODFDIS | SPI_MR_PCS_NPCS0;

    spi0_set_baud(250000UL);    /* slow clock for card identification */

    SD_SPI.cr = SPI_CR_SPIEN;
}

/* send one byte, receive one back at the same time (full duplex) */
static unsigned char writeSPI(unsigned char b) {
    while ((SD_SPI.sr & SPI_SR_TDRE) == 0) ;
    SD_SPI.tdr = b;
    while ((SD_SPI.sr & SPI_SR_RDRF) == 0) ;
    return (unsigned char)SD_SPI.rdr;
} /* writeSPI */

/* macros, same shape as the Pico original */
#define disableSD()  gpio_set(SD_PIN_CS); clockSPI()
#define enableSD()   gpio_clr(SD_PIN_CS)
#define readSPI()    writeSPI(0xFF)
#define clockSPI()   writeSPI(0xFF)

static void initSD(void) {
    /* CS: plain GPIO output, bit-banged (see header comment) */
    gpio_cfg_output(SD_PIN_CS);
    gpio_set(SD_PIN_CS);

    /* CD: input, no pull needed unless your switch is open-drain */
    gpio_cfg_input(SD_PIN_CD, 0);

    /* MISO/MOSI/SCK: hand pins to the SPI0 peripheral function */
    gpio_enable_module_pin(SD_PIN_MISO_FUNC());
    gpio_enable_module_pin(SD_PIN_MOSI_FUNC());
    gpio_enable_module_pin(SD_PIN_SCK_FUNC());
    gpio_enable_pin_pull_up(AVR32_SPI0_MISO_0_0_PIN);

    sd_bytesread = 0;
    init = 1;

    spi0_init_hw();
} /* initSD */

void spi_wait_ready(void) {
    uint32_t i;
    for (i = 0; i < 100000; i++) {
        if (writeSPI(0xFF) == 0xFF) break;
    }
}

int sendSDCmd(unsigned char c, unsigned long a) {
    int i, r;
    enableSD();
    if (init) spi_wait_ready();

    writeSPI(c | 0x40);
    writeSPI(a >> 24);
    writeSPI(a >> 16);
    writeSPI(a >> 8);
    writeSPI(a);

    switch (c) {
        case RESET:
            writeSPI(0x95);
            break;
        case CMD_SEND_IF_COND:
            writeSPI(0x87);
            break;
        default:
            writeSPI(0xff);
            break;
    }

    for (i = 0; i < 8; i++) {
        r = readSPI();
        if (r != 0xFF) break;
    }
    return (r);
    /* NOTE CS is still low! */
} /* sendSDCmd */

int sd_init(void) {
    int i, r;

    initSD();

    writelock = readlock = 0;
    sd_raw_card_type = 0;
    lastsec = 0xffffffff;

    disableSD();
    for (i = 0; i < 10; i++) clockSPI();
    enableSD();

    r = sendSDCmd(RESET, 0);
    disableSD();
    if (r != 1) return E_COMMAND_ACK;

    r = sendSDCmd(CMD_SEND_IF_COND, 0x1aa);
    if ((r & (1 << R1_ILL_COMMAND)) == 0) {
        readSPI();
        readSPI();
        if ((readSPI() & 0x01) == 0) return 2;
        if (readSPI() != 0xaa) return 3;

        printf("SD v2 card\r\n");
        sd_raw_card_type |= (1 << SD_RAW_SPEC_2);
    } else {
        sendSDCmd(CMD_APP, 0);
        r = sendSDCmd(CMD_SD_SEND_OP_COND, 0);
        if ((r & (1 << R1_ILL_COMMAND)) == 0) {
            sd_raw_card_type |= (1 << SD_RAW_SPEC_1);
            printf("SD v1 card\r\n");
        } else {
            printf("MMC card\r\n");
        }
    }

    for (i = 0;; ++i) {
        if (sd_raw_card_type & ((1 << SD_RAW_SPEC_1) | (1 << SD_RAW_SPEC_2))) {
            long arg = 0;
            if (sd_raw_card_type & (1 << SD_RAW_SPEC_2)) arg = 0x40000000;
            sendSDCmd(CMD_APP, 0);
            r = sendSDCmd(CMD_SD_SEND_OP_COND, arg);
        } else {
            r = sendSDCmd(CMD_SEND_OP_COND, 0);
        }

        if ((r & (1 << R1_IDLE_STATE)) == 0) break;

        if (i == 0x7fff) {
            disableSD();
            return 4;
        }
    }

    if (sd_raw_card_type & (1 << SD_RAW_SPEC_2)) {
        if (sendSDCmd(CMD_READ_OCR, 0)) {
            disableSD();
            return 5;
        }

        if (readSPI() & 0x40) sd_raw_card_type |= (1 << SD_RAW_SPEC_SDHC);

        readSPI();
        readSPI();
        readSPI();
    }

    if (sendSDCmd(CMD_SET_BLOCKLEN, 512)) {
        disableSD();
        return 6;
    }

    disableSD();

    /* switch to full speed once init handshake is done */
    spi0_set_baud(16000000UL);
    init = 0;
    return 0;
} /* sd_init */

#define DATA_START 0xFE

int sd_readSECTOR(LBA a, char *p) {
    int r, i;

    sd_led = 1;
#ifdef SD_LOCK
    while (readlock) ;
    readlock = 1;
#endif

#ifdef USECACHE
    if (a == lastsec) {
        for (r = 0; r < 512; r++) *p++ = cache[r];
        sd_bytesread = sd_bytesread + 512;
        return 1;
    }
#endif

    lastsec = a;

    if (sd_raw_card_type & (1 << SD_RAW_SPEC_SDHC)) r = sendSDCmd(READ_SINGLE, (a << 9) / 512);
    else r = sendSDCmd(READ_SINGLE, (a << 9));

    if (r == 0) {
        for (i = 0; i < R_TIMEOUT; i++) {
            r = readSPI();
            if (r == DATA_START) break;
        }
        if (i != R_TIMEOUT) {
            i = 0;
            do {
                sdsread++;
#ifdef USECACHE
                cache[i] =
#endif
                *p++ = readSPI();
                i++;
            } while (i < 512);
            sd_bytesread = sd_bytesread + 512;
            readSPI();
            readSPI();
        } else {
            printf("readSECTOR %lu R_TIMEOUT return -2\r\n", a);
            readlock = 0;
            return 0;
        }
    } else {
        printf("readSECTOR %lu cmd rejected return -1\r\n", a);
        readlock = 0;
        return 0;
    }

    disableSD();
#ifdef SD_LOCK
    readlock = 0;
#endif
    return (r == DATA_START);
} /* readSECTOR */

#define DATA_ACCEPT 0x05

int sd_writeSECTOR(LBA a, const char *p) {
    unsigned r, i;
    sd_led = 1;
#ifdef SD_LOCK
    while (writelock) ;
    writelock = 1;
#endif

    if (sd_raw_card_type & (1 << SD_RAW_SPEC_SDHC)) r = sendSDCmd(WRITE_SINGLE, (a << 9) / 512);
    else r = sendSDCmd(WRITE_SINGLE, (a << 9));

    if (r == 0) {
        writeSPI(DATA_START);
        for (i = 0; i < 512; i++) writeSPI(*p++);
        clockSPI();
        clockSPI();
        r = readSPI();
        if ((r & 0xf) == DATA_ACCEPT) {
            for (i = 0; i < W_TIMEOUT; i++) {
                r = readSPI();
                if (r != 0) break;
            }
        } else {
            r = FAIL;
            printf("writeSECTOR %lu failed r=%u\n", a, r);
        }
    } else {
        printf("writeSECTOR %lu cmd rejected r=%u\n", a, r);
    }

    disableSD();
#ifdef SD_LOCK
    writelock = 0;
#endif
    return (r);
} /* writeSECTOR */

int sd_getCD(void) {
    return !gpio_get(SD_PIN_CD);
}
