/**
 * FujiNet Configuration Program
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

/**
 * Is device configured?
 */
bool configured(void);

/**
 * Connect to configured network
 */
bool config_connect(void);

/**
 * Run Wifi scan and Configuration
 */
void config_run(void);

#endif /* CONFIG_H */
