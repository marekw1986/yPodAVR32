#include "system_time.h"
#include "pm.h"
#include "gpio.h"
#include "ssc_i2s.h"
#include "pdca.h"
#include "intc.h"
#include "wm8731.h"
#include "twi.h"

#define WM8731_ADDR   0x1A   /* 7-bit address — TWI driver shifts it internally,
                                 unlike HAL_I2C which wants it pre-shifted.
                                 Using 0x1A<<1 here would double-shift and
                                 address the wrong chip. */

#define SAMPLE_RATE      44100UL   // pick your actual target; must match a USB-mode table entry — see note below

/* MCLK: OSC0 (12MHz) routed directly out a GCLK pin to WM8731 XTI — no PLL needed in USB mode. */
#define MCLK_GCLK_NUM         0                        // pm_gc_setup() takes just the GCLK number, 0-3 here
#define MCLK_GCLK_PIN          AVR32_PM_GCLK_0_0_PIN    // pin 7 — one of two routing options for GCLK0; confirm against schematic which one is wired to WM8731 XTI
#define MCLK_GCLK_FUNCTION     AVR32_PM_GCLK_0_0_FUNCTION

/* SSC — AVR32 as I2S master, generates BCLK+WS */
#define AUDIO_SSC             (&AVR32_SSC)
#define AUDIO_SSC_TX_PIN      AVR32_SSC_TX_DATA_0_PIN
#define AUDIO_SSC_TX_FUNCTION AVR32_SSC_TX_DATA_0_FUNCTION
#define AUDIO_SSC_TF_PIN      AVR32_SSC_TX_FRAME_SYNC_0_PIN
#define AUDIO_SSC_TF_FUNCTION AVR32_SSC_TX_FRAME_SYNC_0_FUNCTION
#define AUDIO_SSC_TK_PIN      AVR32_SSC_TX_CLOCK_0_PIN
#define AUDIO_SSC_TK_FUNCTION AVR32_SSC_TX_CLOCK_0_FUNCTION

#define AUDIO_PDCA_CHANNEL    0
#define AUDIO_PDCA_PID        AVR32_PDCA_PID_SSC_TX     // check exact macro name in your part header

#define AUDIO_BUFFER_SIZE     4096
static int16_t audio_data[2 * AUDIO_BUFFER_SIZE];
static volatile uint8_t audio_data_needed = 0;

static void wm8731_twi_init(void);
static void wm8731_write_reg(uint8_t reg, uint16_t value);
__attribute__((__interrupt__)) static void audio_pdca_isr(void);

static void audio_mclk_init(void)
{
    static const gpio_map_t MCLK_GPIO_MAP = {
        { MCLK_GCLK_PIN, MCLK_GCLK_FUNCTION },
    };
    gpio_enable_module(MCLK_GPIO_MAP, 1);

    // GCLK sourced directly from OSC0, no PLL, no divider — MCLK = 12.000MHz exactly
    pm_gc_setup(&AVR32_PM, MCLK_GCLK_NUM,
                0,  // osc_or_pll: 0 = use OSC directly, not a PLL
                0,  // pll_osc: irrelevant here since osc_or_pll=0, but selects OSC0 either way
                0,  // diven: no divider — pass OSC0 through unmodified
                0);
    pm_gc_enable(&AVR32_PM, MCLK_GCLK_NUM);
}

static void audio_ssc_init(void)
{
    static const gpio_map_t SSC_GPIO_MAP = {
        { AUDIO_SSC_TX_PIN, AUDIO_SSC_TX_FUNCTION },
        { AUDIO_SSC_TF_PIN, AUDIO_SSC_TF_FUNCTION },
        { AUDIO_SSC_TK_PIN, AUDIO_SSC_TK_FUNCTION },
    };
    gpio_enable_module(SSC_GPIO_MAP, 3);

    ssc_i2s_init(AUDIO_SSC,
                 SAMPLE_RATE,
                 16,                        // data bit resolution
                 32,                        // frame bit resolution (2 x 16-bit slots — L+R)
                 SSC_I2S_MODE_STEREO_OUT,   // AVR32 = I2S master, generates BCLK+WS from PBA clock
                 FPBA_HZ);
}

static void audio_pdca_init(void)
{
    static const pdca_channel_options_t PDCA_OPT = {
        .addr         = audio_data,
        .size         = AUDIO_BUFFER_SIZE,          // first half, in halfwords
        .r_addr       = audio_data + AUDIO_BUFFER_SIZE,  // second half queued as reload
        .r_size       = AUDIO_BUFFER_SIZE,
        .pid          = AUDIO_PDCA_PID,
        .transfer_size = PDCA_TRANSFER_SIZE_HALF_WORD,
    };

    pdca_init_channel(AUDIO_PDCA_CHANNEL, &PDCA_OPT);
    INTC_register_interrupt(&audio_pdca_isr, AVR32_PDCA_IRQ_0 + AUDIO_PDCA_CHANNEL, AVR32_INTC_INT0);
    pdca_enable_interrupt_transfer_complete(AUDIO_PDCA_CHANNEL);
    pdca_enable(AUDIO_PDCA_CHANNEL);
}

/* PDCA fires transfer-complete once per half-buffer boundary — this is the direct
   analog of HAL_I2S_TxHalfCpltCallback / HAL_I2S_TxCpltCallback, just collapsed
   into a single recurring event instead of two distinct ones. */
__attribute__((__interrupt__)) static void audio_pdca_isr(void) {
    static uint8_t which_half = 0;

    // Whichever half PDCA just finished playing needs refilling and re-queuing
    // as the *next* reload target, same ping-pong your original code did via
    // audio_data_needed + wm8731_handle().
    if (which_half == 0)
        audio_data_needed = 1;
    else
        audio_data_needed = 2;

    which_half ^= 1;

    pdca_reload_channel(AUDIO_PDCA_CHANNEL,
                         which_half == 0 ? audio_data : audio_data + AUDIO_BUFFER_SIZE,
                         AUDIO_BUFFER_SIZE);
}

void wm8731_set_in_volume(int vol) {
    // -23 <= vol <= 8
    const unsigned involume = 0x17 + vol;
    wm8731_write_reg(0x00, 0x100 | (involume & 0x1f)); // Left line in, unmute
}

void wm_8731_set_out_volume(int voldB) {
    // -73 <= voldB <= 6
    const unsigned volume = 121 + voldB;
    wm8731_write_reg(0x02, 0x100 | (volume & 0x7f)); // Left headphone
    wm8731_write_reg(0x03, 0x100 | (volume & 0x7f)); // Right headphone
}

void wm8731_handle(void)
{
    if (audio_data_needed == 1) {
        //hxcmod_fillbuffer(&modctx, audio_data, AUDIO_BUFFER_SIZE / 2, NULL);
        audio_data_needed = 0;
    } else if (audio_data_needed == 2) {
        //hxcmod_fillbuffer(&modctx, audio_data + AUDIO_BUFFER_SIZE, AUDIO_BUFFER_SIZE / 2, NULL);
        audio_data_needed = 0;
    }
}

void wm8731_init(void)
{
    wm8731_twi_init();
    
    wm8731_write_reg(0x0f, 0b000000000);
    wm8731_set_in_volume(0);
    wm_8731_set_out_volume(-10);
    wm8731_write_reg(0x04, 0b000010010);
    wm8731_write_reg(0x05, 0b000000001);
    wm8731_write_reg(0x06, 0b000000000);
    wm8731_write_reg(0x07, 0b000000010);   // 16-bit I2S — unchanged, format is protocol-level not clock-level

    // NEW vs. original: explicitly select USB mode + the SR bits for your chosen
    // rate. Get the exact SR[3:0] value from the WM8731 USB-mode sample-rate
    // table (datasheet, "USB MODE SAMPLE RATES") for 44.1kHz — I don't have that
    // 4-bit value memorized confidently enough to assert it here.
    //wm8731_write_reg(0x08, 0b0000000?1);   // USB=1, SR[3:0]=??? — fill from the table
    wm8731_write_reg(0x08, 0b000000001);   // USB=1, SR[3:0]=??? — fill from the table

    wm8731_write_reg(0x09, 0b000000001);

    audio_mclk_init();
    audio_ssc_init();
    audio_pdca_init();

    //hxcmod_init(&modctx);
    //hxcmod_setcfg(&modctx, SAMPLE_RATE, 0, 0);
    //hxcmod_load(&modctx, (void*)minimalistic_mod, minimalistic_mod_size);
    //hxcmod_fillbuffer(&modctx, audio_data, AUDIO_BUFFER_SIZE, NULL);
}

static void wm8731_twi_init(void) {
    const twi_options_t opt = {
        .pba_hz = FPBA_HZ,   // must match whatever clock_init() actually configured PBA to
        .speed  = 100000,    // standard-mode I2C; WM8731 supports up to 400kHz if you want faster
        .chip   = WM8731_ADDR,
    };
    twi_master_init(&AVR32_TWI, &opt);
}

static void wm8731_write_reg(uint8_t reg, uint16_t value) {
    uint16_t tmp = ((uint16_t)reg << 9) | value;
    uint8_t data[2] = { (tmp & 0xFF00) >> 8, tmp & 0x00FF };

    twi_package_t packet = {
        .chip        = WM8731_ADDR,
        .addr_length = 0,     // WM8731 has no register-pointer phase — reg+value is packed into the 2 data bytes themselves
        .buffer      = data,
        .length      = 2,
    };

    twi_master_write(&AVR32_TWI, &packet);
}
