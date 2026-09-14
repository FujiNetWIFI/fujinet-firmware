#ifdef BUILD_MAC
#include "mac.h"
#include "mac_ll.h"
#include "macFuji.h"
#include "fnSystem.h"

#include "../../include/debug.h"

void systemBus::setup(void)
{
  Debug_printf("\r\nMAC FujiNet based on FujiApple\r\n");

  _serial.begin(ChannelConfig()
                    .deviceID(FN_UART_BUS)
                    .baud(_mac_baud_rate));

  // GPIO needs to read Head Select (SEL)
  floppy_ll.setup_gpio();
  Debug_printf("\r\nGPIO configured");

  floppy_ll.setup_rmt();
  Debug_printf("\r\nRMT configured for Floppy Output");
}

/**
 * 699-0452-A Double Sided floppy requirement document
 *
 * The host system can send four commands: /DIRTN,/STEP,
 * /MOTORON and EJECT. To send one of the control commands to the
 * drive, set CA2 to the value (a zero or a one) to which the host
 * system wishes the command to be set, and then set CAO, CA 1, and
 * SEL to the value which selects the desired command. Finally, bring
 * LSTRB first high and then low.
 *
 *               SEL CA2 CA1 CA0   value
 * /DIRTN         0   0   0   0     0     increase track number
 * /DIRTN         0   1   0   0     4     decrease track number
 * /STEP          0   0   0   1     1     step the head in the direction
 * /STEP          0   1   0   1     5     probably not used
 * /MOTORON       0   0   1   0     2     turn motor on
 * /MOTORON       0   1   1   0     6     turn motor off
 * EJECT          0   0   1   1     3     probably not used
 * EJECT          0   1   1   1     7     eject the disk
 */

/**
 * for DCD (HD20) the protocol is different.
 * need to read and write blocks
 * would be nice to fetch the image name so it can be displayed on the mac
 *
 * block numbers are 3 bytes, 512 bytes/block - 8GB addressable, but MAC OS limites to 2 GB
 *
*/

size_t systemBus::read_exact(void *buffer, size_t length, unsigned timeout_ms)
{
  uint8_t *p = (uint8_t *)buffer;
  size_t got = 0;
  unsigned long start = fnSystem.millis();

  while (got < length)
  {
    size_t n = _serial.read(p + got, length - got);
    if (n > 0)
    {
      got += n;
      start = fnSystem.millis();
      continue;
    }
    if (fnSystem.millis() - start > timeout_ms)
    {
      Debug_printf("\r\nMAC bus: read_exact timeout, got %u of %u bytes", (unsigned)got, (unsigned)length);
      break;
    }
    vTaskDelay(1);
  }
  return got;
}

static inline macFloppy &floppy_dev()
{
  return theFuji->get_disk(MAC_FLOPPY_SLOT)->disk_dev;
}

void systemBus::handle_floppy_command(int c)
{
  switch (c - '0')
  {
  case 0:
    // set direction to increase track number
    Debug_printf("%c", 'I');
    floppy_dev().set_dir(+1);
    break;
  case 4:
    // set direction to decrease track number
    Debug_printf("%c", 'D');
    floppy_dev().set_dir(-1);
    break;
  case 1:
    // step the head
    Debug_printf("%c", 'S');
    {
      t0 = fnSystem.micros();
      track_not_copied = true;
      int track_position = floppy_dev().step();
      if (track_position < 0)
      {
        write((uint8_t)'N');
      }
      else
      {
        write((uint8_t)(track_position | 128)); // send the track position(/2) back
      }
    }
    break;
  case 2:
    // turn motor on
    Debug_printf("\nMotor ON");
    floppy_ll.start();
    write((uint8_t)'M');
    break;
  case 6:
    // turn motor off
    Debug_printf("\nMotor OFF");
    floppy_ll.stop();
    write((uint8_t)'F');
    break;
  case 7:
    // eject
    Debug_printf("\neject - unmounting");
    floppy_ll.stop();
    floppy_dev().unmount();
    write((uint8_t)'E');
    break;
  default:
    write((uint8_t)'X');
    break;
  }
}

void systemBus::handle_dcd_command(int c)
{
  switch (c)
  {
  case 'A':
  case 'B':
  case 'C':
  case 'D':
  case 'E':
    if (_active_DCD_disk != c - 'A')
      Debug_printf("\nactive disk %d", c - 'A');
    _active_DCD_disk = c - 'A'; // 0, 1, 2, 3
    break;
  case 'R':
  case 'T':
  case 'W':
    if (_active_DCD_disk >= 0 && _active_DCD_disk < MAC_DCD_SLOTS)
      theFuji->get_disk(_active_DCD_disk)->disk_dev.process((mac_cmd_t)c);
    else
      Debug_printf("\nDCD command %c for invalid disk %d", c, _active_DCD_disk);
    break;
  default:
    break;
  }
}

void systemBus::service(void)
{
  // todo - figure out two floppies - either on RP2040 or ESP32 side. Use the two enable lines - get_disks(0 or 1)
  if (available())
  {
    int c = read();
    if (c <= 0)
      return;
    else if (c < 'A') // floppy
      handle_floppy_command(c);
    else // DCD
      handle_dcd_command(c);
  }
  if (track_not_copied && stepper_timeout())
  {
    floppy_dev().update_track_buffers();
    track_not_copied = false;
    write((uint8_t)'S');
  }
}

char systemBus::num_dcd_mounts()
{
  // find maximum consecutively occupied disk slot
  char d = 0;
  while (_mounted_dcd_disks & (1 << d))
    d++;
  return d;
}

void systemBus::send_dcd_count()
{
  char d = num_dcd_mounts();
  Debug_printf("\r\nMAC bus: %d DCD drive(s) in chain", d);
  write((uint8_t)'h'); // harddisk
  write((uint8_t)d);   // number of DCD's in a contiguous daisy chain
}

void systemBus::add_dcd_mount(char c)
{
  _mounted_dcd_disks |= 1 << (c - '0');
  send_dcd_count();
}

void systemBus::rem_dcd_mount(char c)
{
  _mounted_dcd_disks &= ~(1 << (c - '0'));
  send_dcd_count();
}

bool systemBus::stepper_timeout()
{
  unsigned long tn = fnSystem.micros();
  return ((tn - t0) > 2000);
}

void systemBus::shutdown(void)
{
  shuttingDown = true;

  for (auto devicep : _daisyChain)
  {
    Debug_printf("Shutting down device %02x\n", devicep->id());
    devicep->shutdown();
  }
  Debug_printf("All devices shut down.\n");
}

#endif // BUILD_MAC
