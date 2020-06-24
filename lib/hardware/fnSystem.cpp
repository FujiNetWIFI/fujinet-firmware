//#include <Arduino.h> // Lets us get the Arduino framework version

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <driver/dac.h>
#include <driver/adc.h>
#include "soc/sens_reg.h"
#include "esp_adc_cal.h"


#include "../../include/debug.h"
#include "../../include/version.h"

#include "fnSystem.h"
#include "fnFsSD.h"
#include "fnFsSPIF.h"

#define NOP() asm volatile ("nop")

// Global object to manage System
SystemManager fnSystem;

// Temprary (?) replacement for Arduino's pinMode()
// Handles only common cases
// PINMODE_INPUT or PINMODE_OUTPUT
// can be ORed with PINMODE_PULLUP or PINMODE_PULLDOWN
void SystemManager::set_pin_mode(uint8_t pin, uint8_t mode)
{
    gpio_config_t io_conf;

    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;


    // set pin mode
    if(mode & PINMODE_INPUT) 
    {
        io_conf.mode = GPIO_MODE_INPUT;

    } else if (mode & PINMODE_OUTPUT)
    {
        io_conf.mode = GPIO_MODE_OUTPUT;
    }
    else
    {
        // Make sure we have either PINMODE_INPUT or PINMODE_OUTPUT
        // Don't continue if we get something unexpected        
#ifdef DEBUG
        Debug_println("set_pin_mode mode isn't INPUT or OUTPUT");
#endif
        abort();
    }

    //set pull-up/down mode (only one or the other)
    io_conf.pull_down_en = gpio_pulldown_t::GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = gpio_pullup_t::GPIO_PULLUP_DISABLE;
    if(mode & PINMODE_PULLDOWN)
    {
        io_conf.pull_down_en = gpio_pulldown_t::GPIO_PULLDOWN_ENABLE;
    } else if (mode & PINMODE_PULLUP)
    {
        io_conf.pull_up_en = gpio_pullup_t::GPIO_PULLUP_ENABLE;
    }

    //bit mask of the pins that you want to set
    io_conf.pin_bit_mask = (1ULL << pin);

    //configure GPIO with the given settings
    gpio_config(&io_conf);    

}

// from esp32-hal-misc.
// Set DIGI_LOW or DIGI_HIGH
void IRAM_ATTR SystemManager::digital_write(uint8_t pin, uint8_t val)
{
    if(val) {
        if(pin < 32) {
            GPIO.out_w1ts = ((uint32_t)1 << pin);
        } else if(pin < 34) {
            GPIO.out1_w1ts.val = ((uint32_t)1 << (pin - 32));
        }
    } else {
        if(pin < 32) {
            GPIO.out_w1tc = ((uint32_t)1 << pin);
        } else if(pin < 34) {
            GPIO.out1_w1tc.val = ((uint32_t)1 << (pin - 32));
        }
    }
}

// from esp32-hal-misc.
// Returns DIGI_LOW or DIGI_HIGH
int IRAM_ATTR SystemManager::digital_read(uint8_t pin)
{
    if(pin < 32) {
        return (GPIO.in >> pin) & 0x1;
    } else if(pin < 40) {
        return (GPIO.in1.val >> (pin - 32)) & 0x1;
    }
    return 0;
}

// from esp32-hal-misc.c
unsigned long IRAM_ATTR SystemManager::micros()
{
    return (unsigned long) (esp_timer_get_time());
}

// from esp32-hal-misc.c
unsigned long IRAM_ATTR SystemManager::millis()
{
     return (unsigned long) (esp_timer_get_time() / 1000ULL);
}

// from esp32-hal-misc.
void SystemManager::delay(uint32_t ms)
{
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

// from esp32-hal-misc.
void IRAM_ATTR SystemManager::delay_microseconds(uint32_t us)
{
    uint32_t m = micros();
    if(us){
        uint32_t e = (m + us);
        if(m > e){ //overflow
            while(micros() > e){
                NOP();
            }
        }
        while(micros() < e){
            NOP();
        }
    }
}

// from esp32-hal-misc.
void SystemManager::yield()
{
    vPortYield();
}

// TODO: Close open files first
void SystemManager::reboot()
{
    esp_restart();
}

/* Size of available heap. Size of largest contiguous block may be smaller.
*/
uint32_t SystemManager::get_free_heap_size()
{
    return esp_get_free_heap_size();
}

/* Microseconds since system boot-up
*/
int64_t SystemManager::get_uptime()
{
    return esp_timer_get_time();
}

const char * SystemManager::get_uptime_str()
{
    int64_t ms = esp_timer_get_time();

    long ml = ms / 1000;
    long s = ml / 1000;
    int m = s / 60;
    int h = m / 60;

    if(h > 0)
        snprintf(_uptime_string, sizeof(_uptime_string), "%02d:%02d:%02ld.%03ld", h, m%60, s%60, ml%1000);
    else
        snprintf(_uptime_string, sizeof(_uptime_string), "%02d:%02ld.%03ld", m, s%60, ml%1000);

    return _uptime_string;
}

const char * SystemManager::get_sdk_version()
{
#ifdef ARDUINO
    static char _version[60];
	int major = ARDUINO / 10000;
	int minor = (ARDUINO % 10000) / 100;
	int patch = ARDUINO % 100;
    snprintf(_version, sizeof(_version), "%s; Arduino %d.%.d.%.d", esp_get_idf_version(), major, minor, patch );
    return _version;
#else    
    return esp_get_idf_version();
#endif    
}

const char * SystemManager::get_fujinet_version()
{
    return FN_VERSION_FULL " " FN_VERSION_DATE;
}
int SystemManager::get_cpu_rev()
{
    esp_chip_info_t chipinfo;
    esp_chip_info(&chipinfo);
    return chipinfo.revision;
}

SystemManager::chipmodels SystemManager::get_cpu_model()
{
    esp_chip_info_t chipinfo;
    esp_chip_info(&chipinfo);

    switch(chipinfo.model)
    {
    case esp_chip_model_t::CHIP_ESP32:
        return chipmodels::CHIP_ESP32;
        break;
    default:
        return chipmodels::CHIP_UNKNOWN;
        break;
    }
}

int SystemManager::get_sio_voltage()
{
    // Configure ADC1_CH7
    adc1_config_width(ADC_WIDTH_12Bit);
    adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_11db);

    // Calculate ADC characteristics
    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);

    int samples = 10;
    uint32_t avgV = 0;
    uint32_t vcc = 0;

    for (int i = 0; i < samples; i++)
    {
        esp_adc_cal_get_voltage(ADC_CHANNEL_7, &adc_chars, &vcc);
        avgV += vcc;
        //delayMicroseconds(5);
    }

    avgV /= samples;

    if (avgV < 501)
        return 0;
    else
        return (avgV * 5900/3900); // SIOvoltage = Vadc*(R1+R2)/R2 (R1=2000, R2=3900)
}

/*
 Create temporary file using provided FileSystem.
 Filename will be 8 characters long. If provided, generated filename will be placed in result_filename
 File opened in "w+" mode.
*/
FILE * SystemManager::make_tempfile(FileSystem *fs, char *result_filename)
{
    if(fs == nullptr || !fs->running())
        return nullptr;

    // Generate a 'random' filename by using timer ticks
    uint32_t ms = micros();

    char buff[9];
    char *fname;
    if(result_filename != nullptr)
        fname = result_filename;
    else
        fname = buff;

    sprintf(fname, "%08u", ms);
    return fs->file_open(fname, "w+");
}

void SystemManager::delete_tempfile(FileSystem *fs, const char* filename)
{
    if (fs==nullptr || !fs->running())
        return;
    
    fs->remove(filename);
}

/*
 Remove specified temporary file, if fnSDFAT available, then file is deleted there,
 otherwise deleted from SPIFFS
*/
void SystemManager::delete_tempfile(const char* filename)
{
    if (fnSDFAT.running())
        delete_tempfile(&fnSDFAT, filename);
    else
        delete_tempfile(&fnSPIFFS, filename);
}

/*
 Create temporary file. fnSDFAT will be used if available, otherwise fnSPIFFS.
 Filename will be 8 characters long. If provided, generated filename will be placed in result_filename
 File opened in "w+" mode.
*/
FILE * SystemManager::make_tempfile(char *result_filename)
{
    if(fnSDFAT.running())
        return make_tempfile(&fnSDFAT, result_filename);
    else
        return make_tempfile(&fnSPIFFS, result_filename);
}


// Copy file from source filesystem/filename to destination filesystem/name using optional buffer_hint for buffer size
size_t SystemManager::copy_file(FileSystem *source_fs, const char *source_filename, FileSystem *dest_fs, const char *dest_filename, size_t buffer_hint)
{
    #ifdef DEBUG
    Debug_printf("copy_file \"%s\" -> \"%s\"\n", source_filename, dest_filename);
    #endif

    FILE * fin = source_fs->file_open(source_filename);
    if(fin == nullptr)
    {
        #ifdef DEBUG
        Debug_println("copy_file failed to open source");
        #endif
        return 0;
    }
    uint8_t *buffer = (uint8_t *) malloc(buffer_hint);
    if(buffer == NULL)
    {
        #ifdef DEBUG
        Debug_println("copy_file failed to allocate copy buffer");
        #endif
        fclose(fin);
        return 0;
    }

    size_t result = 0;
    FILE * fout = dest_fs->file_open(dest_filename, "w");
    if(fout == nullptr)
    {
        #ifdef DEBUG
        Debug_println("copy_file failed to open destination");
        #endif
    }
    else
    {
        size_t count = 0;
        do
        {
            count = fread(buffer, 1, buffer_hint, fin);
            result += fwrite(buffer, 1, count, fout);
        } while (count > 0);

        fclose(fout);
    }

    fclose(fin);
    free(buffer);

    #ifdef DEBUG
    Debug_printf("copy_file copied %d bytes\n", result);
    #endif

    return result;
}


// From esp32-hal-dac.c
/*
void IRAM_ATTR SystemManager::dac_write(uint8_t pin, uint8_t value)
{
    if(pin != DAC_CHANNEL_1_GPIO_NUM && pin != DAC_CHANNEL_2_GPIO_NUM)
        return; // Not a DAC pin
    
    dac_channel_t dac_chan = pin == DAC_CHANNEL_1_GPIO_NUM ? DAC_CHANNEL_1 : DAC_CHANNEL_2;

    ESP_ERROR_CHECK(dac_output_enable(dac_chan));

    // Disable tone
    CLEAR_PERI_REG_MASK(SENS_SAR_DAC_CTRL1_REG, SENS_SW_TONE_EN);

    if (dac_chan == DAC_CHANNEL_2)
    {
        // Disable channel tone
        CLEAR_PERI_REG_MASK(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_CW_EN2_M);
        // Set the DAC value
        SET_PERI_REG_BITS(RTC_IO_PAD_DAC2_REG, RTC_IO_PDAC2_DAC, value, RTC_IO_PDAC2_DAC_S);   //dac_output
        // Channel output enable
        SET_PERI_REG_MASK(RTC_IO_PAD_DAC2_REG, RTC_IO_PDAC2_XPD_DAC | RTC_IO_PDAC2_DAC_XPD_FORCE);
    }
    else
    {
        // Disable Channel tone
        CLEAR_PERI_REG_MASK(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_CW_EN1_M);
        // Set the DAC value
        SET_PERI_REG_BITS(RTC_IO_PAD_DAC1_REG, RTC_IO_PDAC1_DAC, value, RTC_IO_PDAC1_DAC_S);   //dac_output
        // Channel output enable
        SET_PERI_REG_MASK(RTC_IO_PAD_DAC1_REG, RTC_IO_PDAC1_XPD_DAC | RTC_IO_PDAC1_DAC_XPD_FORCE);
    }
}
*/
/*
esp_err_t SystemManager::dac_output_disable(dac_channel_t channel)
{
    return ::dac_output_disable((::dac_channel_t)channel);
}
esp_err_t SystemManager::dac_output_enable(dac_channel_t channel)
{
    return ::dac_output_enable((::dac_channel_t)channel);
}
esp_err_t SystemManager::dac_output_voltage(dac_channel_t channel, uint8_t dac_value)
{
    return ::dac_output_voltage((::dac_channel_t)channel, dac_value);
}
*/

uint32_t SystemManager::get_psram_size()
{
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
    return info.total_free_bytes + info.total_allocated_bytes;
}
