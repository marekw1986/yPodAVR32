#include <stdint.h>
#include <stdio.h>
#include <avr32/io.h>
#include <mad.h>

int main(void)
{
    mad_timer_t timer;

    volatile const char *version = mad_version;

    mad_timer_reset(&timer);
    
    volatile uint32_t counter = version[0];

    AVR32_GPIO.port[0].ovr = 0x01;

    while (1)
    {
        counter++;
    }

    return 0;
}
