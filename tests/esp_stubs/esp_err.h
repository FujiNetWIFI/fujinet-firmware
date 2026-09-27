// Minimal esp_err.h for host-side unit tests. Only the values the vendored
// fn_esp_http_client header code actually uses.
#ifndef _TEST_STUB_ESP_ERR_H_
#define _TEST_STUB_ESP_ERR_H_

typedef int esp_err_t;

#define ESP_OK                  0
#define ESP_FAIL               -1
#define ESP_ERR_NO_MEM          0x101
#define ESP_ERR_INVALID_ARG     0x102
#define ESP_ERR_INVALID_SIZE    0x104
#define ESP_ERR_NOT_FOUND       0x105

#endif
