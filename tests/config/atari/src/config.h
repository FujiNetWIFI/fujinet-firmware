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
 * Run Wifi scan and Configuration
 */
void config_run(void);

#endif /* CONFIG_H */
