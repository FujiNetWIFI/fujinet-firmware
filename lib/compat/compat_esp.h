#ifndef COMPAT_ESP_H
#define COMPAT_ESP_H

#ifndef ESP_PLATFORM

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ      100
#endif

#ifndef portTICK_PERIOD_MS
#define portTICK_PERIOD_MS      (1000 / configTICK_RATE_HZ)
#endif

#endif /* !ESP_PLATFORM */

#endif /* COMPAT_ESP_H */
