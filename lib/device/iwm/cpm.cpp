#ifdef BUILD_APPLE
#define CCP_INTERNAL

#include "cpm.h"

#include "fnSystem.h"
#include "fnUART.h"
#include "fnWiFi.h"
#include "fuji.h"
#include "fnFS.h"
#include "fnFsSD.h"

#include "../hardware/led.h"

#include "../runcpm/abstraction_fujinet_apple2.h"

#include "../runcpm/globals.h"
#include "../runcpm/ram.h"     // ram.h - Implements the RAM
#include "../runcpm/console.h" // console.h - implements console.
#include "../runcpm/cpu.h"     // cpu.h - Implements the emulated CPU
#include "../runcpm/disk.h"    // disk.h - Defines all the disk access abstraction functions
#include "../runcpm/host.h"    // host.h - Custom host-specific BDOS call
#include "../runcpm/cpm.h"     // cpm.h - Defines the CPM structures and calls
#ifdef CCP_INTERNAL
#include "../runcpm/ccp.h" // ccp.h - Defines a simple internal CCP
#endif

static void cpmTask(void *arg)
{
    while (1)
    {
        Status = Debug = 0;
        Break = Step = -1;
        RAM = (uint8_t *)malloc(MEMSIZE);
        memset(RAM, 0, MEMSIZE);
        memset(filename, 0, sizeof(filename));
        memset(newname, 0, sizeof(newname));
        memset(fcbname, 0, sizeof(fcbname));
        memset(pattern, 0, sizeof(pattern));
        vTaskDelay(100);
        _puts(CCPHEAD);
        _PatchCPM();
        _ccp();
    }
}

iwmCPM::iwmCPM()
{
    rxq = xQueueCreate(2048,sizeof(char));
    txq = xQueueCreate(2048,sizeof(char));
}

void iwmCPM::encode_status_reply_packet()
{
    uint8_t checksum = 0;
    uint8_t data[4];

    // Build the contents of the packet
    data[0] = STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE;
    data[1] = 0; // block size 1
    data[2] = 0; // block size 2
    data[3] = 0; // block size 3
    // to do - just call encode data using the data[] array?
    packet_set_sync_bytes();

    packet_buffer[6] = 0xc3;  // PBEGIN - start byte
    packet_buffer[7] = 0x80;  // DEST - dest id - host
    packet_buffer[8] = id();  // d.device_id; // SRC - source id - us
    packet_buffer[9] = PACKET_TYPE_STATUS;  // TYPE -status
    packet_buffer[10] = 0x80; // AUX
    packet_buffer[11] = 0x80; // STAT - data status
    packet_buffer[12] = 0x84; // ODDCNT - 4 data bytes
    packet_buffer[13] = 0x80; // GRP7CNT
    // 4 odd bytes
    packet_buffer[14] = 0x80 | ((data[0] >> 1) & 0x40) | ((data[1] >> 2) & 0x20) | ((data[2] >> 3) & 0x10) | ((data[3] >> 4) & 0x08); // odd msb
    packet_buffer[15] = data[0] | 0x80;                                                                                               // data 1
    packet_buffer[16] = data[1] | 0x80;                                                                                               // data 2
    packet_buffer[17] = data[2] | 0x80;                                                                                               // data 3
    packet_buffer[18] = data[3] | 0x80;                                                                                               // data 4

    for (int i = 0; i < 4; i++)
    { // calc the data bytes checksum
        checksum ^= data[i];
    }
    for (int count = 7; count < 14; count++) // xor the packet header bytes
        checksum = checksum ^ packet_buffer[count];
    packet_buffer[19] = checksum | 0xaa;      // 1 c6 1 c4 1 c2 1 c0
    packet_buffer[20] = checksum >> 1 | 0xaa; // 1 c7 1 c5 1 c3 1 c1

    packet_buffer[21] = 0xc8; // PEND
    packet_buffer[22] = 0x00; // end of packet in buffer
}

void iwmCPM::encode_status_dib_reply_packet()
{
    int grpbyte, grpcount, i;
    int grpnum, oddnum;
    uint8_t checksum = 0, grpmsb;
    uint8_t group_buffer[7];
    uint8_t data[25];
    // data buffer=25: 3 x Grp7 + 4 odds
    grpnum = 3;
    oddnum = 4;

    //* write data buffer first (25 bytes) 3 grp7 + 4 odds
    // General Status byte
    // Bit 7: Block  device
    // Bit 6: Write allowed
    // Bit 5: Read allowed
    // Bit 4: Device online or disk in drive
    // Bit 3: Format allowed
    // Bit 2: Media write protected (block devices only)
    // Bit 1: Currently interrupting (//c only)
    // Bit 0: Currently open (char devices only)
    data[0] = STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE;
    data[1] = 0;    // block size 1
    data[2] = 0;    // block size 2
    data[3] = 0;    // block size 3
    data[4] = 0x03; // ID string length - 11 chars
    data[5] = 'C';
    data[6] = 'P';
    data[7] = 'M';
    data[8] = ' ';
    data[9] = ' ';
    data[10] = ' ';
    data[11] = ' ';
    data[12] = ' ';
    data[13] = ' ';
    data[14] = ' ';
    data[15] = ' ';
    data[16] = ' ';
    data[17] = ' ';
    data[18] = ' ';
    data[19] = ' ';
    data[20] = ' ';                         // ID string (16 chars total)
    data[21] = SP_TYPE_BYTE_FUJINET_CPM;    // Device type    - 0x02  harddisk
    data[22] = SP_SUBTYPE_BYTE_FUJINET_CPM; // Device Subtype - 0x0a
    data[23] = 0x00;                        // Firmware version 2 bytes
    data[24] = 0x01;                        //

    // print_packet ((uint8_t*) data,get_packet_length()); // debug
    // Debug_print(("\nData loaded"));
    // Calculate checksum of sector bytes before we destroy them
    for (int count = 0; count < 25; count++) // xor all the data bytes
        checksum = checksum ^ data[count];

    // Start assembling the packet at the rear and work
    // your way to the front so we don't overwrite data
    // we haven't encoded yet

    // grps of 7
    for (grpcount = grpnum - 1; grpcount >= 0; grpcount--) // 3
    {
        for (i = 0; i < 7; i++)
        {
            group_buffer[i] = data[i + oddnum + (grpcount * 7)]; // data should have 26 cells?
        }
        // add group msb byte
        grpmsb = 0;
        for (grpbyte = 0; grpbyte < 7; grpbyte++)
            grpmsb = grpmsb | ((group_buffer[grpbyte] >> (grpbyte + 1)) & (0x80 >> (grpbyte + 1)));
        packet_buffer[(14 + oddnum + 1) + (grpcount * 8)] = grpmsb | 0x80; // set msb to one

        // now add the group data bytes bits 6-0
        for (grpbyte = 0; grpbyte < 7; grpbyte++)
            packet_buffer[(14 + oddnum + 2) + (grpcount * 8) + grpbyte] = group_buffer[grpbyte] | 0x80;
    }

    // odd byte
    packet_buffer[14] = 0x80 | ((data[0] >> 1) & 0x40) | ((data[1] >> 2) & 0x20) | ((data[2] >> 3) & 0x10) | ((data[3] >> 4) & 0x08); // odd msb
    packet_buffer[15] = data[0] | 0x80;
    packet_buffer[16] = data[1] | 0x80;
    packet_buffer[17] = data[2] | 0x80;
    packet_buffer[18] = data[3] | 0x80;
    ;

    packet_set_sync_bytes();

    packet_buffer[6] = 0xc3;  // PBEGIN - start byte
    packet_buffer[7] = 0x80;  // DEST - dest id - host
    packet_buffer[8] = id();  // d.device_id; // SRC - source id - us
    packet_buffer[9] = PACKET_TYPE_STATUS;  // TYPE -status
    packet_buffer[10] = 0x80; // AUX
    packet_buffer[11] = 0x80; // STAT - data status
    packet_buffer[12] = 0x84; // ODDCNT - 4 data bytes
    packet_buffer[13] = 0x83; // GRP7CNT - 3 grps of 7

    for (int count = 7; count < 14; count++) // xor the packet header bytes
        checksum = checksum ^ packet_buffer[count];
    packet_buffer[43] = checksum | 0xaa;      // 1 c6 1 c4 1 c2 1 c0
    packet_buffer[44] = checksum >> 1 | 0xaa; // 1 c7 1 c5 1 c3 1 c1

    packet_buffer[45] = 0xc8; // PEND
    packet_buffer[46] = 0x00; // end of packet in buffer
}

void iwmCPM::sio_status()
{
    // Nothing to do here
    return;
}

void iwmCPM::iwm_open(cmdPacket_t cmd)
{
    Debug_printf("\r\nOpen CP/M Unit # %02x\n", cmd.g7byte1);
    encode_status_reply_packet();
    IWM.iwm_send_packet((unsigned char *)packet_buffer);
}

void iwmCPM::iwm_close(cmdPacket_t cmd)
{
    // Probably need to send close command here.
    Debug_printf("\r\nClose CP/M Unit # %02x\n", cmd.g7byte1);
    encode_status_reply_packet();
    IWM.iwm_send_packet((unsigned char *)packet_buffer);
}

void iwmCPM::iwm_status(cmdPacket_t cmd)
{
    uint8_t source = cmd.dest;                                                // we are the destination and will become the source // packet_buffer[6];
    uint8_t status_code = (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80); // status codes 00-FF
    Debug_printf("\r\nDevice %02x Status Code %02x\n", source, status_code);
    Debug_printf("\r\nStatus List is at %02x %02x\n", cmd.g7byte1 & 0x7f, cmd.g7byte2 & 0x7f);

    switch (status_code)
    {
    case IWM_STATUS_STATUS: // 0x00
        encode_status_reply_packet();
        IWM.iwm_send_packet((unsigned char *)packet_buffer);
        return;
        break;
    // case IWM_STATUS_DCB:                  // 0x01
    // case IWM_STATUS_NEWLINE:              // 0x02
    case IWM_STATUS_DIB: // 0x03
        encode_status_dib_reply_packet();
        IWM.iwm_send_packet((unsigned char *)packet_buffer);
        return;
        break;
    case 'S': // Status
        unsigned short mw = uxQueueMessagesWaiting(rxq);
        packet_buffer[0] = mw & 0xFF;
        packet_buffer[1] = mw >> 8;
        packet_len = 2;
        Debug_printf("%u bytes waiting\n",mw);
        break;
    }

    Debug_printf("\r\nStatus code complete, sending response");
    //encode_data_packet(packet_len);
    encode_packet(id(), iwm_packet_type_t::data, 0, packet_buffer, packet_len);

    IWM.iwm_send_packet((unsigned char *)packet_buffer);
}

void iwmCPM::iwm_read(cmdPacket_t cmd)
{
    uint8_t source = cmd.dest; // we are the destination and will become the source // packet_buffer[6];

    uint16_t numbytes = (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80);
    numbytes |= ((cmd.g7byte4 & 0x7f) | ((cmd.grp7msb << 4) & 0x80)) << 8;

    uint32_t addy = (cmd.g7byte5 & 0x7f) | ((cmd.grp7msb << 5) & 0x80);
    addy |= ((cmd.g7byte6 & 0x7f) | ((cmd.grp7msb << 6) & 0x80)) << 8;
    addy |= ((cmd.g7byte7 & 0x7f) | ((cmd.grp7msb << 7) & 0x80)) << 16;

    Debug_printf("\r\nDevice %02x Read %04x bytes from address %06x\n", source, numbytes, addy);

    memset(packet_buffer,0,sizeof(packet_buffer));

    for (int i=0;i<numbytes;i++)
    {
        char b;
        xQueueReceive(rxq,&b,portMAX_DELAY);
        packet_buffer[i] = b;
        packet_len++;
    }

    Debug_printf("%s\n",packet_buffer);

    //encode_data_packet(packet_len);
    encode_packet(id(), iwm_packet_type_t::data, 0, packet_buffer, packet_len);
    Debug_printf("\r\nsending block packet ...");
    IWM.iwm_send_packet((unsigned char *)packet_buffer);
    packet_len = 0;
    memset(packet_buffer, 0, sizeof(packet_buffer));
}

void iwmCPM::iwm_write(cmdPacket_t cmd)
{
    uint8_t source = cmd.dest; // packet_buffer[6];
    // to do - actually we will already know that the cmd.dest == id(), so can just use id() here
    Debug_printf("\r\nCPM# %02x ", source);

    uint16_t num_bytes = (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80);
    num_bytes |= ((cmd.g7byte4 & 0x7f) | ((cmd.grp7msb << 4) & 0x80)) << 8;

    uint32_t addy = (cmd.g7byte5 & 0x7f) | ((cmd.grp7msb << 5) & 0x80);
    addy |= ((cmd.g7byte6 & 0x7f) | ((cmd.grp7msb << 6) & 0x80)) << 8;
    addy |= ((cmd.g7byte7 & 0x7f) | ((cmd.grp7msb << 7) & 0x80)) << 16;

    Debug_printf("\nWrite %u bytes to address %04x\n", num_bytes);

    // get write data packet, keep trying until no timeout
    //  to do - this blows up - check handshaking
    if (IWM.iwm_read_packet_timeout(100, (unsigned char *)packet_buffer, BLOCK_PACKET_LEN))
    {
        Debug_printf("\r\nTIMEOUT in read packet!");
        return;
    }
    // partition number indicates which 32mb block we access
    if (decode_data_packet())
        iwm_return_ioerror(cmd);
    else
    {
        // DO write
    }
}

void iwmCPM::iwm_ctrl(cmdPacket_t cmd)
{
    uint8_t err_result = SP_ERR_NOERROR;

    uint8_t source = cmd.dest;                                                 // we are the destination and will become the source // packet_buffer[6];
    uint8_t control_code = (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80); // ctrl codes 00-FF
    Debug_printf("\r\nDevice %02x Control Code %02x", source, control_code);
    Debug_printf("\r\nControl List is at %02x %02x", cmd.g7byte1 & 0x7f, cmd.g7byte2 & 0x7f);
    IWM.iwm_read_packet_timeout(100, (uint8_t *)packet_buffer, BLOCK_PACKET_LEN);
    Debug_printf("\r\nThere are %02x Odd Bytes and %02x 7-byte Groups", packet_buffer[11] & 0x7f, packet_buffer[12] & 0x7f);
    decode_data_packet();
    print_packet((uint8_t *)packet_buffer);

    switch (control_code)
    {
    case 'B': // Boot
        Debug_printf("!!! STARTING CP/M TASK!!!\n");
        xTaskCreate(cpmTask, "cpmtask", 32768, NULL, 11, &cpmTaskHandle);
        break;
    case 'W': // Write
        Debug_printf("Pushing character %c",packet_buffer[0]);
        xQueueSend(txq,&packet_buffer[0],portMAX_DELAY);
        break;
    }

    encode_reply_packet(err_result);
    IWM.iwm_send_packet((unsigned char *)packet_buffer);
}

void iwmCPM::process(cmdPacket_t cmd)
{
    fnLedManager.set(LED_BUS, true);
    switch (cmd.command)
    {
    case 0x80: // status
        Debug_printf("\r\nhandling status command");
        iwm_status(cmd);
        break;
    case 0x84: // control
        Debug_printf("\r\nhandling control command");
        iwm_ctrl(cmd);
        break;
    case 0x88: // read
        Debug_printf("\r\nhandling read command");
        iwm_read(cmd);
        break;
    default:
        iwm_return_badcmd(cmd);
        break;
    } // switch (cmd)
    fnLedManager.set(LED_BUS, false);
}

void iwmCPM::shutdown()
{
    // TODO: clean shutdown.
}

#endif /* BUILD_APPLE2 */