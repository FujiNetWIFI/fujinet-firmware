// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
//#include "nvs_flash.h"
//#include "nvs.h"
//#include "esp_partition.h"

#include "esp_log.h"
#include "esp_timer.h"

//#ifdef CONFIG_APP_ROLLBACK_ENABLE
//#include "esp_ota_ops.h"
//#endif //CONFIG_APP_ROLLBACK_ENABLE


#include <sys/time.h>
#include "soc/rtc.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/apb_ctrl_reg.h"
#include "esp32/rom/rtc.h"
#include "esp_task_wdt.h"
#include "esp32-hal.h"

//Undocumented!!! Get chip temperature in Farenheit
//Source: https://github.com/pcbreflux/espressif/blob/master/esp32/arduino/sketchbook/ESP32_int_temp_sensor/ESP32_int_temp_sensor.ino
uint8_t temprature_sens_read();

float temperatureRead()
{
    return (temprature_sens_read() - 32) / 1.8;
}

void __yield()
{
    vPortYield();
}

void yield() __attribute__ ((weak, alias("__yield")));

#if 0

#if CONFIG_AUTOSTART_ARDUINO

extern TaskHandle_t loopTaskHandle;
extern bool loopTaskWDTEnabled;

void enableLoopWDT(){
    if(loopTaskHandle != NULL){
        if(esp_task_wdt_add(loopTaskHandle) != ESP_OK){
            log_e("Failed to add loop task to WDT");
        } else {
            loopTaskWDTEnabled = true;
        }
    }
}

void disableLoopWDT(){
    if(loopTaskHandle != NULL && loopTaskWDTEnabled){
        loopTaskWDTEnabled = false;
        if(esp_task_wdt_delete(loopTaskHandle) != ESP_OK){
            log_e("Failed to remove loop task from WDT");
        }
    }
}

void feedLoopWDT(){
    esp_err_t err = esp_task_wdt_reset();
    if(err != ESP_OK){
        log_e("Failed to feed WDT! Error: %d", err);
    }
}
#endif

void enableCore0WDT(){
    TaskHandle_t idle_0 = xTaskGetIdleTaskHandleForCPU(0);
    if(idle_0 == NULL || esp_task_wdt_add(idle_0) != ESP_OK){
        log_e("Failed to add Core 0 IDLE task to WDT");
    }
}

void disableCore0WDT(){
    TaskHandle_t idle_0 = xTaskGetIdleTaskHandleForCPU(0);
    if(idle_0 == NULL || esp_task_wdt_delete(idle_0) != ESP_OK){
        log_e("Failed to remove Core 0 IDLE task from WDT");
    }
}

#ifndef CONFIG_FREERTOS_UNICORE
void enableCore1WDT(){
    TaskHandle_t idle_1 = xTaskGetIdleTaskHandleForCPU(1);
    if(idle_1 == NULL || esp_task_wdt_add(idle_1) != ESP_OK){
        log_e("Failed to add Core 1 IDLE task to WDT");
    }
}

void disableCore1WDT(){
    TaskHandle_t idle_1 = xTaskGetIdleTaskHandleForCPU(1);
    if(idle_1 == NULL || esp_task_wdt_delete(idle_1) != ESP_OK){
        log_e("Failed to remove Core 1 IDLE task from WDT");
    }
}
#endif

BaseType_t xTaskCreateUniversal( TaskFunction_t pxTaskCode,
                        const char * const pcName,
                        const uint32_t usStackDepth,
                        void * const pvParameters,
                        UBaseType_t uxPriority,
                        TaskHandle_t * const pxCreatedTask,
                        const BaseType_t xCoreID ){
#ifndef CONFIG_FREERTOS_UNICORE
    if(xCoreID >= 0 && xCoreID < 2) {
        return xTaskCreatePinnedToCore(pxTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pxCreatedTask, xCoreID);
    } else {
#endif
    return xTaskCreate(pxTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pxCreatedTask);
#ifndef CONFIG_FREERTOS_UNICORE
    }
#endif
}

#endif // test, don't think these are needed

unsigned long IRAM_ATTR micros()
{
    return (unsigned long) (esp_timer_get_time());
}

unsigned long IRAM_ATTR millis()
{
    return (unsigned long) (esp_timer_get_time() / 1000ULL);
}

void delay(uint32_t ms)
{
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

void IRAM_ATTR delayMicroseconds(uint32_t us)
{
    uint64_t now = esp_timer_get_time();
    if(us){
        do {
            NOP();
        } while ((esp_timer_get_time() - now) < us);
    }
}


