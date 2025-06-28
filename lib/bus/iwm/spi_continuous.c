#include "spi_continuous.h"

#ifndef CONFIG_IDF_TARGET_ESP32S3

#include <esp_log.h>
#include <esp_private/spi_common_internal.h>
#include <hal/spi_hal.h>
#include <freertos/queue.h>

// FIXME - structs that had to be copied from ESPIDF components/driver/spi_master.c
//         these structs are needed in order to tear down a
//         spi_device_handle_t to access the selected SPI peripheral
//         registers

// spi_device_t and spi_host_t reference each other
typedef struct spi_device_t spi_device_t;
//spi_device_handle_t is a pointer to spi_device_t

typedef struct {
  spi_transaction_t *trans;
  const uint32_t *buffer_to_send;
  uint32_t *buffer_to_rcv;
} spi_trans_priv_t;

typedef struct {
  int id;
  spi_device_t *device[DEV_NUM_MAX];
  intr_handle_t intr;
  spi_hal_context_t hal;
  spi_trans_priv_t cur_trans_buf;
  int cur_cs;
  const spi_bus_attr_t *bus_attr;
  spi_device_t *device_acquiring_lock;

  //debug information
  bool polling;   //in process of a polling, avoid of queue new transactions into ISR
} spi_host_t;

struct spi_device_t {
  int id;
  QueueHandle_t trans_queue;
  QueueHandle_t ret_queue;
  spi_device_interface_config_t cfg;
  spi_hal_dev_config_t hal_dev;
  spi_host_t *host;
  spi_bus_lock_dev_handle_t dev_lock;
};

// END spi_master.c structs

/* Allocates a DMA buffer that is at least length long but is an even
   multiple of chunk_size. Sets up a circular linked list with each
   element pointing to a successive section of buffer that is only
   chunk_size long. The last element in the list points back to the
   first and no element has eof set.

   Returns length of allocated buffer or 0 on error.
*/
size_t cspi_alloc_continuous(size_t length, size_t chunk_size,
			     uint8_t **buffer, lldesc_t **desc)
{
  uint32_t num_chunks, idx;
  lldesc_t *llfirst, *llcur;
  uint8_t *newbuf;
  size_t length2, avail;


  *buffer = NULL;
  *desc = NULL;

  /* Make sure it's an even multiple of chunk_size */
  num_chunks = (length + chunk_size - 1) / chunk_size;
  length = num_chunks * chunk_size;
  length2 = sizeof(lldesc_t) * num_chunks;
  avail = heap_caps_get_free_size(MALLOC_CAP_DMA);
  if (avail < length + length2) {
    ESP_LOGE("SPI_SMA", "Insuffucient RAM %u < %u + %u", avail, length, length2);
    return 0;
  }

  newbuf = (uint8_t *) heap_caps_realloc(*buffer, length, MALLOC_CAP_DMA);
  if (!newbuf) {
    ESP_LOGE("SPI_DMA", "Failed to allocate DMA descriptor 1: %u", length);
    return 0;
  }
  *buffer = newbuf;

  llfirst = (lldesc_t *) heap_caps_realloc(*desc, length2, MALLOC_CAP_DMA);
  if (!llfirst) {
    ESP_LOGE("SPI_DMA", "Failed to allocate DMA descriptor 2: %u", length2);
    return 0;
  }
  *desc = llfirst;

  memset(llfirst, 0, length2);
  for (idx = 0; idx < num_chunks; idx++) {
    llcur = &llfirst[idx];
    llcur->size = chunk_size;
    llcur->owner = 1; // Owned by DMA hardware
    llcur->buf = &newbuf[idx * chunk_size];
    llcur->qe.stqe_next = &llfirst[(idx + 1) % num_chunks];
  }

  return length;
}

/* Put SPI peripheral into continuous capture mode */
void cspi_begin_continuous(spi_device_handle_t handle, lldesc_t *desc)
{
  spi_transaction_t rxtrans;
  spi_host_t *host = handle->host;
  spi_dev_t *hw = host->hal.hw;


  /* Do a quick poll transaction with a single bit to get the SPI
     peripheral configured. Must be less than 8 bits otherwise
     continuous doesn't work. */
  memset(&rxtrans, 0, sizeof(spi_transaction_t));
  rxtrans.rxlength = 1;
  rxtrans.rx_buffer = (uint8_t *) desc->buf;

  ESP_ERROR_CHECK(spi_device_polling_start(handle, &rxtrans, portMAX_DELAY));
  spi_device_polling_end(handle, portMAX_DELAY);

  /* Setup SPI peripheral for continuous using passed linked list */
  hw->dma_in_link.addr = (uint32_t) desc;
  hw->dma_inlink_dscr = hw->dma_in_link.addr;
  hw->dma_conf.dma_continue = 1;
  hw->dma_in_link.start = 1;

  // Start SPI receive
  hw->cmd.usr = 1;

  return;
}

void cspi_end_continuous(spi_device_handle_t handle)
{
  spi_transaction_t rxtrans;
  spi_host_t *host = handle->host;
  spi_dev_t *hw = host->hal.hw;


  hw->dma_conf.dma_rx_stop = 1;
  hw->dma_conf.dma_tx_stop = 1;

  /* Wait for transaction to come to a full and complete stop */
  while (hw->ext2.st)
    ;

  hw->dma_conf.dma_continue = 0;

  /* Do a poll transaction with 8 bits to get the SPI peripheral back
     into a state that the ESPIDF driver expects */
  memset(&rxtrans, 0, sizeof(spi_transaction_t));
  rxtrans.rxlength = 8;
  rxtrans.rx_buffer = (uint8_t *) heap_caps_malloc(8, MALLOC_CAP_DMA);
  ESP_ERROR_CHECK(spi_device_polling_start(handle, &rxtrans, portMAX_DELAY));
  spi_device_polling_end(handle, portMAX_DELAY);
  heap_caps_free(rxtrans.rx_buffer);

  return;
}

// Get current SPI position
size_t cspi_current_pos(spi_device_handle_t handle)
{
  uint8_t *buf, *cur;
  lldesc_t *current_desc;
  spi_host_t *host = handle->host;
  spi_dev_t *hw = host->hal.hw;


  // Access the current descriptor being used by DMA
  current_desc = (typeof(current_desc)) hw->dma_inlink_dscr;
  cur = (typeof(cur)) current_desc->buf;
  buf = (typeof(buf)) host->cur_trans_buf.buffer_to_rcv;

  return cur - buf;
}

#endif /* CONFIG_IDF_TARGET_ESP32S3 */
