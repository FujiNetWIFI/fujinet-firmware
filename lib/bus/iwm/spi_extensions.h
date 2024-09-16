#ifndef _SPI_BETTER_H

#include "sdkconfig.h"

#include <driver/spi_master.h>
#include <soc/lldesc.h>

#ifdef __cplusplus
extern "C" {
#endif

  lldesc_t *fspi_alloc_linked_list(uint8_t *buffer, size_t length, size_t chunk_size);
  void spi_open_receive(spi_device_handle_t handle);
  void spi_close_receive(spi_device_handle_t handle);
  void spi_trigger_receive(spi_device_handle_t handle, lldesc_t *desc, size_t bit_length);

#ifdef __cplusplus
}
#endif

#endif /* _SPI_BETTER_H */
