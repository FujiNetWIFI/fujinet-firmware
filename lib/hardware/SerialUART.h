/* Basically a simplified copy of the ESP Arduino library in HardwareSerial.h/HardwareSerial.cpp
*/
#ifndef SERIALUART_H
#define SERIALUART_H

#include "SerialInterface.h"

#include <driver/uart.h>
#include <hal/uart_types.h>

#define FN_UART_DEBUG   UART_NUM_0
#if defined(BUILD_RS232) || defined(PINMAP_COCO_ESP32S3) || defined(PINMAP_COCO_RS232)
#  define FN_UART_BUS   UART_NUM_1
#else
#  define FN_UART_BUS   UART_NUM_2
#endif

struct SerialUARTConfig
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
    
    SerialUARTConfig& baud(int baud) {
        uart_config.baud_rate = baud; return *this;
    }
    SerialUARTConfig& dataBits(uart_word_length_t bits) {
        uart_config.data_bits = bits; return *this;
    }
    SerialUARTConfig& parity(uart_parity_t par) {
        uart_config.parity = par; return *this;
    }
    SerialUARTConfig& stopBits(uart_stop_bits_t bits) {
        uart_config.stop_bits = bits; return *this;
    }
    SerialUARTConfig& flowControl(uart_hw_flowcontrol_t flow) {
        uart_config.flow_ctrl = flow; return *this;
    }
    SerialUARTConfig& inverted(bool inv) {
        isInverted = inv; return *this;
    }
};

class SerialUART : public SerialInterface
{
private:
    std::string fifo;
    //size_t fifo_avail;
    uart_port_t _uart_num;
    QueueHandle_t _uart_q;

    void checkRXQueue();;
    
public:
    void begin(uart_port_t uart_num, const SerialUARTConfig& conf);
    void end() override;
    size_t recv(void *buffer, size_t length) override;
    size_t send(const void *buffer, size_t length) override;

    size_t available() override;
    void flush() override;
    void discardInput() override;
    
    uint32_t getBaudrate() override;
    void setBaudrate(uint32_t baud) override;

    bool dtrState() override;

};

extern SerialUART fnDebugConsole;

#endif //FNUART_H
