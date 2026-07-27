#include <stdint.h>
#include "system_time.h"

volatile uint32_t milliseconds = 0;

void time_init(void) {
//TODO: Add initialization
}

uint32_t millis(void) {
    return milliseconds;
}

//TODO: Add ISR
