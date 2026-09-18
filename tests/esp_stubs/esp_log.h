// Minimal esp_log.h for host-side unit tests: the log macros become no-ops so
// the vendored sources compile unchanged.
#ifndef _TEST_STUB_ESP_LOG_H_
#define _TEST_STUB_ESP_LOG_H_

#define ESP_LOGE(tag, fmt, ...) ((void)0)
#define ESP_LOGW(tag, fmt, ...) ((void)0)
#define ESP_LOGI(tag, fmt, ...) ((void)0)
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#define ESP_LOGV(tag, fmt, ...) ((void)0)

#endif
