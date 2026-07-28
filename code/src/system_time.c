#include <avr32/io.h>
#include <stdint.h>
#include "compiler.h"
#include "intc.h"
#include "pm.h"
#include "flashc.h"
#include "system_time.h"

#define FOSC0        12000000UL
#define OSC0_STARTUP AVR32_PM_OSCCTRL0_STARTUP_2048_RCOSC  // check your board's actual startup define

#define F_CPU 66000000UL
#define TICK_HZ 1000UL

volatile static uint32_t milliseconds = 0;

volatile static uint32_t tick_increment;
volatile static uint32_t next_compare;

__attribute__((__interrupt__)) static void compare_isr(void);

void clock_init(void) {
    // Bring up OSC0 (external crystal) and switch main clock to it first
    pm_switch_to_osc0(&AVR32_PM, FOSC0, OSC0_STARTUP);

    // --- PLL0: CPU/HSB/PBA @ 66 MHz ---
    pm_pll_setup(&AVR32_PM,
                 0,    // pll: PLL0
                 10,   // mul: MUL+1 = 11
                 1,    // div: PLLDIV = 1
                 0,    // osc: source OSC0
                 16);  // lockcount

    pm_pll_set_option(&AVR32_PM,
                       0,   // pll: PLL0
                       0,   // pll_freq: VCO range select — verify against the PLLOPT[0] figure for a 132MHz VCO before trusting this
                       1,   // pll_div2: enable /2 → 132MHz VCO -> 66MHz
                       0);  // wbwdisable

    pm_pll_enable(&AVR32_PM, 0);
    pm_wait_for_pll0_locked(&AVR32_PM);

    // Flash needs a wait state above ~33MHz — set BEFORE switching CPU to the PLL
    flashc_set_wait_state(1);

    // PBA/PBB/HSB dividers — 0 = run at full CPU rate; raise if a peripheral needs it slower
    pm_cksel(&AVR32_PM, 0, 0, 0, 0, 0, 0);

    pm_switch_to_clock(&AVR32_PM, AVR32_PM_MCCTRL_MCSEL_PLL0);

    // --- PLL1: dedicated 48MHz for USB ---
    pm_pll_setup(&AVR32_PM,
                 1,    // pll: PLL1
                 7,    // mul: MUL+1 = 8
                 1,    // div: PLLDIV = 1
                 0,    // osc: source OSC0 (same crystal, second PLL)
                 16);  // lockcount

    pm_pll_set_option(&AVR32_PM, 1, 0, 1, 0);  // same VCO-range caveat as above; 96MHz VCO / 2 = 48MHz

    pm_pll_enable(&AVR32_PM, 1);
    pm_wait_for_pll1_locked(&AVR32_PM);

    pm_gc_setup(&AVR32_PM, AVR32_PM_GCLK_USBB,
                1,  // osc_or_pll: use PLL
                1,  // pll_osc: select PLL1
                0,  // diven: no extra divider — PLL1 already outputs exactly 48MHz
                0);
    pm_gc_enable(&AVR32_PM, AVR32_PM_GCLK_USBB);
}

void system_time_init(void) {
    tick_increment = F_CPU / TICK_HZ;

    /* INTC_init_interrupts() must already have been called once in main(). */
    INTC_register_interrupt(&compare_isr, AVR32_CORE_COMPARE_IRQ, AVR32_INTC_INT0);

    Set_system_register(AVR32_COUNT, 0);
    next_compare = tick_increment;
    Set_system_register(AVR32_COMPARE, next_compare);
}

uint32_t millis(void) {
    return milliseconds;
}

__attribute__((__interrupt__)) static void compare_isr(void) {
    milliseconds++;
    next_compare += tick_increment;
    Set_system_register(AVR32_COMPARE, next_compare);
}
