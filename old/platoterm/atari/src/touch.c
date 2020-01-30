/**
 * PLATOTERM for Atari Cartridges
 *
 * Author: Thomas Cherryhomes <thom.cherryhomes at gmail dot com>
 *
 * Touch Routines
 */


#include <mouse.h>
#include <stdbool.h>
#include <atari.h>
#include "protocol.h"
#include "touch.h"

static struct mouse_info mouse_data;
static padBool touch_allowed=false;

/* unsigned short touch_scale_320(short x) */
/* { */
/*    uint16_t n, q; */
/*    n  = x << 3; */
/*    q  = x + (x >> 1); */
/*    q += q >> 4; */
/*    q += q >> 8; */
/*    n -= q << 2; */
/*    n -= q; */
/*    n += ((n << 1) + n) << 2; */
/*    return q + (n >> 6); */
/* } */

/* unsigned short touch_scale_192(short y) */
/* { */
/*   uint16_t n, q; */
/*   n  = y << 3; */
/*   q  = ((y << 1) + n) >> 2; */
/*   q += q >> 4; */
/*   q += q >> 8; */
/*   n -= q << 1; */
/*   n -= q; */
/*   n += ((n << 2) + n) << 1; */
/*   return (q + (n >> 5) ^ 0x1FF); */
/* } */


/**
 * touch_init() - Set up touch screen
 */
void touch_init(void)
{
  /* mouse_install(&mouse_def_callbacks,atrjoy_mou); */
  /* mouse_show(); */
}

/**
 * touch_main() - Main loop for touch screen
 */
void touch_main(void)
{
  /* uint8_t lastbuttons; */
  /* padPt coord; */

  /* mouse_info(&mouse_data); */
  
  /* if (mouse_data.buttons == lastbuttons) */
  /*   return; /\* debounce *\/ */
  /* else if ((mouse_data.buttons & MOUSE_BTN_LEFT)) */
  /*   { */
  /*     coord.x=mouse_data.pos.x; */
  /*     coord.y=mouse_data.pos.y; */
  /*     touch_translate(&coord); */
  /*     Touch(&coord); */
  /*   } */
  /* lastbuttons = mouse_data.buttons; */
}

/**
 * touch_allow - Set whether touchpanel is active or not.
 */
void touch_allow(padBool allow)
{
  /* if (touch_allowed==false && allow==true) */
  /*   { */
  /*     touch_allowed=true; */
  /*     mouse_show(); */
  /*   } */
  /* else if (touch_allowed==true && allow==false) */
  /*   { */
  /*     touch_allowed=false; */
  /*     mouse_hide(); */
  /*   } */
}

/**
 * touch_translate - Translate coordinates from native system to PLATO
 */
void touch_translate(padPt* Coord)
{
  /* Coord->x = touch_scale_320(Coord->x); */
  /* Coord->y = touch_scale_192(Coord->y); */
}

/**
 * touch_done() - Stop the mouse driver
 */
void touch_done(void)
{
  /* mouse_uninstall(); */
}
