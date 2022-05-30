/* SPI Master example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
// // #include <stdio.h>
// //#include <stdlib.h>
// #include <string.h>
// // #include "freertos/FreeRTOS.h"
// // #include "freertos/task.h"
// // #include "esp_system.h"
#include "driver/spi_master.h"
// #include "soc/gpio_struct.h"
// #include "driver/gpio.h"

void lcd_cmd(spi_device_handle_t spi, const uint8_t cmd);
void lcd_data(spi_device_handle_t spi, const uint8_t *data, int len);
void lcd_spi_pre_transfer_callback(spi_transaction_t *t);
// static void send_line(spi_device_handle_t spi, int ypos, uint16_t *line); 
// static void send_line_finish(spi_device_handle_t spi);
// static void display_pretty_colors(spi_device_handle_t spi);
void app_main();
