// Arudino needed for CONFIGTIME
#include <Arduino.h>
//#include <WiFiUdp.h>
#include "../tcpip/fnUDP.h"

#include "apetime.h"

#define NTP_TIMESTAMP_DELTA 2208988800ull

#ifndef htons
#define htons(x) ( ((x)<< 8 & 0xFF00) | \
                   ((x)>> 8 & 0x00FF) )
#endif

#ifndef ntohs
#define ntohs(x) htons(x)
#endif

#ifndef htonl
#define htonl(x) ( ((x)<<24 & 0xFF000000UL) | \
                   ((x)<< 8 & 0x00FF0000UL) | \
                   ((x)>> 8 & 0x0000FF00UL) | \
                   ((x)>>24 & 0x000000FFUL) )
#endif

#ifndef ntohl
#define ntohl(x) htonl(x)
#endif

union {
    struct
    {

        uint8_t li_vn_mode; // Eight bits. li, vn, and mode.
                            // li.   Two bits.   Leap indicator.
                            // vn.   Three bits. Version number of the protocol.
                            // mode. Three bits. Client will pick mode 3 for client.

        uint8_t stratum;   // Eight bits. Stratum level of the local clock.
        uint8_t poll;      // Eight bits. Maximum interval between successive messages.
        uint8_t precision; // Eight bits. Precision of the local clock.

        uint32_t rootDelay;      // 32 bits. Total round trip delay time.
        uint32_t rootDispersion; // 32 bits. Max error aloud from primary clock source.
        uint32_t refId;          // 32 bits. Reference clock identifier.

        uint32_t refTm_s; // 32 bits. Reference time-stamp seconds.
        uint32_t refTm_f; // 32 bits. Reference time-stamp fraction of a second.

        uint32_t origTm_s; // 32 bits. Originate time-stamp seconds.
        uint32_t origTm_f; // 32 bits. Originate time-stamp fraction of a second.

        uint32_t rxTm_s; // 32 bits. Received time-stamp seconds.
        uint32_t rxTm_f; // 32 bits. Received time-stamp fraction of a second.

        uint32_t txTm_s; // 32 bits and the most important field the client cares about. Transmit time-stamp seconds.
        uint32_t txTm_f; // 32 bits. Transmit time-stamp fraction of a second.

    } ntp_packet; // Total: 384 bits or 48 bytes.
    unsigned char rawData[48];
} ntpdata;

void sioApeTime::sio_time()
{
#ifdef DEBUG
    Debug_println("APETIME time query");
#endif

    time_t txTim;
    tm *now;
    
    configTime(tz.gmt,tz.dst,"pool.ntp.org");

    memset(&ntpdata.rawData, 0, sizeof(ntpdata.rawData));
    ntpdata.ntp_packet.li_vn_mode = 0x1b;

    //WiFiUDP udp;
    fnUDP udp;
    // Send NTP packet request
    udp.beginPacket("pool.ntp.org", 123);
    udp.write(ntpdata.rawData, sizeof(ntpdata.rawData));
    udp.endPacket();

    delay(100);

    if (udp.parsePacket())
    {
#ifdef DEBUG
    Debug_println("parsePacket succeeded");
#endif
        udp.read(ntpdata.rawData, sizeof(ntpdata.rawData));
        ntpdata.ntp_packet.rxTm_s=ntohl(ntpdata.ntp_packet.rxTm_s);
        txTim = (time_t)(ntpdata.ntp_packet.rxTm_s - NTP_TIMESTAMP_DELTA);
        now = localtime(&txTim);

        now->tm_mon++;
        now->tm_year-=100;

        byte sio_ts[6] = {
            (byte)now->tm_mday,
            (byte)now->tm_mon,
            (byte)now->tm_year,
            (byte)now->tm_hour,
            (byte)now->tm_min,
            (byte)now->tm_sec};
        sio_to_computer(sio_ts, sizeof(sio_ts), false);
    }
    else
    {
#ifdef DEBUG
    Debug_println("parsePacket failed");
#endif
        byte sio_ts[6]={0,0,0,0,0,0};
        sio_to_computer(sio_ts,sizeof(sio_ts),true);
    }

}

void sioApeTime::sio_timezone()
{
#ifdef DEBUG
    Debug_println("APETIME TZ response");
#endif
    sio_to_peripheral(tz.rawData,sizeof(tz.rawData));
    sio_complete();
}

void sioApeTime::sio_status()
{
}

void sioApeTime::sio_process()
{
    switch (cmdFrame.comnd)
    {
    case 0x93:
        sio_ack();
        sio_time();
        break;
    case 0xFE:
        sio_ack();
        sio_timezone();
        break;
    default:
        sio_nak();
        break;
    };
}
