#include "spi_extensions.h"

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



/* Allocates a DMA linked list with each element pointing to a
   successive section of buffer that is only chunk_size long. */
lldesc_t *fspi_alloc_linked_list(uint8_t *buffer, size_t length, size_t chunk_size)
{
  uint32_t num_chunks, idx;
  lldesc_t *llfirst, *llcur;


  num_chunks = (length + chunk_size - 1) / chunk_size;

  llfirst = (lldesc_t *) heap_caps_malloc(sizeof(lldesc_t) * num_chunks, MALLOC_CAP_DMA);
  if (!llfirst) {
    ESP_LOGE("SPI_DMA", "Failed to allocate DMA descriptor");
    return 0;
  }

  memset(llfirst, 0, sizeof(lldesc_t) * num_chunks);
  for (idx = 0; idx < num_chunks; idx++) {
    llcur = &llfirst[idx];
    llcur->size = chunk_size;
    llcur->owner = 1; // Owned by DMA hardware
    llcur->buf = &buffer[idx * chunk_size];
    llcur->qe.stqe_next = &llfirst[(idx + 1) % num_chunks];
  }

  /* Mark last element in linked list as last */
  llcur = &llfirst[num_chunks - 1];
  llcur->size = length - (num_chunks - 1) * chunk_size;
  llcur->eof = 1;
  llcur->qe.stqe_next = NULL;

  return llfirst;
}

/* Gets SPI peripheral setup and ready to start capturing data with
   spi_start_receive */
void spi_open_receive(spi_device_handle_t handle)
{
  spi_transaction_t rxtrans;


  /* Do a short poll to setup the SPI peripheral */
  memset(&rxtrans, 0, sizeof(spi_transaction_t));
  rxtrans.rxlength = 8;
  rxtrans.rx_buffer = (uint8_t *) heap_caps_malloc(8, MALLOC_CAP_DMA);
  ESP_ERROR_CHECK(spi_device_polling_start(handle, &rxtrans, portMAX_DELAY));
  spi_device_polling_end(handle, portMAX_DELAY);
  heap_caps_free(rxtrans.rx_buffer);

  return;
}

/* Waits for receive transaction to end and Puts SPI peripheral back
   into a state that it will work with the ESPIDF drivers */
void spi_close_receive(spi_device_handle_t handle)
{
  spi_host_t *host = handle->host;
  spi_dev_t *hw = host->hal.hw;


  hw->dma_conf.dma_rx_stop = 1;

  /* Wait for transaction to come to a full and complete stop */
  while (hw->ext2.st)
    ;

  return;
}

/* Starts SPI receive using the linked list `desc` */
void spi_trigger_receive(spi_device_handle_t handle, lldesc_t *desc, size_t bit_length)
{
  spi_host_t *host = handle->host;
  spi_dev_t *hw = host->hal.hw;


  hw->dma_in_link.addr = (uint32_t) desc;
  hw->dma_inlink_dscr = hw->dma_in_link.addr;
  hw->miso_dlen.usr_miso_dbitlen = bit_length;
  hw->dma_in_link.start = 1;
  hw->cmd.usr = 1;

  return;
}

#endif /* CONFIG_IDF_TARGET_ESP32S3 */
