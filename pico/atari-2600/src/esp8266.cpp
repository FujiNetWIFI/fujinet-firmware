/**
 * File:    esp8266.c
 * Author:  Wolfgang Stubig <w.stubig@firmaplus.de>
 * Version: v0.0.4
 *
 * structure based on ESP8266_PIC (v0.1) by Camil Staps <info@camilstaps.nl>
 * Website: http://github.com/camilstaps/ESP8266_PIC
 *
 * ESP8266 AT WiFi Manager templates based on:
 * https://github.com/tzapu/WiFiManager
 *
 * ESP8266 AT Webserver code inspired by:
 * https://os.mbed.com/users/programmer5/code/STM32-ESP8266-WEBSERVER//file/89cb04c5c613/main.cpp/
 *
 * RP2040 porting by Gennaro Tortone <gtortone@gmail.com>
 *
 * C library for interfacing the ESP8266 WiFi transceiver module (esp-01)
 * with a STM32F4 micro controller. Should be used with the HAL Library.
 */

#if USE_WIFI

#include "board.h"
#include "global.h"
#include "user_settings.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "esp8266.h"
#include "md5.h"
#include <hardware/uart.h>

WiFiClient client;

char tmp_uart_buffer[50];
char esp8266_at_version[15];
WiFiApData waplist[MAX_AP_NUM];

extern queue_t pqueue;

int esp8266_file_list(char *path, MENU_ENTRY **dst, int *num_menu_entries, uint8_t *plus_store_status, char *status_msg) {

   int char_counter = 0, trim_path = 0;
   bool is_entry_row, is_status_row;
   uint8_t pos = 0, c;
   int count;

   if(esp8266_PlusStore_API_connect()) {
      esp8266_PlusStore_API_prepare_request_header(path, false);

      client.write(http_request_header, strlen(http_request_header));
      client.flush();
      uint16_t bytes_read = 0, content_length = esp8266_skip_http_response_header(&client);
      is_status_row = true;

      while(bytes_read < content_length) {

         if(!client.available()) {
            count = 0;

            while(!client.available() && count++<5)
               sleep_ms(100);

            if(!client.available())
               break;
         }

         c = client.read();

         if(is_status_row) {
            if(c == '\n') {
               is_status_row = false;
               status_msg[pos] = '\0';
               pos = 0;
            } else if(bytes_read < 1) {
               plus_store_status[bytes_read] = (uint8_t)c;
            } else if(bytes_read < 2) {
               trim_path = c - '0';
            } else {
               status_msg[pos++] = c;
            }

         } else if((*num_menu_entries) < NUM_MENU_ITEMS) {
            if(char_counter == 0) { // first char defines if its an entry row
               is_entry_row = (c >= '0' && c <= '9');  // First char is entry.type '0' to '9'

               if(is_entry_row) {
                  (*dst)->type = (MENU_ENTRY_Type)(c - 48);
               }
            } else if(is_entry_row) {
               if(char_counter == 1) {
                  (*dst)->filesize = 0U;
                  pos = 0;
               } else if(char_counter < 8) {  // get the filesize
                  (*dst)->filesize = (*dst)->filesize * 10 + (uint8_t)(c - '0');
               } else if(char_counter > 8 && char_counter < 41 && c != '\n') { // filename/dirname should begin at index 9
                  (*dst)->entryname[pos] = c;
                  pos++;
               }
            }

            if(c == '\n') {
               if(is_entry_row) {
                  (*dst)->entryname[pos] = '\0';
                  (*dst)->font = user_settings.font_style;
                  (*dst)++;
                  (*num_menu_entries)++;
               }

               char_counter = 0;
            } else {
               char_counter++;
            }
         }

         bytes_read++;
      }

      esp8266_PlusStore_API_end_transmission();
   }

   return trim_path;
}

bool esp8266_PlusStore_API_connect() {
   return client.connect(PLUSSTORE_API_HOST, 80);
}

void esp8266_PlusStore_API_prepare_request_header(char *path, bool prepare_range_request) {

   // ' ' --> '+' last check no space in http GET request!
   for(char* p = path; (p = strchr(p, ' ')); *p++ = '+');

   http_request_header[0] = '\0';

   strcat(http_request_header, API_ATCMD_3);
   strcat(http_request_header, path);
   strcat(http_request_header, API_ATCMD_4);
   strcat(http_request_header, pico_uid);
   strcat(http_request_header, API_ATCMD_4a);

   size_t header_len = strlen(http_request_header);
   // default start sector
   itoa(5, (char *)&http_request_header[header_len++], 16); // 5 - C
   http_request_header[header_len++] = ',';
   itoa(user_settings.font_style, &http_request_header[header_len++], 10);                      // 0 - 3
   http_request_header[header_len++] = ',';
   itoa(user_settings.line_spacing, &http_request_header[header_len++], 10);                    // 0 - 2
   http_request_header[header_len++] = ',';
   itoa(user_settings.tv_mode, (char *)&http_request_header[header_len++], 10);                 // 1 - 3
   http_request_header[header_len] = '\0';

   // FLASH_SIZE > 512U
   strcat(http_request_header, ",1, ");

   uint8_t conf;
   conf = HARDWARE_TYPE - 1 + ((MENU_TYPE - 1) << 1);

#if USE_SD_CARD
   conf += USE_SD_CARD << 2;
#endif

#if USE_WIFI
   conf += USE_WIFI << 3;
#endif
   itoa(conf, (char *)&http_request_header[(header_len+3)], 16);									// 0 - F

   strcat(http_request_header, API_ATCMD_5);

   if(prepare_range_request)
      strcat(http_request_header, API_ATCMD_6a);
   else
      strcat(http_request_header, API_ATCMD_6b);
}

void esp8266_PlusStore_API_end_transmission() {
   client.stop();
}

uint32_t esp8266_PlusStore_API_range_request(char *path, http_range range, uint8_t *ext_buffer) {
   uint32_t response_size = 0;
   uint16_t expected_size = (uint16_t)(range.stop + 1 - range.start);
   uint8_t c;
   int count;

   esp8266_PlusStore_API_prepare_request_header(path, true);

   char range_str[64];
   sprintf(range_str, "%lu-%lu", range.start, range.stop);

   strcat(http_request_header, range_str);
   strcat(http_request_header, "\r\n\r\n");

   client.write(http_request_header, strlen(http_request_header));
   client.flush();

   esp8266_skip_http_response_header(&client);

   while(response_size < expected_size) {
      if(!client.available()) {
         count = 0;

         while(!client.available() && count++<5)
            sleep_ms(100);

         if(!client.available())
            break;
      }

      c = client.read();

      ext_buffer[response_size] = c;
      response_size++;
   }

   return response_size;
}

uint32_t esp8266_PlusStore_API_file_request(uint8_t *ext_buffer, char *path, uint32_t start_pos, uint32_t length) {

   uint32_t bytes_read = 0, chunk_read = 0;
   uint32_t max_range_pos = start_pos + length - 1;
   uint32_t request_count = (length + (MAX_RANGE_SIZE - 1))  / MAX_RANGE_SIZE;
   http_range range;

   esp8266_PlusStore_API_connect();

   for(uint32_t i = 0; i < request_count; i++) {
      range.start = start_pos + (i * MAX_RANGE_SIZE);
      range.stop = range.start + (MAX_RANGE_SIZE -1);

      if(range.stop > max_range_pos) {
         range.stop = max_range_pos;
      }

      chunk_read = esp8266_PlusStore_API_range_request(path, range, &ext_buffer[(range.start - start_pos)]);
      bytes_read += chunk_read;

      if(chunk_read != (range.stop + 1 - range.start))
         break;
   }

   esp8266_PlusStore_API_end_transmission();
   return bytes_read;
}

int esp8266_PlusROM_API_connect(unsigned int size) {

   uint16_t * nmi_p = (uint16_t *)&buffer[size - 6];
   int i = nmi_p[0] - 0x1000;
   unsigned char device_id_hash[16];
   int offset = (int)strlen((char *)&buffer[i]) + 1 + i;

   md5((unsigned char *)pico_uid, strlen(pico_uid), device_id_hash);

   // stop previous TCP connections
   client.stop();

   empty_rx();

   sendCommandGetResponse("ATE0\r\n", 2000);
   sendCommandGetResponse("AT+CWMODE=1\r\n", 2000);
   sendCommandGetResponse("AT+CIPMUX=0\r\n", 2000);
   sendCommandGetResponse("AT+CIPSERVER=0\r\n", 2000);
   sendCommandGetResponse("AT+CIPMODE=1\r\n", 2000);
   sendCommandGetResponse("AT+SLEEP=0\r\n", 2000);

   http_request_header[0] = '\0';
   strcat(http_request_header, (char *)"AT+CIPSTART=\"TCP\",\"");
   strcat(http_request_header, (char *)&buffer[offset]);
   strcat(http_request_header, (char *)"\",80,1\r\n");

   sendCommandGetResponse(http_request_header, PLUSROM_API_CONNECT_TIMEOUT);

   http_request_header[0] = '\0';
   strcat(http_request_header, (char *)"POST /");
   strcat(http_request_header, (char *)&buffer[i]);
   strcat(http_request_header, (char *)" HTTP/1.0\r\nHost: ");
   strcat(http_request_header, (char *)&buffer[offset]);
   strcat(http_request_header, (char *)"\r\nConnection: keep-alive\r\n"
          "Content-Type: application/octet-stream\r\n"
          "PlusROM-Info: agent=PlusCart;ver=" VERSION ";id=");

   char *ptr = &http_request_header[strlen(http_request_header)];

   for(i = 0; i < 16; i++)
      ptr += sprintf(ptr, "%02X", device_id_hash[i]);

   strcat(http_request_header, (char *)";nick=\r\nContent-Length:    \r\n\r\n");
   offset = (int)strlen(http_request_header);

   sendCommand(API_ATCMD_2);
   sleep_ms(250);
   empty_rx();

   return offset;
}

uint16_t esp8266_skip_http_response_header(WiFiClient *wcl) {

   int count = 0;
   uint16_t content_length = 0;
   uint8_t c;

   while(!wcl->available())
      ;

   while(wcl->available()) {
      c = wcl->read();

      if(c == '\n') {
         if(count == 1) {
            break;
         } else if(count > 16 && strncasecmp("content-length: ", tmp_uart_buffer, 16) == 0) {
            content_length = (uint16_t) atoi(&tmp_uart_buffer[16]);
         }

         count = 0;
      } else {
         if(count < 21) {
            tmp_uart_buffer[count] = c;
         }

         count++;
      }
   }

   return content_length;
}

/**
  * @brief ESP8266 Initialization Function
  * @param None
  * @retval None
  */
void esp8266_init() {

   int count = 0;

   WiFi.init(espSerial, ESP_RESET_PIN);
   // set WiFi STA mdoe
   WiFi.endAP(true);

   do {
      sleep_ms(200);
   } while(esp8266_is_connected() == false && count++ < 50);
}

#if 0
void esp8266_update() {
   uint8_t c;
   //wait 2 seconds
   HAL_Delay(2000);

   while(HAL_UART_Receive(&huart1, &c, 1, 10) == HAL_OK); // first read old messages..

   if(esp8266_send_command("AT+CIUPDATE\r\n", 120000) != ESP8266_OK)  // wait 2 minutes max for firmware download and flashing
      return;

   // Update success wait for ESP8266 reboot (we don't monitor ESP8266_WIFI_DISCONNECT).
   if(wait_response(15000) != ESP8266_READY)
      return;

   wait_response(7000); // wait for reconnect to WiFi

   read_esp8266_at_version(); // read (hopefully) new AT version

   esp8266_init(); // redo init
}
#endif

/**
 * Check if the module is started
 */
bool esp8266_is_started(void) {
   return WiFi.status() != WL_NO_MODULE;
}

/**
 * Restart or Restore the module
 */
bool esp8266_reset(bool factory_reset) {

   if(factory_reset)
      return (sendCommandGetResponse("AT+RESTORE\r\n") == OK);

   WiFi.reset(ESP_RESET_PIN);
   sleep_ms(500);

   return true;
}

bool esp8266_wifi_list(MENU_ENTRY **dst, int *num_menu_entries) {

   int numSsid = WiFi.scanNetworks(waplist, MAX_AP_NUM);

   for(int thisNet = 0; thisNet < numSsid; thisNet++) {

      (*dst)->entryname[0] = '\0';
      strncat((*dst)->entryname, WiFi.SSID(thisNet), 32);
      (*dst)->type = Input_Field;
      (*dst)->filesize = 0U;

      (*dst)++;
      (*num_menu_entries)++;
   }

   return true;
}

bool esp8266_wifi_connect(char *ssid, char *password) {

   uint t = 0, timeout = 15000;             // 15 sec
   uint interval = timeout / 10;

   WiFi.setPersistent();
   WiFi.begin(ssid, password);

   while(!esp8266_is_connected() && t < timeout) {
      t += interval;
      sleep_ms(interval);
   }

   return esp8266_is_connected();
}

bool esp8266_wps_connect() {

   uint t = 0, timeout = 10000;             // 10 sec
   uint interval = timeout / 10;

   sendCommandGetResponse("AT+CWMODE=1\r\n");
   sendCommandGetResponse("AT+WPS=1\r\n");

   while(!esp8266_is_connected() && t < timeout) {
      t += interval;
      sleep_ms(interval);
   }

   // flush rx buffer
   empty_rx();

   return esp8266_is_connected();
}

bool esp8266_is_connected(int timeout) {

   uint t = 0;
   uint interval = timeout / 10;
   uint8_t val;

   while(WiFi.status() != WL_CONNECTED) {

      if(t >= timeout)
         break;

      if(WiFi.status() == WL_CONNECT_FAILED)       // check for authentication issues...
         break;

      t += interval;
      sleep_ms(interval);
   }

   return WiFi.status() == WL_CONNECTED;
}

void read_esp8266_at_version() {
   WiFi.firmwareVersion(esp8266_at_version);
}

void empty_rx(void) {
   int count = 0;

   while(true) {
      if(count > 5)
         break;

      if(espSerial.available())
         espSerial.read();
      else {
         delay(200);
         count++;
      }
   }
}

void sendData(const char *data) {
   espSerial.write(data);
}

void sendCommand(const char *cmd) {
   espSerial.write(cmd);
}

uint8_t sendCommandGetResponse(const char *cmd, const int timeout) {

   char c, data[128];
   int i = 0;
   const int maxCount = 50;
   int interval = timeout / maxCount;
   int count = 0;

   sendCommand(cmd);

   while(true) {

      if(count == maxCount)
         return TIMEOUT;

      if(espSerial.available()) {

         c = espSerial.read();
         data[i++] = c;

         if(c == '\n') {
            data[i] = '\0';

            if(strstr(data, "OK\r\n"))
               return OK;

            if(strstr(data, "FAIL\r\n"))
               return FAIL;

            if(strstr(data, "ERROR\r\n"))
               return ERROR;

            data[0] = '\0';
            i = 0;
         }
      } else {
         delay(interval);
         count++;
      }
   }
}

#endif
