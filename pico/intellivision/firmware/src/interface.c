
#include <stdbool.h>
#include "pico/stdlib.h"

#include "board.h"
#include "interface.h"

void resetCart(void) {
   gpio_init(MSYNC);
   gpio_set_dir(MSYNC, false);
   gpio_set_pulls(MSYNC, false, true);
   gpio_put(LED, false);

   resetHigh();
   sleep_ms(30);                // was 20 for Model II; 30 works for both

   resetLow();
   sleep_ms(1);                 // was 1 for Model II; 

   resetHigh();
   gpio_put(LED, true);
}


