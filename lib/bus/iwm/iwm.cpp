#ifdef BUILD_APPLE
#include "iwm.h"
#include "fnSystem.h"
#include "fnHardwareTimer.h"
// #include "fnFsTNFS.h" // do i need this?
#include <string.h>
// #include "driver/timer.h" // contains the hardware timer register data structure
#include "../../include/debug.h"
#include "utils.h"
#include "led.h"

#include "../device/iwm/disk.h"
#include "../device/iwm/disk2.h"
#include "../device/iwm/fuji.h"
#include "../device/iwm/cpm.h"
#include "../device/iwm/clock.h"

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
void print_packet (uint8_t* data, int bytes)
{
  int row;
  char tbs[8];
  char xx;

  Debug_printf(("\r\n"));
  for (int count = 0; count < bytes; count = count + 16) 
  {
    sprintf(tbs, ("%04X: "), count);
    Debug_print(tbs);
    for (row = 0; row < 16; row++) {
      if (count + row >= bytes)
        Debug_print(("   "));
      else {
        Debug_printf("%02x ",data[count + row]);
      }
    }
    Debug_print(("-"));
    for (row = 0; row < 16; row++) {
      if ((data[count + row] > 31) && (count + row < bytes) && (data[count + row] < 128))
      {
        xx = data[count + row];
        Debug_printf("%c",xx);
      }
      else
      {
        Debug_print(("."));
      }
    }
    Debug_printf(("\r\n"));
  }
}

void print_packet(uint8_t* data)
{
  Debug_printf("\r\n");
  for (int i = 0; i < 40; i++)
  {
    if (data[i]!=0 || i==0)
      Debug_printf("%02x ", data[i]);
    else
      break;
  }
  // Debug_printf("\r\n");
}

void print_packet_wave(uint8_t* data, int bytes)
{
  int row;
  char tbs[8];

  Debug_printf(("\r\n"));
  for (int count = 0; count < bytes; count = count + 12) 
  {
    sprintf(tbs, ("%04X: "), count);
    Debug_print(tbs);
    for (row = 0; row < 12; row++) {
      if (count + row >= bytes)
        Debug_print(("         "));
      else {
        uint8_t b = data[count + row];
        for (int bnum=0; bnum<8; bnum++)
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
    Debug_printf(("\r\n"));
  }
}

//------------------------------------------------------------------------------

// uint8_t iwmDevice::packet_buffer[BLOCK_PACKET_LEN] = { 0 };
// uint16_t iwmDevice::packet_len = 0;
// uint16_t iwmDevice::num_decoded = 0;

uint8_t iwmDevice::data_buffer[MAX_DATA_LEN] = { 0 };
int iwmDevice::data_len = 0;

void iwmBus::iwm_ack_deassert()
{
  smartport.iwm_ack_set(); // go hi-z
}

void iwmBus::iwm_ack_assert()
{
  smartport.iwm_ack_clr();
  smartport.spi_end();
}

bool iwmBus::iwm_phase_val(uint8_t p)
{
  uint8_t phases = _phases; // smartport.iwm_phase_vector();
  if (p < 4)
    return (phases >> p) & 0x01;
  Debug_printf("\r\nphase number out of range");
  return false;
}

iwmBus::iwm_phases_t iwmBus::iwm_phases()
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
    //Debug_printf("\r\n%d%d%d%d",iwm_phase_val(0),iwm_phase_val(1),iwm_phase_val(2),iwm_phase_val(3));
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
    oldphase=phasestate;
  }
#endif

  return phasestate;
}

//------------------------------------------------------

int iwmBus::iwm_send_packet(uint8_t source, iwm_packet_type_t packet_type, uint8_t status, const uint8_t* data, uint16_t num)
{
  smartport.encode_packet(source, packet_type, status, data, num);
  int r = smartport.iwm_send_packet_spi();
  return r;
}

bool iwmBus::iwm_read_packet_timeout(int attempts, uint8_t *data, int &n)
{
  int nn = 17 + n % 7 + (n % 7 != 0) + n * 8 / 7;
  Debug_printf("\r\nAttempting to receive %d length packet", nn);
  portDISABLE_INTERRUPTS();
  iwm_ack_deassert();
  for (int i = 0; i < attempts; i++)
  {
    if (!smartport.iwm_read_packet_spi(nn))
    {
      iwm_ack_assert();
      portENABLE_INTERRUPTS();
#ifdef DEBUG
      print_packet(data);
#endif
      n = smartport.decode_data_packet(data);
      return false;
    } // if
  }
#ifdef DEBUG
  Debug_printf("\r\nERROR: Read Packet tries exceeds %d attempts", attempts);
  // print_packet(data);
#endif
  portENABLE_INTERRUPTS();
  return true;
}


void iwmBus::setup(void)
{
  Debug_printf(("\r\nIWM FujiNet based on SmartportSD v1.15\r\n"));

  fnTimer.config();
  Debug_printf("\r\nFujiNet Hardware timer started");

  smartport.setup_spi();
  Debug_printf("\r\nSPI configured for smartport I/O");
  
  diskii_xface.setup_spi();
  Debug_printf("\r\nSPI configured for Disk ][ Output");

  smartport.setup_gpio();
  Debug_printf("\r\nIWM GPIO configured");
}


//*****************************************************************************
// Function: encode_data_packet
// Parameters: source id
// Returns: none
//
// Description: encode 512 byte data packet for read block command from host
// requires the data to be in the packet buffer, and builds the smartport
// packet IN PLACE in the packet buffer
//*****************************************************************************


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
void iwmDevice::send_init_reply_packet (uint8_t source, uint8_t status)
{
  IWM.iwm_send_packet(source, iwm_packet_type_t::status, status, nullptr, 0);
}

void iwmDevice::send_reply_packet (uint8_t status)
{
  IWM.iwm_send_packet(id(), iwm_packet_type_t::status, status, nullptr, 0);
}

void iwmDevice::iwm_return_badcmd(iwm_decoded_cmd_t cmd)
{
  Debug_printf("\r\nUnit %02x Bad Command %02x", id(), cmd.command);
  send_reply_packet(SP_ERR_BADCMD);
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

//*****************************************************************************
// Function: verify_cmdpkt_checksum
// Parameters: none
// Returns: 0 = ok, 1 = error
//
// Description: verify the checksum for command packets
//
// &&&&&&&&not used at the moment, no error checking for checksum for cmd packet
//*****************************************************************************
bool iwmBus::verify_cmdpkt_checksum(void)
{
  //int length;
  uint8_t evenbits, oddbits, bit7, bit0to6, grpbyte;
  uint8_t calc_checksum = 0; //initial value is 0
  uint8_t pkt_checksum;

  //length = get_packet_length();
  //Debug_printf("\r\npacket length = %d", length);
  //2 oddbytes in cmd packet
  // calc_checksum ^= ((packet_buffer[13] << 1) & 0x80) | (packet_buffer[14] & 0x7f);
  // calc_checksum ^= ((packet_buffer[13] << 2) & 0x80) | (packet_buffer[15] & 0x7f);
  calc_checksum ^= ((command_packet.oddmsb << 1) & 0x80) | (command_packet.command & 0x7f);
  calc_checksum ^= ((command_packet.oddmsb << 2) & 0x80) | (command_packet.parmcnt & 0x7f);

  // 1 group of 7 in a cmd packet
  for (grpbyte = 0; grpbyte < 7; grpbyte++) {
    bit7 = (command_packet.grp7msb << (grpbyte + 1)) & 0x80;
    bit0to6 = (command_packet.data[17 + grpbyte]) & 0x7f;
    calc_checksum ^= bit7 | bit0to6;
  }

  // calculate checksum for overhead bytes
  for (int count = 6; count < 13; count++) // start from first id byte
    calc_checksum ^= command_packet.data[count];

  // int chkidx = 13 + numodd + (numodd != 0) + numgrps * 8;
  // evenbits = packet_buffer[chkidx] & 0x55;
  // oddbits = (packet_buffer[chkidx + 1] & 0x55) << 1;
  oddbits = (command_packet.chksum2 << 1) | 0x01;
  evenbits = command_packet.chksum1;
  pkt_checksum = oddbits & evenbits; // oddbits | evenbits;
  // every other bit is ==1 in checksum, so need to AND to get data back

  //  Debug_print(("Pkt Chksum Byte:\r\n"));
  //  Debug_print(pkt_checksum,DEC);
  //  Debug_print(("Calc Chksum Byte:\r\n"));
  //  Debug_print(calc_checksum,DEC);
  //  Debug_printf("\r\nChecksum - pkt,calc: %02x %02x", pkt_checksum, calc_checksum);
  // if ( pkt_checksum == calc_checksum )
  //   return false;
  // else
  //   return true;
  return (pkt_checksum != calc_checksum);  
}

void iwmDevice::iwm_status(iwm_decoded_cmd_t cmd) // override;
{
  uint8_t status_code = cmd.params[2]; //cmd.g7byte3 & 0x7f; // (packet_buffer[19] & 0x7f); // | (((unsigned short)packet_buffer[16] << 3) & 0x80);
  Debug_printf("\r\nTarget Device: %02x", id());
  // add a switch case statement for ALL THE STATUSESESESESS
  if (status_code == 0x03)
  { // if statcode=3, then status with device info block
    Debug_printf("\r\n******** Sending DIB! ********");
    send_status_dib_reply_packet();
    // print_packet ((unsigned char*) packet_buffer,get_packet_length());
    // fnSystem.delay(50);
  }
  else
  { // else just return device status
    Debug_printf("\r\nSending Status");
    send_status_reply_packet();
  }
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
void iwmBus::service()
{
  // check on the diskii status
  switch (iwm_drive_enabled())
  {
  case iwm_enable_state_t::off:
    // diskii_xface.disable_output();
    diskii_xface.spi_end();
    // smartport.iwm_ack_set();
    break;
  case iwm_enable_state_t::on:
#ifdef DEBUG
    new_track = theFuji._fnDisk2s[diskii_xface.iwm_enable_states()-1].get_track_pos();
    if (old_track != new_track)
  {
      Debug_printf("\ntrk pos %03d on d%d", new_track, diskii_xface.iwm_enable_states());
      old_track = new_track;
  }
#endif
    // smartport.iwm_ack_clr();  - need to deal with write protect
    diskii_xface.enable_output();
    if (theFuji._fnDisk2s[diskii_xface.iwm_enable_states()-1].device_active)
    {
      // Debug_printf("%d ", isrctr);
      diskii_xface.iwm_queue_track_spi();
    }
    return;
  }

  // read phase lines to check for smartport reset or enable
  switch (iwm_phases())
  {
  case iwm_phases_t::idle:
    break;
  case iwm_phases_t::reset:
    Debug_printf(("\r\nReset"));
 
    // clear all the device addresses
    for (auto devicep : _daisyChain)
      devicep->_devnum = 0;

    while (iwm_phases() == iwm_phases_t::reset)
      portYIELD(); // no timeout needed because the IWM must eventually clear reset.
    // even if it doesn't, we would just come back to here, so might as
    // well wait until reset clears.

    Debug_printf(("\r\nReset Cleared"));
    break;
  case iwm_phases_t::enable:
    // expect a command packet
    // portDISABLE_INTERRUPTS();
    // if(smartport.iwm_read_packet_spi(command_packet.data, COMMAND_PACKET_LEN))
    // {
    //   portENABLE_INTERRUPTS();
    //   return;
    // }
    // should not ACK unless we know this is our Command
    
    if (!sp_command_mode)
    {
      //iwm_ack_deassert(); // go hi-Z
      return;
    }
    /** instead of iwm_phases, create an iwm_state() and switch on that. States would be:
     * IDLE
     * RESET
     * RESET CLEARED
     * ENABLED
     * REQ ASSERTED
     * REQ DEASSERTED
     * pretty much what i'm doing above - in fact don't need to change the function calls here,
     * just need to change what's in the functions
     * */
    
    if (command_packet.command == 0x85)
    {
      // iwm_ack_assert(); // includes waiting for spi read transaction to finish
      // portENABLE_INTERRUPTS();

      // wait for REQ to go low
      if (iwm_req_deassert_timeout(50000))
      {
        //iwm_ack_deassert(); // go hi-Z
        return;
      }
      // if (smartport.req_wait_for_falling_timeout(50000))
      //   return;


#ifdef DEBUG
      print_packet(command_packet.data);
      Debug_printf("\r\nhandling init command");
#endif
      if (verify_cmdpkt_checksum())
      {
        Debug_printf("\r\nBAD CHECKSUM!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        Debug_printf("\r\ndo init anyway");
      }      // to do - checksum verification? How to respond?
      handle_init();
    }
    else
    {
      for (auto devicep : _daisyChain)
      {
        if (command_packet.dest == devicep->_devnum && devicep->device_active == true)
        {
          // iwm_ack_assert(); // includes waiting for spi read transaction to finish
          // portENABLE_INTERRUPTS();
          // wait for REQ to go low
          if (iwm_req_deassert_timeout(50000))
          {
            //iwm_ack_deassert(); // go hi-Z
            return;
          }
          // need to take time here to service other ESP processes so they can catch up
          taskYIELD(); // Allow other tasks to run
          print_packet(command_packet.data);
          
          _activeDev = devicep;
          // handle command
          if (verify_cmdpkt_checksum())
          {
            Debug_printf("\r\nBAD CHECKSUM!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
            _activeDev->iwm_return_ioerror();
          }
          else
          {
            memset(command.decoded, 0, sizeof(command.decoded));
            smartport.decode_data_packet(command_packet.data, command.decoded);
            print_packet(command.decoded,9);
            _activeDev->process(command);
          }
        }
      }
    }
    sp_command_mode = false;
    iwm_ack_deassert(); // go hi-Z
  } // switch (phasestate)
}

iwm_enable_state_t iwmBus::iwm_drive_enabled()
{
  uint8_t phases = smartport.iwm_phase_vector();
  uint8_t newstate = diskii_xface.iwm_enable_states();
  
  if (!(phases & 0b1000) && !(phases & 0b0010)) //SP bus not enabled
  {
    // Debug_printf("\ndisk ii enable states: %02x",newstate);
    return (newstate != 0) ? iwm_enable_state_t::on : iwm_enable_state_t::off;
  }
  else
  {
    return iwm_enable_state_t::off;
  }
}

void iwmBus::handle_init()
{
  uint8_t status = 0;
  iwmDevice* pDevice = nullptr;

  fnLedManager.set(LED_BUS, true);

  // iwm_rddata_clr();
  
  // to do - get the next device in the daisy chain and assign ID
  for (auto it = _daisyChain.begin(); it != _daisyChain.end(); ++it)
  {
    // tell the Fuji it's device no.
    if (it == _daisyChain.begin())
    {
      theFuji._devnum = command_packet.dest;
    }
    // assign dev numbers
    pDevice = (*it);

    if (pDevice->device_active == false)
      continue;
    
    if (pDevice->id() == 0)
    {
      pDevice->_devnum = command_packet.dest; // assign address
      if (++it == _daisyChain.end())
        status = 0xff; // end of the line, so status=non zero - to do: check GPIO for another device in the physical daisy chain
      Debug_printf("\r\nSending INIT Response Packet...");
      pDevice->send_init_reply_packet(command_packet.dest, status);

      //smartport.iwm_send_packet_spi((uint8_t *)pDevice->packet_buffer); // timeout error return is not handled here (yet?)

      // print_packet ((uint8_t*) packet_buffer,get_packet_length());

      Debug_printf(("\r\nDrive: %02x\r\n"), pDevice->id());
      fnLedManager.set(LED_BUS, false);
      return;
    }
  }

  fnLedManager.set(LED_BUS, false);

}

// Add device to SIO bus
void iwmBus::addDevice(iwmDevice *pDevice, iwm_fujinet_type_t deviceType)
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
      // todo: work out how to assign different network devices - idea:
      // include a number in the DIB name, e.g., "NETWORK 1"
      // and extract that number from the DIB and use it as the index
      //_netDev[device_id - SIO_DEVICEID_FN_NETWORK] = (iwmNetwork *)pDevice;
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
void iwmBus::remDevice(iwmDevice *p)
{
    _daisyChain.remove(p);
}

// Should avoid using this as it requires counting through the list
int iwmBus::numDevices()
{
    int i = 0;
    __BEGIN_IGNORE_UNUSEDVARS
    for (auto devicep : _daisyChain)
        i++;
    return i;
    __END_IGNORE_UNUSEDVARS
}

void iwmBus::changeDeviceId(iwmDevice *p, int device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep == p)
            devicep->_devnum = device_id;
    }
}

iwmDevice *iwmBus::deviceById(int device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep->_devnum == device_id)
            return devicep;
    }
    return nullptr;
}

void iwmBus::enableDevice(uint8_t device_id)
{
    iwmDevice *p = deviceById(device_id);
    p->device_active = true;
}

void iwmBus::disableDevice(uint8_t device_id)
{
    iwmDevice *p = deviceById(device_id);
    p->device_active = false;
}

// Give devices an opportunity to clean up before a reboot
void iwmBus::shutdown()
{
    shuttingDown = true;

    for (auto devicep : _daisyChain)
    {
        Debug_printf("Shutting down device %02x\n",devicep->id());
        devicep->shutdown();
    }
    Debug_printf("All devices shut down.\n");
}

iwmBus IWM; // global smartport bus variable

#endif /* BUILD_APPLE */
