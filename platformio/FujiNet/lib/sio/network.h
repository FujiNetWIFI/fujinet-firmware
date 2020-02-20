#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>

#ifdef ESP8266
#include <ESP8266Wifi.h>
#endif

#ifdef ESP32
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFiServer.h>
#include <HTTPClient.h>
#endif

#include "sio.h"

#define NUM_DEVICES 8
#define TCP_INPUT_BUFFER_SIZE 256
#define TCP_OUTPUT_BUFFER_SIZE 256
#define DEVICESPEC_SIZE 256


class sioNetwork : public sioDevice
{

private:
    WiFiClient client;
    WiFiServer* server;
    HTTPClient* http_client;
    char* tmp;

    unsigned char eol_mode;
    bool eol_skip_byte;
    unsigned char tcp_input_buffer[TCP_INPUT_BUFFER_SIZE];
    unsigned char tcp_input_buffer_len;
    unsigned char tcp_output_buffer[TCP_OUTPUT_BUFFER_SIZE];
    unsigned char tcp_output_buffer_len;
    union 
    {
        struct
        {
            char device[4];
            char protocol[16];
            char path[234];
            unsigned short port;
        };
        char rawData[DEVICESPEC_SIZE];
    } deviceSpec;

public:
    void open();
    void close();
    void read();
    void write();
    void status();


    bool parse_deviceSpec();

};

#endif /* NETWORK_H */