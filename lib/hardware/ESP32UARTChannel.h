#ifndef ESP32UARTCHANNEL_H
#define ESP32UARTCHANNEL_H

#include "IOChannel.h"
#include "RS232ChannelProtocol.h"

#ifdef ESP_PLATFORM

#include "pinmap.h"
#include <driver/uart.h>
#include <hal/uart_types.h>

#define FN_UART_DEBUG   UART_NUM_0
#if defined(BUILD_RS232) || defined(PINMAP_COCO_ESP32S3) || defined(PINMAP_COCO_RS232)
#  define FN_UART_BUS   UART_NUM_1
#else
#  define FN_UART_BUS   UART_NUM_2
#endif

struct RS232ControlPins
{
    int rx, tx, rts, cts, dtr, dsr, dcd, ri;
};

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
    uint32_t read_timeout_ms = IOCHANNEL_DEFAULT_TIMEOUT;
    uint32_t discard_timeout_ms = IOCHANNEL_DEFAULT_TIMEOUT;
    RS232ControlPins pins = {
        .rts = PIN_RS232_RTS,
        .cts = PIN_RS232_CTS,
        .dtr = PIN_RS232_DTR,
        .dsr = PIN_RS232_DSR,
        .dcd = PIN_RS232_DCD,
        .ri = PIN_RS232_RI,
    };

    ChannelConfig& baud(int baud) {
        uart_config.baud_rate = baud; return *this;
    }
    ChannelConfig& dataBits(uart_word_length_t bits) {
        uart_config.data_bits = bits; return *this;
    }
    ChannelConfig& parity(uart_parity_t par) {
        uart_config.parity = par; return *this;
    }
    ChannelConfig& stopBits(uart_stop_bits_t bits) {
        uart_config.stop_bits = bits; return *this;
    }
    ChannelConfig& flowControl(uart_hw_flowcontrol_t flow) {
        uart_config.flow_ctrl = flow; return *this;
    }
    ChannelConfig& inverted(bool inv) {
        isInverted = inv; return *this;
    }
    ChannelConfig& deviceID(uart_port_t num) {
        device = num; return *this;
    }
    ChannelConfig& readTimeout(uint32_t millis) {
        read_timeout_ms = millis; return *this;
    }
    ChannelConfig& discardTimeout(uint32_t millis) {
        discard_timeout_ms = millis; return *this;
    }
    ChannelConfig& rtsPin(int num) {
        pins.rts = num; return *this;
    }
    ChannelConfig& ctsPin(int num) {
        pins.cts = num; return *this;
    }
    ChannelConfig& dtrPin(int num) {
        pins.dtr = num; return *this;
    }
    ChannelConfig& dsrPin(int num) {
        pins.dsr = num; return *this;
    }
    ChannelConfig& dcdPin(int num) {
        pins.dcd = num; return *this;
    }
    ChannelConfig& ri(int num) {
        pins.ri = num; return *this;
    }
};

class ESP32UARTChannel : public IOChannel, public RS232ChannelProtocol
{
private:
    uart_port_t _uart_num;
    QueueHandle_t _uart_q;
    RS232ControlPins controlPins;

protected:
    bool getPin(int pin);
    void setPin(int pin, bool state);

    void updateFIFO() override;
    size_t dataOut(const void *buffer, size_t length) override;

public:
    void begin(const ChannelConfig& conf);
    void end() override;

    void flushOutput() override;

    uint32_t getBaudrate() override;
    void setBaudrate(uint32_t baud) override;

    // FujiNet acts as modem (DCE), computer serial ports are DTE.
    bool getDTR() override;           // modem DTR input  → actually reads RS-232 DSR pin
    void setDSR(bool state) override; // modem DSR output → actually drives RS-232 DTR pin
    bool getRTS() override;           // modem RTS input  → actually reads RS-232 CTS pin
    void setCTS(bool state) override; // modem CTS output → actually drives RS-232 RTS pin
    void setDCD(bool state) override; // modem DCD output → drives RS-232 DCD pin
    void setRI(bool state) override;  // modem RI output → drives RS-232 RI pin
};

extern ESP32UARTChannel fnDebugConsole;

#endif /* ESP_PLATFORM */

#endif /* ESP32UARTCHANNEL_H */
