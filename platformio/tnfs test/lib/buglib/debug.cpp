#include <Arduino.h>
#include "debug.h"

void Debug_Serial()
{
    BUG_UART.begin(DEBUG_SPEED);
}