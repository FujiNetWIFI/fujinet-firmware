/*
//                       PiRTO II Flash MultiCART by Andrea Ottaviani 2024
//
//  Intellivision flash multicart based on Raspberry Pico board -
//
//  More info on https://github.com/aotta/ 
//
//   parts of code are directly from the A8PicoCart project by Robin Edwards 2023
//  
//   Needs to be a release NOT debug build for the cartridge emulation to work
// 
//   Edit myboard.h depending on the type of flash memory on the pico clone//
//
//   v. 1.0 2024-05-04 : Initial version for Pi Pico 
//
*/


#ifndef __INTY_CART_H__
#define __INTY_CART_H__

#define MSYNC_PIN 19
#define RST_PIN   20

void Inty_cart_main();
void resetCart();


#endif