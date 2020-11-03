#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <time.h>
#include <driver/gpio.h>
#include <driver/dac.h>
#include <driver/adc.h>
#include "soc/sens_reg.h"
#include "soc/rtc.h"
#include "esp_adc_cal.h"

#include <cstring>

#include "../../include/debug.h"
#include "../../include/version.h"

#include "fnSystem.h"
#include "fnFsSD.h"
#include "fnFsSPIF.h"

// Global object to manage System
SystemManager fnSystem;

// Returns current CPU frequency in MHz
uint32_t SystemManager::get_cpu_frequency()
{
    rtc_cpu_freq_config_t cfg;
    rtc_clk_cpu_freq_get_config(&cfg);
    return cfg.freq_mhz;
}

// Set pin mode. No option to enable interrupts, although this is possible
void SystemManager::set_pin_mode(uint8_t pin, gpio_mode_t mode, pull_updown_t pull_mode)
{
    gpio_config_t io_conf;

    // disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;

    // set mode
    io_conf.mode = mode;

    // set pull-up pull-down modes
    if (pull_mode == PULL_BOTH || pull_mode == PULL_UP)
        io_conf.pull_up_en = gpio_pullup_t::GPIO_PULLUP_ENABLE;
    else
        io_conf.pull_up_en = gpio_pullup_t::GPIO_PULLUP_DISABLE;

    if (pull_mode == PULL_BOTH || pull_mode == PULL_DOWN)
        io_conf.pull_down_en = gpio_pulldown_t::GPIO_PULLDOWN_ENABLE;
    else
        io_conf.pull_down_en = gpio_pulldown_t::GPIO_PULLDOWN_DISABLE;

    // bit mask of the pin to set
    io_conf.pin_bit_mask = 1ULL << pin;

    // configure GPIO with the given settings
    gpio_config(&io_conf);
}

// from esp32-hal-misc.
// Set DIGI_LOW or DIGI_HIGH
void IRAM_ATTR SystemManager::digital_write(uint8_t pin, uint8_t val)
{
    if (val)
    {
        if (pin < 32)
        {
            GPIO.out_w1ts = ((uint32_t)1 << pin);
        }
        else if (pin < 34)
        {
            GPIO.out1_w1ts.val = ((uint32_t)1 << (pin - 32));
        }
    }
    else
    {
        if (pin < 32)
        {
            GPIO.out_w1tc = ((uint32_t)1 << pin);
        }
        else if (pin < 34)
        {
            GPIO.out1_w1tc.val = ((uint32_t)1 << (pin - 32));
        }
    }
}

// from esp32-hal-misc.
// Returns DIGI_LOW or DIGI_HIGH
int IRAM_ATTR SystemManager::digital_read(uint8_t pin)
{
    if (pin < 32)
    {
        return (GPIO.in >> pin) & 0x1;
    }
    else if (pin < 40)
    {
        return (GPIO.in1.val >> (pin - 32)) & 0x1;
    }
    return 0;
}

// from esp32-hal-misc.c
unsigned long IRAM_ATTR SystemManager::micros()
{
    return (unsigned long)(esp_timer_get_time());
}

// from esp32-hal-misc.c
unsigned long IRAM_ATTR SystemManager::millis()
{
    return (unsigned long)(esp_timer_get_time() / 1000ULL);
}

// from esp32-hal-misc.
void SystemManager::delay(uint32_t ms)
{
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

// from esp32-hal-misc.
void IRAM_ATTR SystemManager::delay_microseconds(uint32_t us)
{
    uint32_t start = (uint32_t)esp_timer_get_time();

    if (us)
    {
        uint32_t end = start + us;

        // Handle overflow first
        if (start > end)
        { 
            while ((uint32_t)esp_timer_get_time() > end)
                NOP();
        }
        while ((uint32_t)esp_timer_get_time() < end)
            NOP();
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

void SystemManager::update_timezone(const char *timezone)
{
    if (timezone != nullptr && timezone[0] != '\0')
        setenv("TZ", timezone, 1);

    tzset();
}

const char *SystemManager::get_current_time_str()
{
    time_t tt = time(nullptr);
    struct tm *tinfo = localtime(&tt);

    strftime(_currenttime_string, sizeof(_currenttime_string), "%a %b %e, %H:%M:%S %Y %z", tinfo);

    return _currenttime_string;
}

const char *SystemManager::get_uptime_str()
{
    int64_t ms = esp_timer_get_time();

    long ml = ms / 1000;
    long s = ml / 1000;
    int m = s / 60;
    int h = m / 60;

    if (h > 0)
        snprintf(_uptime_string, sizeof(_uptime_string), "%02d:%02d:%02ld.%03ld", h, m % 60, s % 60, ml % 1000);
    else
        snprintf(_uptime_string, sizeof(_uptime_string), "%02d:%02ld.%03ld", m, s % 60, ml % 1000);

    return _uptime_string;
}

const char *SystemManager::get_sdk_version()
{
    return esp_get_idf_version();
}

const char *SystemManager::get_fujinet_version(bool shortVersionOnly)
{
    if (shortVersionOnly)
        return FN_VERSION_FULL;
    else
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

    switch (chipinfo.model)
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
        return (avgV * 5900 / 3900); // SIOvoltage = Vadc*(R1+R2)/R2 (R1=2000, R2=3900)
}

/*
 Create temporary file using provided FileSystem.
 Filename will be 8 characters long. If provided, generated filename will be placed in result_filename
 File opened in "w+" mode.
*/
FILE *SystemManager::make_tempfile(FileSystem *fs, char *result_filename)
{
    if (fs == nullptr || !fs->running())
        return nullptr;

    // Generate a 'random' filename by using timer ticks
    uint32_t ms = micros();

    char buff[9];
    char *fname;
    if (result_filename != nullptr)
        fname = result_filename;
    else
        fname = buff;

    sprintf(fname, "%08u", ms);
    return fs->file_open(fname, "w+");
}

void SystemManager::delete_tempfile(FileSystem *fs, const char *filename)
{
    if (fs == nullptr || !fs->running())
        return;

    fs->remove(filename);
}

/*
 Remove specified temporary file, if fnSDFAT available, then file is deleted there,
 otherwise deleted from SPIFFS
*/
void SystemManager::delete_tempfile(const char *filename)
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
FILE *SystemManager::make_tempfile(char *result_filename)
{
    if (fnSDFAT.running())
        return make_tempfile(&fnSDFAT, result_filename);
    else
        return make_tempfile(&fnSPIFFS, result_filename);
}

// Checks that path exists and creates if it doesn't
bool create_path(char *fullpath)
{
    char * segment = strtok(fullpath, "/");
    while(segment != nullptr)
    {
        segment = strtok(nullptr, "/");
    }
}

// Copy file from source filesystem/filename to destination filesystem/name using optional buffer_hint for buffer size
size_t SystemManager::copy_file(FileSystem *source_fs, const char *source_filename, FileSystem *dest_fs, const char *dest_filename, size_t buffer_hint)
{
    Debug_printf("copy_file \"%s\" -> \"%s\"\n", source_filename, dest_filename);

    FILE *fin = source_fs->file_open(source_filename);
    if (fin == nullptr)
    {
        Debug_println("copy_file failed to open source");
        return 0;
    }
    uint8_t *buffer = (uint8_t *)malloc(buffer_hint);
    if (buffer == NULL)
    {
        Debug_println("copy_file failed to allocate copy buffer");
        fclose(fin);
        return 0;
    }

    size_t result = 0;
    FILE *fout = dest_fs->file_open(dest_filename, "w");
    if (fout == nullptr)
    {
        Debug_println("copy_file failed to open destination");
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

    Debug_printf("copy_file copied %d bytes\n", result);

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

/*
    If buffer is NULL, simply returns size of file. Otherwise
    allocates buffer for reading file contents. Buffer must be freed by caller.
*/
int SystemManager::load_firmware(const char *filename, uint8_t **buffer)
{
    Debug_printf("load_firmware '%s'\n", filename);

    if (fnSPIFFS.exists(filename) == false)
    {
        Debug_println("load_firmware FILE NOT FOUND");
        return -1;
    }

    FILE *f = fnSPIFFS.file_open(filename);
    size_t file_size = FileSystem::filesize(f);

    Debug_printf("load_firmware file size = %u\n", file_size);

    if (buffer == NULL)
    {
        fclose(f);
        return file_size;
    }

    int bytes_read = -1;
    uint8_t *result = (uint8_t *)malloc(file_size);
    if (result == NULL)
    {
        Debug_println("load_firmware failed to malloc");
    }
    else
    {
        bytes_read = fread(result, 1, file_size, f);
        if (bytes_read == file_size)
        {
            *buffer = result;
        }
        else
        {
            free(result);
            bytes_read = -1;

            Debug_printf("load_firmware only read %u bytes out of %u - failing\n", bytes_read, file_size);
        }
    }

    fclose(f);
    return bytes_read;
}

// Dumps list of current tasks
void SystemManager::debug_print_tasks()
{
#ifdef DEBUG

    static const char *status[] = {"Running", "Ready", "Blocked", "Suspened", "Deleted"};

    uint32_t n = uxTaskGetNumberOfTasks();
    TaskStatus_t *pTasks = (TaskStatus_t *)malloc(sizeof(TaskStatus_t) * n);
    n = uxTaskGetSystemState(pTasks, n, nullptr);

    for (int i = 0; i < n; i++)
    {
        Debug_printf("T%02d %p c%c (%2d,%2d) %4dh %10dr %8s: %s\n",
                     i + 1,
                     pTasks[i].xHandle,
                     pTasks[i].xCoreID == tskNO_AFFINITY ? '_' : ('0' + pTasks[i].xCoreID),
                     pTasks[i].uxBasePriority, pTasks[i].uxCurrentPriority,
                     pTasks[i].usStackHighWaterMark,
                     pTasks[i].ulRunTimeCounter,
                     status[pTasks[i].eCurrentState],
                     pTasks[i].pcTaskName);
    }
    Debug_printf("\nCPU MHz: %d\n", fnSystem.get_cpu_frequency());
#endif
}
