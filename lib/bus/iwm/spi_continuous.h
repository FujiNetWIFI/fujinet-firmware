#include "sdkconfig.h"

#ifndef CONFIG_IDF_TARGET_ESP32S3

#include <driver/spi_master.h>
#include <soc/lldesc.h>

#ifdef __cplusplus
extern "C" {
#endif

  size_t cspi_alloc_continuous(size_t length, size_t chunk_size,
			       uint8_t **buffer, lldesc_t **desc);
  void cspi_begin_continuous(spi_device_handle_t handle, lldesc_t *desc);
  void cspi_end_continuous(spi_device_handle_t handle);
  size_t cspi_current_pos(spi_device_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_IDF_TARGET_ESP32S3 */
