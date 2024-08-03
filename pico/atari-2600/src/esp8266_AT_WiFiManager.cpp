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

#include "global.h"
#include "menu.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "esp8266_AT_WiFiManager.h"
#include "esp8266.h"
#include <SoftwareSerial.h>

static char tmp_uart_buffer[50];

static WiFiClient client;
static WiFiServer server;

extern char pico_uid[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

void esp8266_AT_WiFiManager(void) {

   sendCommandGetResponse("AT+CIPCLOSE\r\n");	                        // close all connections.
   sendCommandGetResponse("AT+CWMODE=3\r\n");	                        // enable AccessPoint + Station mode.
   sendCommandGetResponse("AT+CWSAP=\"PlusCart(+)\",\"\",1,0\r\n");	   // set SSID.
   sendCommandGetResponse("AT+CIPMODE=0\r\n");	                        // not transparent transmission
   sendCommandGetResponse("AT+CIPMUX=1\r\n");	                        // enable multiple connections
   sendCommandGetResponse("AT+CIPSERVERMAXCONN=1\r\n");                 // set max connections

   server.begin(80, 1, 30);

   handle_http_requests();

   server.end();
   sendCommandGetResponse("AT+CIPMUX=0\r\n");                           // single connection
   sendCommandGetResponse("AT+CWMODE=1\r\n");	                        // disable AccessPoint mode.

   esp8266_reset(false);
   esp8266_init();
}

void handle_http_requests() {
   unsigned char c;
   uint8_t state = 0;
   uint32_t timeout;

   while(state < 7) {
      timeout = 5000;

      client = server.available();

      if(client) {
         while(client.connected()) {
            if(client.available()) {
               client.readBytesUntil('\n', buffer, 1024);

               if(strstr((const char *)buffer, "GET"))
                  state = process_http_headline();
            }
         }
      }
   }
}

uint8_t process_http_headline(void) {

   char linkId = (char)buffer[0];
   uint8_t status = 0, response_page = http_page_not_found;
   char c;
   char response[30] = {0};

   if(strlen((char *)buffer) >= 14) { //smallest valid http header = "GET / HTTP/x.x"
      if(strstr((char *)buffer, GET_ROOT) || strstr((char *)buffer, GET_INDEX_HTML)) {
         response_page = http_page_start;
      } else if(strstr((char *)buffer, GET_FAVICON_ICO)) {
         response_page = http_favicon_ico;
      } else if(strstr((char *)buffer, " " GET_EXIT)) {
         response_page = http_page_exit;
         status = 7;
      } else if(strstr((char *)buffer, " " GET_SAVE"?")) {

         url_param p_array[3] = {{"s="}, {"p="}};
         get_http_request_url_param_values(p_array, 2);

         response_page = http_page_save;
         esp8266_wifi_connect(p_array[0].value, p_array[1].value);

      } else if(strstr((char *)buffer, " " GET_NO_SCAN)) {
         response_page = http_page_wifi_no_scan;
      } else if(strstr((char *)buffer, " " GET_INFO)) {
         response_page = http_page_info;
         generate_html_wifi_info();
      } else if(strstr((char *)buffer, " " GET_WIFI)) {
         response_page = http_page_wifi;
         generate_html_wifi_list();
      } else if(strstr((char *)buffer, " " GET_PLUS_CONNECT)) {
         response_page = http_page_plus_connect;
      } else if(strstr((char *)buffer, " " GET_SAVE_CONNECT)) {
         char cur_path[128] = URLENCODE_MENU_TEXT_SETUP "/" URLENCODE_MENU_TEXT_PLUS_CONNECT "/";
         url_param p_array[3] = {{"s="}};

         get_http_request_url_param_values(p_array, 1);

         strncat(cur_path, p_array[0].value, (127 - sizeof(URLENCODE_MENU_TEXT_SETUP "/" URLENCODE_MENU_TEXT_PLUS_CONNECT "/")));
         strcat(cur_path, "/Enter");

         esp8266_PlusStore_API_prepare_request_header(cur_path, false);

         WiFiClient plusstore;

         if(plusstore.connect(PLUSSTORE_API_HOST, 80)) {

            plusstore.write(http_request_header, strlen(http_request_header));
            plusstore.flush();
            esp8266_skip_http_response_header(&plusstore);

            plusstore.readBytesUntil('\n', response, sizeof(response));

            if(strcmp(response, "04Email Sent") == 0)
               response_page = http_page_plus_connected;
            else if(strcmp(response, "14Connect Failed") == 0)
               response_page = http_page_plus_failed;
            else if(strcmp(response, "04Connect Request Opened") == 0)
               response_page = http_page_plus_created;
            else
               response_page = http_page_plus_error;

         }  // end if(plusstore.connect.,,

         plusstore.stop();
      } // end if(strstr((char *)buffer, " " GET_SAVE_CONNECT)
   } // end if(strlen((char *)buffer) >= 14)

   if(response_page == http_favicon_ico) {
      send_requested_page_to_client(linkId, favicon_ico, sizeof(favicon_ico), true);
   } else if(response_page == http_page_not_found) {
      send_requested_page_to_client(linkId, not_found_text, sizeof(not_found_text)-1, true);
   } else {
      send_requested_page_to_client(linkId, http_header_html, sizeof(http_header_html) - 1, false);
      send_requested_page_to_client(linkId, html_head, sizeof(html_head) - 1, false);

      if(response_page == http_page_wifi || response_page == http_page_info) {
         send_requested_page_to_client(linkId, (char *)buffer, strlen((char *)buffer), false);
      }

      if(response_page == http_page_wifi  || response_page == http_page_wifi_no_scan) {
         send_requested_page_to_client(linkId, html_form, sizeof(html_form) - 1, false);
      }

      if(response_page == http_page_save) {
         send_requested_page_to_client(linkId, html_saved, sizeof(html_saved) - 1, false);
      } else if(response_page == http_page_exit) {
         send_requested_page_to_client(linkId, html_exit, sizeof(html_exit) - 1, false);
      } else if(response_page == http_page_start) {
         send_requested_page_to_client(linkId, html_portal_options, sizeof(html_portal_options) - 1, false);

         if(esp8266_is_connected()) {
            send_requested_page_to_client(linkId, html_plus_connect, sizeof(html_plus_connect) - 1, false);
         }
      } else if(response_page == http_page_plus_connect) {
         send_requested_page_to_client(linkId, html_connect_form, sizeof(html_connect_form) - 1, false);
      } else if(response_page == http_page_plus_failed) {
         send_requested_page_to_client(linkId, html_plus_failed, sizeof(html_plus_failed) - 1, false);
      } else if(response_page == http_page_plus_created) {
         send_requested_page_to_client(linkId, html_plus_created, sizeof(html_plus_created) - 1, false);
      } else if(response_page == http_page_plus_connected) {
         send_requested_page_to_client(linkId, html_plus_connected, sizeof(html_plus_connected) - 1, false);
      }

      if(response_page != http_page_start && response_page != http_page_exit) {
         send_requested_page_to_client(linkId, html_back, sizeof(html_back) - 1, false);
      }

      send_requested_page_to_client(linkId, html_end, sizeof(html_end) - 1, true);
   }

   return status;
}

void send_requested_page_to_client(char id, const char* page, unsigned int len, bool close_connection) {
   uint16_t len_of_package_to_TX;
   unsigned int page_to_send_address = 0;

   while(len > 0) {
      if(len > 2048) {
         len -= 2048;
         len_of_package_to_TX = 2048;
      } else {
         len_of_package_to_TX = (uint16_t) len;
         len = 0;
      }

      client.write(&page[page_to_send_address], len_of_package_to_TX);
      client.flush();
      page_to_send_address += len_of_package_to_TX;
   }

   if(close_connection) {
      client.stop();
   }
}

void get_http_request_url_param_values(url_param * param_array, int len) {
   char *t;

   for(int i=0; i<len; i++) {
      if((param_array[i].value = strstr((char *)buffer, param_array[i].param))) {
         param_array[i].value += 2;
      }
   }

   for(int i=0; i<len; i++) {
      if(param_array[i].value) {
         while((t = strstr(param_array[i].value, "&")))
            t[0] = '\0';

         while((t = strstr(param_array[i].value, " ")))
            t[0] = '\0';

         uri_decode(param_array[i].value);
      }
   }
}

int ishex(char x) {
   return (x >= '0' && x <= '9') || (x >= 'a' && x <= 'f') || (x >= 'A' && x <= 'F');
}

void uri_decode(char *s) {
   int len = (int) strlen(s);
   int c;
   int s_counter = 0, d_counter = 0;

   for(; s_counter < len; s_counter++) {
      c = s[s_counter];

      if(c == '+') {
         c = ' ';
      } else if(c == '%' && (!ishex(s[++s_counter]) || !ishex(s[++s_counter]) || !sscanf(&s[s_counter - 1], "%2x", &c))) {
         return;
      }

      s[d_counter++] = (char) c;
   }

   s[d_counter] = '\0';
}

void generate_html_wifi_list(void) {

   int numSsid = WiFi.scanNetworks(waplist, MAX_AP_NUM);

   buffer[0] = '\0';

   for(int thisNet = 0; thisNet < numSsid; thisNet++) {
      const char *name = WiFi.SSID(thisNet);
      uint8_t enc = WiFi.encryptionType(thisNet);
      int32_t quality = WiFi.RSSI(thisNet);

      strcat((char *)buffer, "<div><a href=\"#p\" onclick=\"c(this)\">");
      strcat((char *)buffer, name);
      strcat((char *)buffer, "</a>&nbsp;<span class=\"q ");

      if(enc != 0)
         strcat((char *)buffer, (const char *)"l");

      strcat((char *)buffer, "\">");

      if(quality <= -100) {
         quality = 0;
      } else if(quality >= -50) {
         quality = 100;
      } else {
         quality = 2 * (quality + 100);
      }

      itoa(quality, (char *)&buffer[strlen((char *)buffer)], 10);
      strcat((char *)buffer, "%</span></div>");
   }
}

void generate_html_wifi_info(void) {

   const char *ssid = WiFi.SSID();
   int32_t quality = WiFi.RSSI();

   if(quality <= -100) {
      quality = 0;
   } else if(quality >= -50) {
      quality = 100;
   } else {
      quality = 2 * (quality + 100);
   }

   buffer[0] = '\0';
   strcat((char *)buffer, "<div>");

   if(ssid[0] != '\0') {
      strcat((char *)buffer, ssid);
      strcat((char *)buffer, "&nbsp;<span class=\"q\">");
      itoa(quality, (char *)&buffer[strlen((char *)buffer)], 10);
      strcat((char *)buffer, "%</span>");
   } else {
      strcat((char *)buffer, "No connection");
   }

   strcat((char *)buffer, "</div>");
}

#endif
