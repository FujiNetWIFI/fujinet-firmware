#ifdef BUILD_LYNX
#include "8bithub.h"
#include "../../bus/comlynx/comlynx.h"

#include "../../include/debug.h"
#include "../../include/pinmap.h"

#include <stdio.h>
#include "fnSystem.h"
#include "fnDNS.h"
#include "utils.h"

// 8Bit Hub General
#define SLOTS    8     // Number of tcp/udp handles
#define PACKET   256   // Max. packet length (bytes)

// 8Bit Hub packet headers
#define HEADER1           0xAA
#define HEADER2           0x55

// 8Bit Hub Commands
#define HUB_SYS_ERROR     0x00 //0
#define HUB_SYS_RESET     0x01 //1
#define HUB_SYS_NOTIF     0x02 //2
#define HUB_SYS_SCAN      0x03 //3
#define HUB_SYS_CONNECT   0x04 //4
#define HUB_SYS_IP        0x05 //5
#define HUB_SYS_MOUSE     0x06 //6
#define HUB_SYS_VERSION   0x07 //7
#define HUB_SYS_UPDATE    0x08 //8
#define HUB_SYS_STATE     0x09 //9   // COM 
#define HUB_SYS_RESEND    0x09 //9   // ESP
#define HUB_DIR_LS        0x0A //10  // Todo: Implement for root directory /microSD
#define HUB_DIR_MK        0x0B //11
#define HUB_DIR_RM        0x0C //12
#define HUB_DIR_CD        0x0D //13
#define HUB_FILE_OPEN     0x15 //21
#define HUB_FILE_SEEK     0x16 //22
#define HUB_FILE_READ     0x17 //23
#define HUB_FILE_WRITE    0x18 //24
#define HUB_FILE_CLOSE    0x19 //25
#define HUB_UDP_OPEN      0x1E //30
#define HUB_UDP_RECV      0x1F //31
#define HUB_UDP_SEND      0x20 //32
#define HUB_UDP_CLOSE     0x21 //33
#define HUB_UDP_SLOT      0x22 //34
#define HUB_TCP_OPEN      0x28 //40
#define HUB_TCP_RECV      0x29 //41
#define HUB_TCP_SEND      0x2A //42
#define HUB_TCP_CLOSE     0x2B //43
#define HUB_TCP_SLOT      0x2C //44
#define HUB_WEB_OPEN      0x32 //50
#define HUB_WEB_RECV      0x33 //51
#define HUB_WEB_HEADER    0x34 //52
#define HUB_WEB_BODY      0x35 //53
#define HUB_WEB_SEND      0x36 //54
#define HUB_WEB_CLOSE     0x37 //55
#define HUB_URL_GET       0x3C //60
#define HUB_URL_READ      0x3D //61

// Enable 8Bit Hub mode
void lynx8bithub::comlynx_8bithub_enable()
{
    hubActive = true;
#ifdef DEBUG
    Debug_println("8BitHub mode ENABLED");
#endif
}

// Disable 8Bit Hub mode
void lynx8bithub::comlynx_8bithub_disable()
{
    hubActive = false;
#ifdef DEBUG
    Debug_println("8BitHub mode DISABLED");
#endif
}

// UDP Port Open
void lynx8bithub::comlynx_8bithub_udpopen(uint8_t len)
{
    unsigned char buffer[PACKET];
    char newIP[16];

    fnUartSIO.readBytes(buffer, len+1);

    sprintf(newIP, "%d.%d.%d.%d", buffer[0], buffer[1], buffer[2], buffer[3]);
    in_addr_t ip = get_ip4_addr_by_name(newIP); // maybe someday we will have hostnames

    int outPort = buffer[4] | buffer[5] << 8;
    int inPort = buffer[6] | buffer[7] << 8;

    comlynx_send(HEADER2); // ACK
#ifdef DEBUG_8BITHUB
    util_dump_bytes(buffer, len+1);
#endif
    Debug_printf("8BitHub: UDP Open IP %s OutPort %d InPort %d\n", newIP, outPort, inPort);
}

void lynx8bithub::comlynx_handle_8bithub()
{
    unsigned char buffer[PACKET];
    uint8_t head, cmd, dev, len = 0;

    if (fnUartSIO.available() > 0)
    {
        head = comlynx_recv();

        // If header doesn't match, ignore it
        if (head != HEADER1 && head != HEADER2)
        {
            while(fnUartSIO.available() > 0)
                fnUartSIO.read(); // read and trash

            return;
        }
        
        // Get the command
        cmd = comlynx_recv();

        if (head == HEADER1) // 0xAA GET FROM LYNX
        {
            len = comlynx_recv();
#ifdef DEBUG_8BITHUB
                Debug_printf("8BitHub: Head 0x%x, CMD 0x%x, Len %d, Data:\n", head, cmd, len);
#endif
            switch (cmd)
            {
            case HUB_SYS_RESET:
                fnUartSIO.readBytes(buffer, len+1);
#ifdef DEBUG_8BITHUB
                util_dump_bytes(buffer, len+1);
#endif
                Debug_println("8BitHub: reset request");
                comlynx_send(HEADER2); // ACK
                break;
            case HUB_SYS_IP:
                // Send back IP address
                break;
            case HUB_UDP_OPEN:
                // Open UDP channel
                comlynx_8bithub_udpopen(len);
                break;
            default:
                break;
            }
        }
        else // 0x55 SEND TO LYNX
        {
            dev = comlynx_recv();
        }
      
    }

    return;
}

void lynx8bithub::comlynx_process(uint8_t b)
{
    // Nothing to do here
    return;
}

#endif /* BUILD_ATARI */