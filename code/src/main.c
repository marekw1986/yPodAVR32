#include <stdint.h>
#include <stdio.h>
#include <avr32/io.h>
#include <mad.h>
#include <ff.h>
#include "intc.h"
#include "wdt.h"
#include "system_time.h"
#include "pcd8544.h"
#include "wm8731.h"

FATFS SDFat;

int main(void)
{
    mad_timer_t timer;

    volatile const char *version = mad_version;

    mad_timer_reset(&timer);
    
    volatile uint32_t counter = version[0];

    AVR32_GPIO.port[0].ovr = 0x01;
    
    clock_init();
    
    INTC_init_interrupts();
    system_time_init();
    PCD_Ini();
    wm8731_init();
    
    /*FRESULT res = */f_mount(&SDFat, "0:", 0);
    
    wdt_opt_t opt = {.us_timeout_period = 2000000};
    wdt_enable(&opt);

    while (1)
    {
        counter++;
        wm8731_handle();
        wdt_clear();
    }

    return 0;
}
