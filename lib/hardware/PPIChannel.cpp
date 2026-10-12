#include "PPIChannel.h"

#ifdef PIN_PPI_OBF

#include <driver/gpio.h>
#include <esp_rom_sys.h>
#include <freertos/task.h>
#include <hal/gpio_ll.h>
#include <soc/gpio_reg.h>
#include <soc/soc.h>

#include "../../include/debug.h"

#define PPI_TX_TIMEOUT_US   1000000 // host stopped reading the reply
#define PPI_TX_SPIN_US      2000    // busy-poll this long, then yield between polls
#define PPI_STROBE_US       100     // 82C55 raises IBF within ns of /STB
#define PPI_WARN_US         1000000

static const gpio_num_t dataPins[8] = {
    PIN_PPI_D0, PIN_PPI_D1, PIN_PPI_D2, PIN_PPI_D3,
    PIN_PPI_D4, PIN_PPI_D5, PIN_PPI_D6, PIN_PPI_D7,
};

static inline void addPin(gpio_num_t pin, uint32_t &mask0, uint32_t &mask1)
{
    if (pin < 32)
        mask0 |= 1UL << pin;
    else
        mask1 |= 1UL << (pin - 32);
}

void PPIChannel::begin()
{
    // Strobes idle high before the pins become outputs
    gpio_set_level(PIN_PPI_STB, 1);
    gpio_set_level(PIN_PPI_ACK, 1);

    gpio_config_t io = {};
    io.pin_bit_mask = BIT64(PIN_PPI_STB) | BIT64(PIN_PPI_ACK);
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);

    // Driven through the card's 5V dividers, so no pulls
    io.pin_bit_mask = BIT64(PIN_PPI_IBF) | BIT64(PIN_PPI_OBF)
        | BIT64(PIN_PPI_DIR) | BIT64(PIN_PPI_OE);
    io.mode = GPIO_MODE_INPUT;
    gpio_config(&io);

    io.pin_bit_mask = 0;
    _dataMask0 = _dataMask1 = 0;
    for (gpio_num_t pin : dataPins)
    {
        io.pin_bit_mask |= BIT64(pin);
        addPin(pin, _dataMask0, _dataMask1);
    }
    io.mode = GPIO_MODE_INPUT;
    io.pull_down_en = GPIO_PULLDOWN_ENABLE;
    gpio_config(&io);
    // Route GPIO_OUT without enabling it: the host may already hold DIR toward us
    for (gpio_num_t pin : dataPins)
        gpio_ll_matrix_out_default(&GPIO, pin);

    Debug_printf("PPIChannel: DIR=%d /OE=%d IBF=%d /OBF=%d\n",
                 gpio_get_level(PIN_PPI_DIR), gpio_get_level(PIN_PPI_OE),
                 gpio_get_level(PIN_PPI_IBF), gpio_get_level(PIN_PPI_OBF));
}

void PPIChannel::end()
{
    releaseData();
    gpio_set_level(PIN_PPI_STB, 1);
    gpio_set_level(PIN_PPI_ACK, 1);
}

// D0-D7 only drive while a byte is strobed out; otherwise the 245 may be driving them
void PPIChannel::releaseData()
{
    REG_WRITE(GPIO_ENABLE_W1TC_REG, _dataMask0);
    REG_WRITE(GPIO_ENABLE1_W1TC_REG, _dataMask1);
}

void PPIChannel::driveData(uint8_t c)
{
    uint32_t set0 = 0, set1 = 0;

    for (int i = 0; i < 8; i++)
        if (c & (1 << i))
            addPin(dataPins[i], set0, set1);

    REG_WRITE(GPIO_OUT_W1TS_REG, set0);
    REG_WRITE(GPIO_OUT_W1TC_REG, _dataMask0 & ~set0);
    REG_WRITE(GPIO_OUT1_W1TS_REG, set1);
    REG_WRITE(GPIO_OUT1_W1TC_REG, _dataMask1 & ~set1);
    REG_WRITE(GPIO_ENABLE_W1TS_REG, _dataMask0);
    REG_WRITE(GPIO_ENABLE1_W1TS_REG, _dataMask1);
}

uint8_t PPIChannel::sampleData()
{
    uint32_t in0 = REG_READ(GPIO_IN_REG);
    uint32_t in1 = REG_READ(GPIO_IN1_REG);
    uint8_t c = 0;

    for (int i = 0; i < 8; i++)
    {
        gpio_num_t pin = dataPins[i];
        uint32_t bit = pin < 32 ? in0 >> pin : in1 >> (pin - 32);
        c |= (bit & 1) << i;
    }

    return c;
}

void PPIChannel::warn(const char *msg)
{
    uint64_t now = esp_timer_get_time();

    if (_lastWarn && now - _lastWarn < PPI_WARN_US)
        return;
    _lastWarn = now;
    Debug_println(msg);
}

void PPIChannel::updateFIFO()
{
    // /OBF low: the host wrote a byte to port A
    while (!gpio_get_level(PIN_PPI_OBF))
    {
        // /ACK makes the 82C55 drive port A, so the 245 must be pointing at us
        if (!gpio_get_level(PIN_PPI_DIR) || gpio_get_level(PIN_PPI_OE))
        {
            warn("PPIChannel: /OBF low but 245 not set host->ESP32");
            return;
        }

        // The host flips DIR back soon after /OBF rises; don't get preempted mid-read
        portENTER_CRITICAL(&_mux);
        gpio_set_level(PIN_PPI_ACK, 0);
        esp_rom_delay_us(1);
        uint8_t c = sampleData();
        gpio_set_level(PIN_PPI_ACK, 1);
        portEXIT_CRITICAL(&_mux);

        _fifo += (char) c;
    }
}

size_t PPIChannel::dataOut(const void *buffer, size_t length)
{
    const uint8_t *ptr = (const uint8_t *) buffer;
    size_t idx;

    for (idx = 0; idx < length; idx++)
    {
        // Wait for the host to empty the input latch with the 245 pointing at it
        uint64_t start = esp_timer_get_time();
        while (gpio_get_level(PIN_PPI_IBF) || gpio_get_level(PIN_PPI_DIR)
               || gpio_get_level(PIN_PPI_OE))
        {
            // A new request means the host stopped reading this reply; it holds
            // DIR toward us until /ACK, so waiting here would deadlock
            if (!gpio_get_level(PIN_PPI_OBF))
            {
                warn("PPIChannel: host sending, reply cut short");
                return idx;
            }

            uint64_t waited = esp_timer_get_time() - start;
            if (waited > PPI_TX_TIMEOUT_US)
            {
                warn("PPIChannel: host not reading, reply dropped");
                return idx;
            }
            if (waited > PPI_TX_SPIN_US)
                vTaskDelay(1);
        }

        driveData(ptr[idx]);
        gpio_set_level(PIN_PPI_STB, 0);
        start = esp_timer_get_time();
        while (!gpio_get_level(PIN_PPI_IBF))
        {
            if (esp_timer_get_time() - start > PPI_STROBE_US)
            {
                warn("PPIChannel: no IBF after /STB");
                break;
            }
        }
        gpio_set_level(PIN_PPI_STB, 1);
        releaseData();
    }

    return idx;
}

#endif /* PIN_PPI_OBF */
