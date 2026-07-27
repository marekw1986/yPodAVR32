#include <stdint.h>
#include <stdio.h>
#include <avr32/io.h>

int main(void)
{
    volatile uint32_t counter = 0;

    AVR32_GPIO.port[0].ovr = 0x01;

    while (1)
    {
        counter++;
    }

    return 0;
}
