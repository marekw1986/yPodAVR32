#include <avr32/io.h>
#include <stdint.h>
#include "compiler.h"
#include "intc.h"
#include "system_time.h"

#define F_CPU 66000000UL
#define TICK_HZ 1000UL

volatile static uint32_t milliseconds = 0;

volatile static uint32_t tick_increment;
volatile static uint32_t next_compare;

__attribute__((__interrupt__)) static void compare_isr(void);

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
