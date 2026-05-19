#ifndef NETWORKPROTOCOLCPM_H
#define NETWORKPROTOCOLCPM_H

#include "Protocol.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
#include <thread>
#include <atomic>
#endif

/**
 * NetworkProtocolCPM
 *
 * Embeds a RunCPM Z80 CP/M 2.2 emulator as a network protocol adapter.
 * open()  — allocates queues and launches the CPM task/thread
 * read()  — drains CPM stdout (characters the emulator has printed)
 * write() — feeds bytes into CPM stdin (keystrokes sent to the emulator)
 * close() — signals the CCP to exit and tears down the task/thread
 */
class NetworkProtocolCPM : public NetworkProtocol
{
public:
    NetworkProtocolCPM(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);
    virtual ~NetworkProtocolCPM();

    fujiError_t open(PeoplesUrlParser *urlParser,
                     fileAccessMode_t access,
                     netProtoTranslation_t translate) override;

    fujiError_t close() override;

    fujiError_t read(unsigned short len) override;

    fujiError_t write(unsigned short len) override;

    fujiError_t status(NetworkStatus *status) override;

    size_t available() override;

private:
    bool running = false;

    void stopCPM();

#ifdef ESP_PLATFORM
    TaskHandle_t cpmTaskHandle = nullptr;
#else
    std::thread      cpmThread;
    std::atomic<bool> cpmStopped{false};
#endif
};

#endif /* NETWORKPROTOCOLCPM_H */
