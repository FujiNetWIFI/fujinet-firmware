#include <WiFiEspAT.h>
#include "global.h"
#include "menu.h"

#ifndef ESP8266_H
#define ESP8266_H

#define CURRENT_ESP8266_FIRMWARE "1.7.4.0"

/** API connect/request **/
#define  API_ATCMD_1	   "AT+CIPSTART=\"TCP\",\"" PLUSSTORE_API_HOST "\",80\r\n"
#define  API_ATCMD_1a   "AT+CIPSTART=%d,\"TCP\",\"" PLUSSTORE_API_HOST "\",80\r\n"
#define  API_ATCMD_2	   "AT+CIPSEND\r\n"
#define  API_ATCMD_3	   "GET /api.php?p="
#define  API_ATCMD_4	   " HTTP/1.0\r\nHost: " PLUSSTORE_API_HOST \
						      "\r\nPlusStore-ID: v" VERSION " "
#define  API_ATCMD_4a	"\r\nClient-Conf: "
#define  API_ATCMD_5	   "\r\nConnection: keep-alive\r\n"
#define  API_ATCMD_6a	"Range: bytes="
#define  API_ATCMD_6b	"\r\n"

#define PLUSSTORE_CONNECT_TIMEOUT         10000
#define PLUSSTORE_RESPONSE_START_TIMEOUT  25000
#define PLUSROM_API_CONNECT_TIMEOUT       PLUSSTORE_CONNECT_TIMEOUT

#define MAX_RANGE_SIZE           32768

#define OK        1
#define ERROR     2
#define FAIL      3
#define TIMEOUT   255

extern char esp8266_at_version[];
extern char http_request_header[];

// wifi AP returned by scanNetworks
#define MAX_AP_NUM      16
extern WiFiApData waplist[];

typedef struct {
   uint32_t start;
   uint32_t stop;
} http_range;

void __in_flash("esp8266_init") esp8266_init(void);
//void esp8266_update(void) __attribute__((section(".flash01")));
bool __in_flash("esp8266_is_started") esp8266_is_started(void);
bool __in_flash("esp8266_reset") esp8266_reset(bool);
bool __in_flash("esp8266_wifi_list") esp8266_wifi_list(MENU_ENTRY **, int *);
bool __in_flash("esp8266_wifi_connect") esp8266_wifi_connect(char *, char *);
bool __in_flash("esp8266_wps_connect") esp8266_wps_connect(void);
bool __in_flash("esp8266_is_connected") esp8266_is_connected(int timeout = 0);
void __in_flash("esp8266_disconnect") esp8266_disconnect(void);
void __in_flash("read_esp8266_at_version") read_esp8266_at_version(void);
int __in_flash("esp8266_file_list") esp8266_file_list(char *, MENU_ENTRY **, int *, uint8_t *, char *);

bool __in_flash("esp8266_PlusStore_API_connect") esp8266_PlusStore_API_connect(void);
void __in_flash("esp8266_PlusStore_API_end_transmission") esp8266_PlusStore_API_end_transmission(void);
void __in_flash("esp8266_PlusStore_API_prepare_request_header") esp8266_PlusStore_API_prepare_request_header(char *, bool);
uint16_t __in_flash("esp8266_skip_http_response_header") esp8266_skip_http_response_header(WiFiClient *);
uint32_t __in_flash("esp8266_PlusStore_API_range_request") esp8266_PlusStore_API_range_request(char *, http_range, uint8_t *);
uint32_t __in_flash("esp8266_PlusStore_API_file_request") esp8266_PlusStore_API_file_request(uint8_t *, char *, uint32_t, uint32_t);

int __in_flash("esp8266_PlusROM_API_connect") esp8266_PlusROM_API_connect(unsigned int);

void __in_flash("empty_rx") empty_rx(void);
void __in_flash("sendData") sendData(const char *);
void __in_flash("sendCommand") sendCommand(const char *);
uint8_t __in_flash("sendCommandGetResponse") sendCommandGetResponse(const char *, const int timeout = 2000);

#endif	/* ESP8266_H */
