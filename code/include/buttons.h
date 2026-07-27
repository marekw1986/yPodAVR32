
#ifndef _BUTTONS_H_
#define _BUTTONS_H_

#include <stdint.h>

#define LONG_WAIT 1500

typedef struct {
    volatile uint32_t *pin;
    uint32_t mask;
    uint32_t timer;
    uint8_t state;
    void (*push_proc)(void);
    void (*long_proc)(void);
} button_t;

void key_init(button_t *key, volatile uint32_t *pin, uint32_t mask, void (*push_proc)(void), void (*long_proc)(void));
void key_update (button_t *key);

#endif // _BUTTONS_H_
