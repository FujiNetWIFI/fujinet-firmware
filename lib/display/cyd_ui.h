#ifndef CYD_UI_H
#define CYD_UI_H

// On-screen status UI for CYD boards (FUJINET_CYD). Starts its own
// low-priority task; call once after the bus and WiFi are set up.
void cyd_ui_start();

#endif // CYD_UI_H
