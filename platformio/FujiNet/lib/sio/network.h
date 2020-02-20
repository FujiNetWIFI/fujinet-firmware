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


class sioNetwork : public sioDevice
{
    WiFiClient client;
    WiFiServer* server;
    HTTPClient* http_client;

    unsigned char eol_mode;
    bool eol_skip_byte;
    unsigned char tcp_input_buffer[TCP_INPUT_BUFFER_SIZE];
    unsigned char tcp_input_buffer_len;
    unsigned char tcp_output_buffer[TCP_OUTPUT_BUFFER_SIZE];
    unsigned char tcp_output_buffer_len[NUM_DEVICES];

    void set_eol_mode();
    void clear_input_buffer();
    void clear_output_buffer();
    void fill_tcp_output_buffer(byte req_len);

    void tcp_open();
    void tcp_close();
    void tcp_read();
};

#endif /* NETWORK_H */