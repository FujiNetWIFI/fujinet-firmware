#ifndef CYD_TOUCH_H
#define CYD_TOUCH_H

/* FT6336 capacitive touch controller on I2C, reported in the same landscape
 * coordinates as cyd_lcd (320x240). Call from the CYD UI task only. */

bool cyd_touch_init();

// Returns true while the panel is touched, with the first touch point
bool cyd_touch_read(int *x, int *y);

#endif // CYD_TOUCH_H
