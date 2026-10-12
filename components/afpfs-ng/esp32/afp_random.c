/* The one place afpfs-ng gets random bytes: the hardware RNG on ESP32,
 * an MbedTLS CTR_DRBG seeded from the OS elsewhere. */

#include "afp_random.h"

#ifdef ESP_PLATFORM

#include "esp_random.h"

void afp_fill_random(void *buffer, size_t length)
{
    esp_fill_random(buffer, length);
}

#else

#include <pthread.h>
#include <stdlib.h>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"

static pthread_once_t seed_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t drbg_lock = PTHREAD_MUTEX_INITIALIZER;
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context drbg;

/* A login handshake must never run on predictable bytes, so failure is fatal. */
static void seed(void)
{
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, NULL, 0) != 0)
        abort();
}

void afp_fill_random(void *buffer, size_t length)
{
    unsigned char *p = (unsigned char *)buffer;

    pthread_once(&seed_once, seed);
    pthread_mutex_lock(&drbg_lock);
    while (length > 0) {
        size_t n = length > MBEDTLS_CTR_DRBG_MAX_REQUEST ? MBEDTLS_CTR_DRBG_MAX_REQUEST : length;
        if (mbedtls_ctr_drbg_random(&drbg, p, n) != 0)
            abort();
        p += n;
        length -= n;
    }
    pthread_mutex_unlock(&drbg_lock);
}

#endif
