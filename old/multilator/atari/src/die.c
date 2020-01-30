/**
 * FujiNet Configurator
 *
 * Function to die, wait for keypress and then do coldstart
 */

#include <conio.h>

/**
 * Stop, wait for keypress, then coldstart
 */
void die(void)
{
  while (!kbhit()) { }
  asm("jmp $E477");
}

