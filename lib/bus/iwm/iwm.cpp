#ifdef BUILD_APPLE

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "iwm.h"
#include "fnSystem.h"

#ifdef ESP_PLATFORM
#include "fnHardwareTimer.h"
#endif

#include <string.h>
#include "../../include/debug.h"
#include "utils.h"
#include "led.h"
#include "string_utils.h"

#include "../device/iwm/disk.h"
#include "../device/iwm/disk2.h"
#include "../device/iwm/fuji.h"
#include "../device/iwm/cpm.h"
#include "../device/iwm/clock.h"

#include "compat_esp.h" // empty IRAM_ATTR macro for FujiNet-PC

/******************************************************************************
Based on:
Apple //c Smartport Compact Flash adapter
Written by Robert Justice  email: rjustice(at)internode.on.net
Ported to Arduino UNO with SD Card adapter by Andrea Ottaviani email: andrea.ottaviani.69(at)gmail.com
SD FAT support added by Katherine Stark at https://gitlab.com/nyankat/smartportsd/
 *****************************************************************************
 * Written for FujiNet ESP32 by @jeffpiep
 * search for "todo" to find things to work on
*/

/* pin assignments for Arduino UNO
from  http://www.users.on.net/~rjustice/SmartportCFA/SmartportSD.htm
IDC20 Disk II 20-pin pins based on
https://www.bigmessowires.com/2015/04/09/more-fun-with-apple-iigs-disks/
*/

// only used when looking for tricky things - will crash after first packet most likely
#undef VERBOSE_IWM
// #define VERBOSE_IWM

//------------------------------------------------------------------------------

//*****************************************************************************
// Function: print_packet
// Parameters: pointer to data, number of bytes to be printed
// Returns: none
//
// Description: prints packet data for debug purposes to the serial port
//*****************************************************************************
void print_packet(uint8_t *data, int bytes)
{
  // int print_len = bytes;
  // if (print_len > 16) print_len = 16;
  // std::string msg = util_hexdump(data, print_len);
  // Debug_printf("\nsize: %d\n%s\n", bytes, msg.c_str());
  // if (print_len != bytes) {
  //   Debug_printf("... truncated");
  // }
}

void print_packet(uint8_t *data)
{
  Debug_printf("\n");
#ifdef DEV_RELAY_SLIP
  for (int i = 0; i < COMMAND_LEN; i++)
    Debug_printf("%02x ", data[i]);
  Debug_printf("\r\n");
#else
  // Debug_printf("packet: %s\r\n", mstr::toHex(data, 16).c_str());
#endif
}

void print_packet_wave(uint8_t *data, int bytes)
{
  int row;
  char tbs[12];

  Debug_printf("\n");
  for (int count = 0; count < bytes; count = count + 12)
  {
    snprintf(tbs, sizeof(tbs), "%04X: ", count);
    Debug_print(tbs);
    for (row = 0; row < 12; row++)
    {
      if (count + row >= bytes)
        Debug_print("         ");
      else
      {
        uint8_t b = data[count + row];
        for (int bnum = 0; bnum < 8; bnum++)
        {
          if (b & 0x80)
          {
            Debug_print("#");
          }
          else
          {
            Debug_print("_");
          }
          b <<= 1;
        }
        Debug_print(".");
      }
    }
    Debug_printf("\r\n");
  }
}

//------------------------------------------------------------------------------

uint8_t iwmDevice::data_buffer[MAX_DATA_LEN] = {0};
int iwmDevice::data_len = 0;

void systemBus::iwm_ack_deassert()
{
  smartport.iwm_ack_set(); // go hi-z
}

void systemBus::iwm_ack_assert()
{
  smartport.iwm_ack_clr();
  smartport.spi_end();
}

#ifndef DEV_RELAY_SLIP
bool systemBus::iwm_phase_val(uint8_t p)
{
  uint8_t phases = _phases; // smartport.iwm_phase_vector();
  if (p < 4)
    return (phases >> p) & 0x01;
  Debug_printf("\r\nphase number out of range");
  return false;
}
#endif

systemBus::iwm_phases_t systemBus::iwm_phases()
{
  iwm_phases_t phasestate = iwm_phases_t::idle;
  // phase lines for smartport bus reset
  // ph3=0 ph2=1 ph1=0 ph0=1
  // phase lines for smartport bus enable
  // ph3=1 ph2=x ph1=1 ph0=x
  uint8_t phases = smartport.iwm_phase_vector();
  switch (phases)
  {
  case 0b1010:
  case 0b1011:
    phasestate = iwm_phases_t::enable;
    break;
  case 0b0101:
    phasestate = iwm_phases_t::reset;
    break;
  default:
    phasestate = iwm_phases_t::idle;
    break;
  }

#ifdef VERBOSE_IWM
  if (phasestate != oldphase)
  {
    // Debug_printf("\r\n%d%d%d%d",iwm_phase_val(0),iwm_phase_val(1),iwm_phase_val(2),iwm_phase_val(3));
    switch (phasestate)
    {
    case iwm_phases_t::idle:
      Debug_printf("\r\nidle");
      break;
    case iwm_phases_t::reset:
      Debug_printf("\r\nreset");
      break;
    case iwm_phases_t::enable:
      Debug_printf("\r\nenable");
    }
    oldphase = phasestate;
  }
#endif

  return phasestate;
}

//------------------------------------------------------

int systemBus::iwm_send_packet(uint8_t source, iwm_packet_type_t packet_type, uint8_t status, const uint8_t *data, uint16_t num)
{
  int r;
  int retry = 5; // host seems to control the retries, this is here so we don't get stuck

  smartport.encode_packet(source, packet_type, status, data, num);
#ifdef DEBUG
  //print_packet(smartport.packet_buffer,BLOCK_PACKET_LEN); // print raw packet contents to be sent
#endif
  do
  {
    r = smartport.iwm_send_packet_spi();
    retry--;
  } while (r && retry); // retry if we get an error and haven't tried too many times

  return r;
}

bool systemBus::iwm_decode_data_packet(uint8_t *data, int &n)
{
  n = smartport.decode_data_packet(data);
  return false;
}

void systemBus::setup(void)
{
  Debug_printf("\r\nIWM FujiNet based on SmartportSD v1.15\r\n");

#ifndef DEV_RELAY_SLIP
  fnTimer.config();
  Debug_printf("\r\nFujiNet Hardware timer started");

  diskii_xface.setup_rmt();
  Debug_printf("\r\nRMT configured for Disk ][ Output");
#endif

  smartport.setup_spi();
  Debug_printf("\r\nSPI configured for smartport I/O");

  smartport.setup_gpio();
  Debug_printf("\r\nIWM GPIO configured");
  }

//*****************************************************************************
// Function: send_init_reply_packet
// Parameters: source
// Returns: none
//
// Description: this is the reply to the init command packet. A reply indicates
// the original dest id has a device on the bus. If the STAT byte is 0, (0x80)
// then this is not the last device in the chain. This is written to support up
// to 4 partions, i.e. devices, so we need to specify when we are doing the last
// init reply.
//*****************************************************************************
void iwmDevice::send_init_reply_packet(uint8_t source, uint8_t status)
{
  SYSTEM_BUS.iwm_send_packet(source, iwm_packet_type_t::status, status, nullptr, 0);
}

void iwmDevice::send_reply_packet(uint8_t status)
{
  SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::status, status, nullptr, 0);
}

void iwmDevice::iwm_return_badcmd(iwm_decoded_cmd_t cmd)
{
  //Handle possible data packet to avoid crash extended and non-extended
  switch(cmd.command)
  {
    case SP_ECMD_WRITEBLOCK:
    case SP_ECMD_CONTROL:
    case SP_ECMD_WRITE:
    case SP_CMD_WRITEBLOCK:
    case SP_CMD_CONTROL:
    case SP_CMD_WRITE:
      data_len = 512;
      SYSTEM_BUS.iwm_decode_data_packet((uint8_t *)data_buffer, data_len);
      Debug_printf("\r\nUnit %02x Bad Command with data packet %02x\r\n", id(), cmd.command);
      print_packet((uint8_t *)data_buffer, data_len);
      break;
    default: //just send the response and return like before
      send_reply_packet(SP_ERR_BADCMD);
      Debug_printf("\r\nUnit %02x Bad Command %02x", id(), cmd.command);
      return;
  }

  if(cmd.command == SP_CMD_CONTROL) //Decode command control code
  {
    send_reply_packet(SP_ERR_BADCTL); //we may be required to accept some control commands
                                      // but for now just report bad control if it's a control
                                      // command
    uint8_t control_code = get_status_code(cmd);
    Debug_printf("\r\nbad command was a control command with control code %02x",control_code);
  }
  else
  {
    send_reply_packet(SP_ERR_BADCMD); //response for Any other command with a data packet
  }
}

void iwmDevice::iwm_return_device_offline(iwm_decoded_cmd_t cmd)
{
  //Handle possible data packet to avoid crash extended and non-extended
  switch(cmd.command)
  {
    case SP_ECMD_WRITEBLOCK:
    case SP_ECMD_CONTROL:
    case SP_ECMD_WRITE:
    case SP_CMD_WRITEBLOCK:
    case SP_CMD_CONTROL:
    case SP_CMD_WRITE:
      data_len = 512;
      SYSTEM_BUS.iwm_decode_data_packet((uint8_t *)data_buffer, data_len);
      Debug_printf("\r\nUnit %02x Offline, Command with data packet %02x\r\n", id(), cmd.command);
      print_packet((uint8_t *)data_buffer, data_len);
      break;
    default: //just send the response and return like before
      send_reply_packet(SP_ERR_OFFLINE);
      Debug_printf("\r\nUnit %02x Offline, Command %02x", id(), cmd.command);
      return;
  }

  if(cmd.command == SP_CMD_CONTROL) //Decode command control code
  {
    send_reply_packet(SP_ERR_OFFLINE);
    uint8_t control_code = get_status_code(cmd);
    Debug_printf("\r\nOffline command was a control command with control code %02x",control_code);
  }
  else
  {
    send_reply_packet(SP_ERR_OFFLINE); //response for Any other command with a data packet
  }
}

void iwmDevice::iwm_return_ioerror()
{
  // Debug_printf("\r\nUnit %02x Bad Command %02x", id(), cmd.command);
  send_reply_packet(SP_ERR_IOERROR);
}

void iwmDevice::iwm_return_noerror()
{
  send_reply_packet(SP_ERR_NOERROR);
}

void iwmDevice::iwm_status(iwm_decoded_cmd_t cmd) // override;
{
  uint8_t status_code = cmd.params[2];

  if (status_code == SP_CMD_FORMAT)
  {
    Debug_printf("\r\nSending DIB Status for device 0x%02x", id());
    send_status_dib_reply_packet();
  }
  else
  {
    Debug_printf("\r\nSending Device Status for device 0x%02x", id());
    send_status_reply_packet();
  }
}

// Create a vector from the input for the various send_status_dib_reply_packet routines to call
// data[0]                = status
// data[1..1+block_size]  = block bytes - 3 bytes except in some unused code!!
// data[..1 byte ]        = name real size
// data[..16 bytes ]      = name padded with spaces to 16 bytes
// data[..2 bytes]        = device type
// data[..2 byte]         = device version
std::vector<uint8_t> iwmDevice::create_dib_reply_packet(const std::string& device_name, uint8_t status, const std::vector<uint8_t>& block_size, const std::array<uint8_t, 2>& type, const std::array<uint8_t, 2>& version)
{
    std::vector<uint8_t> data;
    data.push_back(status);
    data.insert(data.end(), block_size.begin(), block_size.end());
    data.push_back(static_cast<uint8_t>(device_name.size()));

    data.insert(data.end(), device_name.begin(), device_name.end());
    size_t padding_size = 16 - device_name.size();
    for (int i = 0; i < padding_size; i++) {
      data.push_back(' ');
    }

    data.insert(data.end(), type.begin(), type.end());
    data.insert(data.end(), version.begin(), version.end());

    // std::string ddump = util_hexdump(data.data(), data.size());
    // Debug_printv("DIB DATA");
    // Debug_printf("%s\r\n", ddump.c_str());

    return data;
}

//*****************************************************************************
// Function: main loop
/*
 * notes:
 * with individual devices, like disk.cpp,
 * we need to hand off control to the device to service
 * the command packet.
 *
 * Disk II/3.5 selection is determined by the ENABLE lines
 * from BMOW - https://www.bigmessowires.com/2015/04/09/more-fun-with-apple-iigs-disks/
 * On an Apple II, things are more complicated. The Apple 5.25 controller card was the first to use a DB19 connector, and it supported two daisy-chained 5.25 inch drives. Pin 17 is /DRIVE1 enable, and pin 9 (unconnected on the Macintosh) is /DRIVE2 enable. Within each drive, internal circuitry routes the signal from input pin 9 to output pin 17 on the daisy-chain connector. Drive #2 doesn’t actually know that it’s drive #2 – it enables itself by observing /DRIVE1 on pin 17, just like the first drive – only the first drive has sneakily rerouted /DRIVE2 to /DRIVE1. This allows for two drives to be daisy chained.
 * On an Apple IIgs, it’s even more complicated. Its DB19 connector supports daisy-chaining two 3.5 inch drives, and two 5.25 inch drives – as well as even more SmartPort drives, which I won’t discuss now. Pin 4 (GND on the Macintosh) is /EN3.5, a new signal that enables the 3.5 inch drives when it’s low, or the 5.25 inch drives when it’s high. The 3.5 inch drives must appear before any 5.25 inch drives in the daisy chain. When /EN3.5 is low, the 3.5 inch drives use pins 17 and 9 to enable themselves, and when /EN3.5 is high, the 3.5 inch drives pass through the signals on pins 17 and 9 unmodified to the 5.25 drives behind them.
 * This is getting complicated, but there’s one final kick in the nuts: when the first 3.5 drive is enabled, by the IIgs setting /EN3.5 and /DRIVE1 both low, you would think the drive would disable the next 3.5 drive behind it by setting both /DRIVE1 and /DRIVE2 high at the daisy-chain connector. But no, the first 3.5 drive disables the second 3.5 drive by setting both /DRIVE1 and /DRIVE2 low! This looks like both are enabled at the same time, which would be a definite no-no, but the Apple 3.5 Drive contains circuitry that recognizes this “double enable” as being equivalent to a disable. Why it’s done this way, I don’t know, but I’m sure it has some purpose.
 *
 * So for starters FN will look at the /DRIVEx line (not sure which one because IIc has internal floppy drive connected right now)
 * If floppy is enabled, the motor is spinning and FN needs to track the phases and spit out data (unless writereq is activated)
 * If floppy is disabled, smartport should be in control instead.
 *
 * The smartport algorithm is something like:
 * check for 0x85 init and do a bus initialization:
 * BUS INIT
 * after a reset, all devices no longer have an address
 * and they are gating some signal (REQ?) so devices
 * down the chain cannot respond to commands. So the
 * first device responds to INIT. During this, it checks
 * the sense line (still not sure which pin this is) to see
 * if it is low (grounded) or high (floating or pulled up?).
 * It will be low if there's another device in the chain
 * after it. If it is the last device it will be high.
 * It sends this state in the response to INIT. It also
 * ungates whatever the magic line is so the next device
 * in the chain can receive the INIT command that is
 * coming next. This repeats until the last device in the
 * chain says it's so and the A2 will stop sending INITs.
 *
 * Every other command:
 * The bus class checks the target device and should pass
 * on the command packet to the device service routine.
 * Then the device can respond accordingly.
 *
 * When device ID is not FujiNet's:
 * If the device ID does not belong to any of the FujiNet
 * devices (disks, printers, modem, network, etc) then FN
 * should not respond. The SmartPortSD code runs through
 * the states for the packets that should come next. I'm
 * not sure this is the best because what happens in case
 * of a malfunction. I suppose there could be a time out
 * that takes us back to idle. This will take more
 * investigation.
 */
//*****************************************************************************
void IRAM_ATTR systemBus::service()
{
#ifndef DEV_RELAY_SLIP
  // process smartport before diskII
  if (!serviceSmartPort())
    serviceDiskII();

  serviceDiskIIWrite();
#else
  serviceSmartPort();
#endif
}

// Returns true if SmartPort was handled
bool IRAM_ATTR systemBus::serviceSmartPort()
{
  // read phase lines to check for smartport reset or enable
  switch (iwm_phases())
  {
  case iwm_phases_t::idle:
    return false;

  case iwm_phases_t::reset:
    Debug_printf("\r\nReset");

    // clear all the device addresses
    for (auto devicep : _daisyChain)
      devicep->_devnum = 0;

#ifndef DEV_RELAY_SLIP
    while (iwm_phases() == iwm_phases_t::reset)
      portYIELD(); // no timeout needed because the IWM must eventually clear reset.
    // even if it doesn't, we would just come back to here, so might as
    // well wait until reset clears.

    Debug_printf("\r\nReset Cleared");

    // if /EN35 is high, we must be on a host that supports 3.5 dumb drives
    // lets sample it here in case the host is not on when the FN is powered on/reset
    IWM_BIT(SP_EN35) ? en35Host = true : en35Host = false;
    Debug_printf("\r\nen35Host = %d",en35Host);
#endif /* !SLIP */

    break;

  case iwm_phases_t::enable:
    // expect a command packet
    // should not ACK unless we know this is our Command

    // force floppy off when SP bus is enabled, needed for softsp
    if (_old_enable_state != iwm_enable_state_t::off)
    {
      _old_enable_state = iwm_enable_state_t::off;
#ifndef DEV_RELAY_SLIP
      diskii_xface.stop();
#endif /* !SLIP */
    }

    if (sp_command_mode != sp_cmd_state_t::command)
    {
      // iwm_ack_deassert(); // go hi-Z
      return true;
    }

    if ((command_packet.command & 0x7f) == 0x05)
    {
      // wait for REQ to go low
      if (iwm_req_deassert_timeout(50000))
      {
        // iwm_ack_deassert(); // go hi-Z
        return true;
      }

#ifdef DEBUG
      print_packet(command_packet.data);
      Debug_printf("\r\nhandling init command");
#endif
      handle_init();
    }
    else
    {
      for (auto devicep : _daisyChain)
      {
        // This could be a map of _devnum to devicep, then wouldn't have to loop.
        if (command_packet.dest == devicep->_devnum)
        {
          // wait for REQ to go low
          if (iwm_req_deassert_timeout(50000))
          {
            Debug_printf("\nREQ timeout in command processing");
            iwm_ack_deassert(); // go hi-Z
            return true;
          }
#ifndef DEV_RELAY_SLIP
          // need to take time here to service other ESP processes so they can catch up
          taskYIELD(); // Allow other tasks to run
#endif
          // Debug_printf("\r\nCommand Packet:");
          // print_packet(command_packet.data);

          _activeDev = devicep;
          // handle command
          memset(command.decoded, 0, sizeof(command.decoded));
          smartport.decode_data_packet(command_packet.data, command.decoded);
          print_packet(command.decoded, 9);
          _activeDev->process(command);
          break; // we don't need to needlessly keep looping once we find it
        }
      }
    }

    sp_command_mode = sp_cmd_state_t::standby;
    memset(command_packet.data, 0, sizeof(command_packet));

    iwm_ack_deassert(); // go hi-Z
  }                     // switch (phasestate)

  return true;
}

#ifndef DEV_RELAY_SLIP
// Returns true if Disk II was handled
bool IRAM_ATTR systemBus::serviceDiskII()
{
  // check on the diskii status
  switch (iwm_motor_state())
  {
  case iwm_enable_state_t::off:
    return false;

  case iwm_enable_state_t::off2on:
    // need to start a counter and wait to turn on enable output after 1 ms only iff enable state is on
    if (IWM_ACTIVE_DISK2->device_active)
    {
      fnSystem.delay(1); // need a better way to figure out persistence
      if (iwm_motor_state() == iwm_enable_state_t::on)
      {
        current_disk2 = diskii_xface.iwm_active_drive();
        IWM_ACTIVE_DISK2->change_track(0); // copy current track in for this drive
        diskii_xface.start(diskii_xface.iwm_active_drive() - 1,
                           IWM_ACTIVE_DISK2->readonly); // start it up
      }
    } // make a call to start the RMT stream
    else
    {
      diskii_xface.set_output_to_low(); // not sure if best way to trick IIc into booting SP
      // alternative approach is to enable RMT to spit out PRN bits
    }
    // make sure the state machine moves on to iwm_enable_state_t::on
    break;

  case iwm_enable_state_t::on:
    if (current_disk2 != diskii_xface.iwm_active_drive())
    {
      current_disk2 = diskii_xface.iwm_active_drive();
      if (IWM_ACTIVE_DISK2->device_active) {
        IWM_ACTIVE_DISK2->change_track(0); // copy current track in for this drive
        diskii_xface.start(diskii_xface.iwm_active_drive() - 1,
                           IWM_ACTIVE_DISK2->readonly); // start it up
      }
    }
    diskii_xface.d2_enable_seen |= diskii_xface.iwm_active_drive();
#ifdef DEBUG
    new_track = IWM_ACTIVE_DISK2->get_track_pos();
    if (old_track != new_track)
    {
      Debug_printf("\ntrk pos %02i.%i/Q%03d on d%d",
                   new_track / 4, new_track % 4,
                   new_track, diskii_xface.iwm_active_drive());
      old_track = new_track;
    }
#endif
    break;

  case iwm_enable_state_t::on2off:
    fnSystem.delay(1); // need a better way to figure out persistence
    diskii_xface.stop();
    iwm_ack_deassert();
    break;
  }

  return true;
}

// Returns true if a Disk II write was received
bool IRAM_ATTR systemBus::serviceDiskIIWrite()
{
  iwm_write_data item;
  int sector_num;
  uint8_t *decoded;
  size_t decode_len;
  size_t sector_start, sector_end;
  bool found_start, found_end;
  size_t bitlen, used;


  if (!xQueueReceive(diskii_xface.iwm_write_queue, &item, 0))
    return false;

  Debug_printf("\r\nDisk II iwm queue receive %u %u %u %u",
	       item.length, item.track_begin, item.track_end, item.track_numbits);
  // gap 1            = 16 * 10
  // sector header    = 10 * 8          [D5 AA 96] + 4 + [DE AA EB]
  // gap 2            = 7 * 10
  // sector data      = (6 + 343) * 8   [D5 AA AD] + 343 + [DE AA EB]
  // gap 3            = 16 * 10
  // per sector bits  = 3102

  // Take advantage of fixed sector positions of mediaTypeDSK serialise_track()
  // (as listed above)
  sector_num = (item.track_begin - 16 * 10) / 3102;

  bitlen = (item.track_end + item.track_numbits - item.track_begin) % item.track_numbits;
  Debug_printf("\r\nDisk II write Qtrack/sector: %i/%i  bit_len: %i",
	       item.quarter_track, sector_num, bitlen);
  if (bitlen) {
    decoded = (uint8_t *) malloc(item.length);
    decode_len = diskii_xface.iwm_decode_buffer(item.buffer, item.length,
                                                smartport.f_spirx, D2W_CHUNK_SIZE * 2 * 8,
                                                decoded, &used);
    Debug_printf("\r\nDisk II used: %u %lx", used, decoded);

    // Find start of sector: D5 AA AD
    for (sector_start = 0; decode_len > 349 && sector_start <= decode_len - 349; sector_start++)
      if (decoded[sector_start]      == 0xD5
          && decoded[sector_start+1] == 0xAA
          && decoded[sector_start+2] == 0xAD)
        break;
    found_start = sector_start <= decode_len - 349;

    // Find end of sector too: DE AA EB
    for (sector_end = 0; decode_len > 3 && sector_end <= decode_len - 3; sector_end++)
      if (decoded[sector_end]      == 0xDE
          && decoded[sector_end+1] == 0xAA
          && decoded[sector_end+2] == 0xEB)
        break;
    found_end = sector_end <= decode_len - 3;

    if (!found_start && found_end) {
      Debug_printf("\r\nDisk II no prologue found");
#if 0
      sector_start = sector_end - 346;
      found_start = true;
#endif
    }

    if (found_start && found_end && sector_end - sector_start == 346) {
      uint8_t sector_data[343]; // Need enough room to demap and de-xor
      uint16_t checksum;

      // This printf nudges timing too much
      // Debug_printf("\r\nDisk II sector data: %i", sector_start + 3);
      checksum = decode_6_and_2(sector_data, &decoded[sector_start + 3]);
      if ((checksum >> 8) != (checksum & 0xff))
        Debug_printf("\r\nDisk II checksum mismatch: %04x", checksum);

      iwmDisk2 *disk_dev = IWM_ACTIVE_DISK2;
      disk_dev->write_sector(item.quarter_track, sector_num, sector_data);
      disk_dev->change_track(0);
    }
    else {
      Debug_printf("\r\nDisk II sector not found");
    }

    // FIXME - is there another sector to decode?

    free(decoded);
  }

  free(item.buffer);

  return true;
}

iwm_enable_state_t IRAM_ATTR systemBus::iwm_motor_state()
{
  uint8_t phases = smartport.iwm_phase_vector();
  uint8_t newstate = diskii_xface.iwm_active_drive();

  if (!((phases & 0b1000) && (phases & 0b0010))) // SP bus not enabled
  {
    switch (_old_enable_state)
    {
    case iwm_enable_state_t::off:
      _new_enable_state = (newstate != 0) ? iwm_enable_state_t::off2on : iwm_enable_state_t::off;
      break;
    case iwm_enable_state_t::off2on:
      _new_enable_state = (newstate != 0) ? iwm_enable_state_t::on : iwm_enable_state_t::on2off;
      break;
    case iwm_enable_state_t::on:
      _new_enable_state = (newstate != 0) ? iwm_enable_state_t::on : iwm_enable_state_t::on2off;
      break;
    case iwm_enable_state_t::on2off:
      _new_enable_state = (newstate != 0) ? iwm_enable_state_t::off2on : iwm_enable_state_t::off;
      break;
    }
    if (_old_enable_state != _new_enable_state)
      Debug_printf("\ndisk ii [%i] enable states: %02x", newstate, _new_enable_state);

    _old_enable_state = _new_enable_state;

    return _new_enable_state;
  }
  else
  {
    return iwm_enable_state_t::off;
  }
}
#endif /* !DEV_RELAY_SLIP */

void systemBus::handle_init()
{
  uint8_t status = 0;
  iwmDevice *pDevice = nullptr;

  fnLedManager.set(LED_BUS, true);

  // iwm_rddata_clr();

  // to do - get the next device in the daisy chain and assign ID
  for (auto it = _daisyChain.begin(); it != _daisyChain.end(); ++it)
  {
    // assign dev numbers
    pDevice = (*it);
    pDevice->switched = false; //reset switched condition on init
    if (pDevice->id() == 0)
    {
      pDevice->_devnum = command_packet.dest; // assign address
      if (++it == _daisyChain.end())
        status = 0xff; // end of the line, so status=non zero - to do: check GPIO for another device in the physical daisy chain
      Debug_printf("\r\nSending INIT Response Packet...");
      pDevice->send_init_reply_packet(command_packet.dest, status);

      // smartport.iwm_send_packet_spi((uint8_t *)pDevice->packet_buffer); // timeout error return is not handled here (yet?)

      // print_packet ((uint8_t*) packet_buffer,get_packet_length());

      Debug_printf("\r\nDrive: %02x\r\n", pDevice->id());
      fnLedManager.set(LED_BUS, false);
      return;
    }
  }

  fnLedManager.set(LED_BUS, false);
}

// Add device to SIO bus
void systemBus::addDevice(iwmDevice *pDevice, iwm_fujinet_type_t deviceType)
{
  // SmartPort interface assigns device numbers to the devices in the daisy chain one at a time
  // as opposed to using standard or fixed device ID's like Atari SIO. Therefore, an emulated
  // device cannot rely on knowing its device number until it is assigned.
  // Instead of using device_id's to know what kind a specific device is, smartport
  // uses a Device Information Block (DIB) that is returned in a status call for DIB. The
  // DIB includes a 16-character string, Device type byte, and Device subtype byte.
  // In the IIgs firmware reference, the following device types are defined:
  // 0 - memory cards (internal to the machine)
  // 1 - Apple and Uni 3.5 drives
  // 2 - harddisk
  // 3 - SCSI disk
  // The subtype uses the 3 msb's to indicate the following:
  // 0x80 == 1 -> support extended smartport
  // 0x40 == 1 -> supprts disk-switched errors
  // 0x20 == 0 -> removable media (1 means non removable)

  // todo: work out how to use addDevice
  // we can add devices and indicate they are not initialized and have no device ID - call it a value of 0
  // when the SP bus goes into RESET, we would rip through the list setting initialized to false and
  // setting device id's to 0. Then on each INIT command, we iterate through the list, setting
  // initialized to true and assigning device numbers as assigned by the smartport controller in the A2.
  // so I need "reset()" and "initialize()" functions.

  // todo: I need a way to internally keep track of what kind of device each one is. I'm thinking an
  // enumerated class type might work well here. It can be expanded as needed and an extra case added
  // below. I can also make this a switch case structure to ensure each case of the class is handled.

  // assign dedicated pointers to certain devices
  switch (deviceType)
  {
  case iwm_fujinet_type_t::BlockDisk:
    break;
  case iwm_fujinet_type_t::FujiNet:
    _fujiDev = (iwmFuji *)pDevice;
    break;
  case iwm_fujinet_type_t::Modem:
    _modemDev = (iwmModem *)pDevice;
    break;
  case iwm_fujinet_type_t::Network:
    break;
  case iwm_fujinet_type_t::CPM:
    _cpmDev = (iwmCPM *)pDevice;
    break;
  case iwm_fujinet_type_t::Printer:
    _printerdev = (iwmPrinter *)pDevice;
    break;
  case iwm_fujinet_type_t::Voice:
    // not yet implemented: todo - take SAM and implement as a special block device. Also then available for disk rotate annunciation.
    break;
  case iwm_fujinet_type_t::Clock:
    _clockDev = (iwmClock *)pDevice;
    break;
  case iwm_fujinet_type_t::Other:
    break;
  }

  pDevice->_devnum = 0;
  pDevice->_initialized = false;

  _daisyChain.push_front(pDevice);
}

// Removes device from the SIO bus.
// Note that the destructor is called on the device!
void systemBus::remDevice(iwmDevice *p)
{
  _daisyChain.remove(p);
}

// Should avoid using this as it requires counting through the list
int systemBus::numDevices()
{
  int i = 0;
  __BEGIN_IGNORE_UNUSEDVARS
  for (auto devicep : _daisyChain)
    i++;
  return i;
  __END_IGNORE_UNUSEDVARS
}

void systemBus::changeDeviceId(iwmDevice *p, int device_id)
{
  for (auto devicep : _daisyChain)
  {
    if (devicep == p)
      devicep->_devnum = device_id;
  }
}

iwmDevice *systemBus::deviceById(int device_id)
{
  for (auto devicep : _daisyChain)
  {
    if (devicep->_devnum == device_id)
      return devicep;
  }
  return nullptr;
}

void systemBus::enableDevice(uint8_t device_id)
{
  iwmDevice *p = deviceById(device_id);
  p->device_active = true;
}

void systemBus::disableDevice(uint8_t device_id)
{
  iwmDevice *p = deviceById(device_id);
  p->device_active = false;
}

// Give devices an opportunity to clean up before a reboot
void systemBus::shutdown()
{
  shuttingDown = true;

  for (auto devicep : _daisyChain)
  {
    Debug_printf("Shutting down device %02x\n", (unsigned)devicep->id());
    devicep->shutdown();
  }
  Debug_printf("All devices shut down.\n");
}
#endif /* BUILD_APPLE */
