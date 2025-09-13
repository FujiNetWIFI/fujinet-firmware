/* Basically a simplified copy of the ESP Arduino library in HardwareSerial.h/HardwareSerial.cpp
*/
#ifndef UARTCHANNEL_H
#define UARTCHANNEL_H

#include "IOChannel.h"

#ifdef ESP_PLATFORM

#include <driver/uart.h>
#include <hal/uart_types.h>

#define FN_UART_DEBUG   UART_NUM_0
#if defined(BUILD_RS232) || defined(PINMAP_COCO_ESP32S3) || defined(PINMAP_COCO_RS232)
#  define FN_UART_BUS   UART_NUM_1
#else
#  define FN_UART_BUS   UART_NUM_2
#endif

struct ChannelConfig
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {
            .allow_pd = 0,
            .backup_before_sleep = 0,
        }
    };
    bool isInverted = false;
    uart_port_t device;
    uint32_t read_timeout_ms = 10;
    uint32_t discard_timeout_ms = 10;

    ChannelConfig &baud(int baud)
    {
        uart_config.baud_rate = baud;
        return *this;
    }
    ChannelConfig &dataBits(uart_word_length_t bits)
    {
        uart_config.data_bits = bits;
        return *this;
    }
    ChannelConfig &parity(uart_parity_t par)
    {
        uart_config.parity = par;
        return *this;
    }
    ChannelConfig &stopBits(uart_stop_bits_t bits)
    {
        uart_config.stop_bits = bits;
        return *this;
    }
    ChannelConfig &flowControl(uart_hw_flowcontrol_t flow)
    {
        uart_config.flow_ctrl = flow;
        return *this;
    }
    ChannelConfig &inverted(bool inv)
    {
        isInverted = inv;
        return *this;
    }
    ChannelConfig &deviceID(uart_port_t num)
    {
        device = num;
        return *this;
    }
    ChannelConfig &readTimeout(uint32_t millis)
    {
        read_timeout_ms = millis;
        return *this;
    }
    ChannelConfig &discardTimeout(uint32_t millis)
    {
        discard_timeout_ms = millis;
        return *this;
    }
};

class UARTChannel : public IOChannel
{
private:
    uart_port_t _uart_num;
    QueueHandle_t _uart_q;

protected:
    void update_fifo() override;
    size_t dataOut(const void *buffer, size_t length) override;

public:
    void begin(const ChannelConfig &conf);
    void end() override;

    void flush() override;

    uint32_t getBaudrate() override;
    void setBaudrate(uint32_t baud) override;

    bool dtrState();
};

extern UARTChannel fnDebugConsole;

#endif /* ESP_PLATFORM */

#endif /* UARTCHANNEL_H */
