#ifndef NETWORK_H
#define NETWORK_H

#ifdef ESP_PLATFORM
#include <esp_timer.h>
#endif

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "bus.h"
#include "fnjson.h"
#include "network_data.h"
#include "peoples_url_parser.h"
#include "Protocol.h"
// #include "ProtocolParser.h"


/**
 * Number of devices to expose via APPLE2
 */
#define NUM_DEVICES 4

/**
 * The size of rx and tx buffers
 */
#define INPUT_BUFFER_SIZE 65535
#define OUTPUT_BUFFER_SIZE 65535
#define SPECIAL_BUFFER_SIZE 256

class iwmNetwork : public virtualDevice
{

public:

    /**
     * Constructor
     */
    iwmNetwork();

    /**
     * Destructor
     */
    virtual ~iwmNetwork();

    /**
     * The spinlock for the ESP32 hardware timers. Used for interrupt rate limiting.
     */
#ifdef ESP_PLATFORM // OS
    portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
#endif

    /**
     * Toggled by the rate limiting timer to indicate that the PROCEED interrupt should
     * be pulsed.
     */
    bool interruptProceed = false;

    /**
     * Called for iwm Command 'O' to open a connection to a network protocol, allocate all buffers,
     * and start the receive PROCEED interrupt.
     */
    virtual void open();

    /**
     * Called for iwm Command 'C' to close a connection to a network protocol, de-allocate all buffers,
     * and stop the receive PROCEED interrupt.
     */
    virtual void close();

    /**
     * Write to Network Socket 'W'
     */
    void net_write();

    /**
     * Read from Network Socket 'R'
     */
    void net_read();

    /**
     * iwm Write command
     * Write # of bytes specified by aux1/aux2 from tx_buffer out to iwm. If protocol is unable to return requested
     * number of bytes, return ERROR.
     */
    virtual void write(){};

    /**
     * iwm Special, called as a default for any other iwm command not processed by the other iwmnet_ functions.
     * First, the protocol is asked whether it wants to process the command, and if so, the protocol will
     * process the special command. Otherwise, the command is handled locally. In either case, either iwmnet_complete()
     * or iwmnet_error() is called.
     */
    virtual void status();

    void process(iwm_decoded_cmd_t cmd) override;
    void process_fs(fujiCommandID_t fuji_command);
    void process_tcp(fujiCommandID_t fuji_command);
    void process_http(fujiCommandID_t fuji_command);
    void process_udp(fujiCommandID_t fuji_command);

    void iwm_ctrl(iwm_decoded_cmd_t cmd) override;
    void iwm_open(iwm_decoded_cmd_t cmd) override;
    void iwm_close(iwm_decoded_cmd_t cmd) override;
    void iwm_read(iwm_decoded_cmd_t cmd) override;
    void iwm_write(iwm_decoded_cmd_t cmd) override;
    void iwm_status(iwm_decoded_cmd_t cmd) override;
    void shutdown() override;
    void send_status_reply_packet() override;
    void send_extended_status_reply_packet() override{};
    void send_status_dib_reply_packet() override;
    void send_extended_status_dib_reply_packet() override{};

    /**
     * @brief Called to set prefix
     */
    virtual void set_prefix();

    /**
     * @brief Called to get prefix
     */
    virtual void get_prefix();

    /**
     * @brief called to set login
     */
    virtual void set_login();

    /**
     * @brief called to set password
     */
    virtual void set_password();

    /**
     * @brief set channel mode
     */
    void channel_mode();

    /**
     * @brief parse incoming data
     */
    void json_parse();

    /**
     * @brief JSON Query
     * @param s size of query
     */
    void json_query(iwm_decoded_cmd_t cmd);

    std::unordered_map<uint8_t, NetworkData> network_data_map;
    uint8_t current_network_unit = 1;

private:
    /**
     * SP_ERR number when there's an ... error!
     */
    uint8_t err = SP_ERR_NOERROR;

    /**
     * ESP timer handle for the Interrupt rate limiting timer
     */
#ifdef ESP_PLATFORM // OS
    esp_timer_handle_t rateTimerHandle = nullptr;
#endif

    /**
     * Timer Rate for interrupt timer
     */
    int timerRate = 100;

    /**
     * The current receive state, are we sending channel or status data?
     */
    enum _receive_mode
    {
        CHANNEL,
        STATUS
    } receiveMode = CHANNEL;

    /**
     * saved NetworkStatus items
     */
    unsigned char reservedSave = 0;
    unsigned char errorSave = 1;

    /**
     * Instantiate protocol object
     * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
     */
    bool instantiate_protocol();

    /**
     * Create the deviceSpec and fix it for parsing
     */
    void create_devicespec(std::string d);

    /**
     * Create a urlParser from deviceSpec
    */
   void create_url_parser();

    /**
     * Start the Interrupt rate limiting timer
     */
    void timer_start();

    /**
     * Stop the Interrupt rate limiting timer
     */
    void timer_stop();

    /**
     * We were passed a COPY arg from DOS 2. This is complex, because we need to parse the comma,
     * and figure out one of three states:
     *
     * (1) we were passed D1:FOO.TXT,N:FOO.TXT, the second arg is ours.
     * (2) we were passed N:FOO.TXT,D1:FOO.TXT, the first arg is ours.
     * (3) we were passed N1:FOO.TXT,N2:FOO.TXT, get whichever one corresponds to our device ID.
     *
     * DeviceSpec will be transformed to only contain the relevant part of the deviceSpec, sans comma.
     */
    void processCommaFromDevicespec();

    /**
     * Perform the correct read based on value of channelMode
     * @param num_bytes Number of bytes to read.
     * @return TRUE on error, FALSE on success. Passed directly to bus_to_computer().
     */
    bool read_channel(unsigned short num_bytes, iwm_decoded_cmd_t cmd);

    /**
     * Perform the correct read based on value of channelMode
     * @param num_bytes Number of bytes to read.
     * @return TRUE on error, FALSE on success. Passed directly to bus_to_computer().
     */
    bool read_channel_json(unsigned short num_bytes, iwm_decoded_cmd_t cmd);

    /**
     * Perform the correct write based on value of channelMode
     * @param num_bytes Number of bytes to write.
     * @return TRUE on error, FALSE on success. Used to emit iwmnet_error or iwmnet_complete().
     */
    bool write_channel(unsigned short num_bytes);

    /**
     * @brief perform local status commands, if protocol is not bound, based on cmdFrame
     * value.
     */
    void iwmnet_status_local();

    /**
     * @brief perform channel status commands, if there is a protocol bound.
     */
    void iwmnet_status_channel();

    /**
     * Called to pulse the PROCEED interrupt, rate limited by the interrupt timer.
     */
    void iwmnet_assert_interrupt();

    /**
     * @brief set translation specified by aux1 to aux2_translation mode.
     */
    void iwmnet_set_translation();

    /**
     * @brief Set timer rate for PROCEED timer in ms
     */
    void iwmnet_set_timer_rate();

    /**
     * @brief parse URL and instantiate protocol
     * @param db pointer to devicespecbuf 256 chars
     */
    void parse_and_instantiate_protocol(std::string d);
};

#endif /* NETWORK_H */
