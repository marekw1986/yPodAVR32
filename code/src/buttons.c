#include <avr32/io.h>
#include "buttons.h"
#include "time.h"

void key_init(button_t *key, volatile uint32_t *pin, uint32_t mask, void (*push_proc)(void), void (*long_proc)(void))
{
    key->pin = pin;
    key->mask = mask;
    key->timer = 0;
    key->state = 0;
    key->push_proc = push_proc;
    key->long_proc = long_proc;
}

void key_update (button_t *key) {
	unsigned char key_press;
	enum {idle, debounce, wait_long, wait_release};
    
    key_press = !((*key->pin) & key->mask);

	
	if(key_press && !(key->state)) {
		key->state = debounce;
		key->timer = millis();
	} else if (key->state) {
	  if(key_press && (key->state)==debounce && ((unsigned int)(millis()-(key->timer)) >= 4)) {
			key->state = wait_long;
			key->timer = millis();
		}
	  else if (!key_press && (key->state)==wait_long) {
	    if (key->push_proc) (key->push_proc)();
 			key->state = idle;
		}
	  else if (key_press && (key->state)==wait_long && ((unsigned int)(millis()-(key->timer)) >= (50+LONG_WAIT))) {
	    if(key->long_proc) (key->long_proc)();
			key->state = wait_release;
			key->timer = 0; 
		}
  	}
  	if ((key->state)==wait_release && !key_press) key->state = idle;	
}
