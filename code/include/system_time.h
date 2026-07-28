#ifndef _SYSTEM_TIME_H_
#define _SYSTEM_TIME_H_

#include <stdint.h>

#define FPBA_HZ 66000000UL   // matches clock_init()'s pm_cksel(...,0,0,0,0,0,0) — PBA runs at full CPU rate, no divider

void clock_init(void);
void system_time_init(void);
uint32_t millis (void);

#endif // _SYSTEM_TIME_H_
