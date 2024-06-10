#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
// #include "pico/util/queue.h"
// #include "hardware/uart.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

#include "rom.h"

// define how the GPIOs are connected to the CoCo Bus
// these are used in main.c and cococart.pio
#define PINROMADDR  8
#define PINROMDATA  0
#define RWPIN      24
//      CLKPIN     25 - defined in cococart.pio
//		  CTSPIN	   26 - defined in cococart.pio	
#define ADDRWIDTH  16 // 64k address space
// #define ROMWIDTH   14 // 16k cart rom space
// #define DATAWIDTH   8

#include "cococart.pio.h"
#include "uart_rx.pio.h"
#include "uart_tx.pio.h"

PIO pioblk_ro = pio0;
#define SM_ADDR 0
#define SM_READ 1
#define SM_UART_RX 2

/**
 * put the output SM's in PIO1: data write and rom emulator
 *
 * from Section 3.5.6.1 in RP2040 datasheet:
 * For each GPIO, PIO collates the writes from all four state machines,
 * and applies the write from the __highest-numbered__ state machine.
 * This occurs separately for output levels and output values —
 * it is possible for a state machine to change both the level and
 * direction of the same pin on the same cycle (e.g. via simultaneous
 * SET and side-set), or for one state  machine to change a GPIO’s
 * direction while another changes that GPIO’s level.
 */
PIO pioblk_rw = pio1;
#define SM_ROM 3
#define SM_WRITE 2
#define SM_UART_TX 0

uint8_t ccc, fff;

#define SERIAL_BAUD 38400
#define PIO_RX_PIN 28 //A2
#define PIO_TX_PIN 29 //A3
void setup_pio_uart()
{
  int offset = pio_add_program(pioblk_ro, &uart_rx_program);
  uart_rx_program_init(pioblk_ro, SM_UART_RX, offset, PIO_RX_PIN, SERIAL_BAUD);
  printf("uart RX PIO installed at %d\n", offset);
  pio_sm_set_enabled(pioblk_ro, SM_UART_RX, true);

  offset = pio_add_program(pioblk_rw, &uart_tx_program);
  uart_tx_program_init(pioblk_rw, SM_UART_TX, offset, PIO_TX_PIN, SERIAL_BAUD);
  printf("uart TX PIO installed at %d\n", offset);
  pio_sm_set_enabled(pioblk_rw, SM_UART_TX, true);
}

void setup_rom_emulator()
{
	int chan_rom_addr, chan_rom_data;
	dma_channel_config cfg_rom_addr, cfg_rom_data;

	uint offset = pio_add_program(pioblk_rw, &rom_program);
    printf("\nLoaded rom program at %d\n", offset);
    rom_program_init(pioblk_rw, SM_ROM, offset);
    pio_sm_put(pioblk_rw, SM_ROM, (uintptr_t)rom >> ROMWIDTH);
    pio_sm_exec_wait_blocking(pioblk_rw, SM_ROM, pio_encode_pull(false, true));
    pio_sm_exec_wait_blocking(pioblk_rw, SM_ROM, pio_encode_mov(pio_y, pio_osr));
    pio_sm_exec_wait_blocking(pioblk_rw, SM_ROM, pio_encode_out(pio_null, 1)); 
    pio_sm_set_enabled(pioblk_rw, SM_ROM, true);

    chan_rom_addr = dma_claim_unused_channel(true);
    cfg_rom_addr = dma_channel_get_default_config(chan_rom_addr);

    chan_rom_data = dma_claim_unused_channel(true);
    cfg_rom_data = dma_channel_get_default_config(chan_rom_data);
 
    channel_config_set_read_increment(&cfg_rom_data,false);
    channel_config_set_write_increment(&cfg_rom_data,false);
    channel_config_set_dreq(&cfg_rom_data, pio_get_dreq(pioblk_rw, SM_ROM, true)); // ROM PIO
    channel_config_set_chain_to(&cfg_rom_data, chan_rom_addr);
    channel_config_set_transfer_data_size(&cfg_rom_data, DMA_SIZE_8);
    channel_config_set_irq_quiet(&cfg_rom_data, true);
    channel_config_set_enable(&cfg_rom_data, true);
    dma_channel_configure(
        chan_rom_data,                          // Channel to be configured
        &cfg_rom_data,                        // The configuration we just created
        &pioblk_rw->txf[SM_ROM],                   // The initial write address
        rom,                      // The initial read address
        1,                                  // Number of transfers; in this case each is 1 byte.
        false                               // do not Start immediately.      
      );

    channel_config_set_read_increment(&cfg_rom_addr,false);
    channel_config_set_write_increment(&cfg_rom_addr,false);
    channel_config_set_dreq(&cfg_rom_addr, pio_get_dreq(pioblk_rw, SM_ROM, false)); // ROM PIO
    channel_config_set_chain_to(&cfg_rom_addr, chan_rom_data);
    channel_config_set_transfer_data_size(&cfg_rom_addr, DMA_SIZE_32);
    channel_config_set_irq_quiet(&cfg_rom_addr, true);
    channel_config_set_enable(&cfg_rom_addr, true);
    dma_channel_configure(
        chan_rom_addr,                          // Channel to be configured
        &cfg_rom_addr,                        // The configuration we just created
        &dma_channel_hw_addr(chan_rom_data)->read_addr,   // The initial write address
        &pioblk_rw->rxf[SM_ROM],            // The initial read address
        1,                                  // Number of transfers; in this case each is 1 byte.
        true                               // do Start immediately.      
      );
}


void initio()
{
  const uint32_t addrmask = 0xffff << PINROMADDR;
  const uint32_t datamask = 0xff << PINROMDATA;
  const uint32_t ctrlmask = (1 << CLKPIN) | (1 << CTSPIN) | (1 << RWPIN);
  
  gpio_init_mask(addrmask | datamask | ctrlmask);
  gpio_set_dir_all_bits(0);

  for (int i = 0; i < DATAWIDTH; i++)
    gpio_disable_pulls(PINROMDATA + i);

  for (int i = 0; i < ADDRWIDTH; i++)
    gpio_disable_pulls(PINROMADDR + i);

  gpio_set_pulls(CTSPIN, true, false);
  gpio_disable_pulls(CLKPIN);
  gpio_disable_pulls(RWPIN);
  // gpio_set_pulls(BUGPIN, false, true);

}

/* 

  Becker port   http://www.davebiz.com/wiki/CoCo3FPGA#Becker_port
  
  The "Becker" port is a simple interface used in the CoCo3FPGA project 
  (and some CoCo emulators) to allow high speed I/O between the CoCo3FPGA 
  and the DriveWire server. The interface uses 2 addresses, one for status 
  and one for the actual I/O. 
  
  The read status port is &HFF41 
  The only bit used out of bit 0 to bit 7 is bit #1 
  If there is data to read then bit #1 will be set to 1 
  
  The read/write port is &HFF42 
  When reading you must make sure you only read when data is present by the status bit. 
  As far as writing you just write the data to the port. 
*/ 

void setup_becker_port()
{
  // install the PIO that captures CoCo access to $FFxx
	uint offset = pio_add_program(pioblk_ro, &cocoaddr_program);
	printf("cocoaddr PIO installed at %d\n", offset);
	cocoaddr_program_init(pioblk_ro, SM_ADDR, offset);
	pio_sm_set_enabled(pioblk_ro, SM_ADDR, true);

  // install PIO that reads from the CoCo data bus
  offset = pio_add_program(pioblk_ro, &dataread_program);
  printf("dataread PIO installed at %d\n", offset);
  dataread_program_init(pioblk_ro, SM_READ, offset);
	pio_sm_set_enabled(pioblk_ro, SM_READ, true);

  // install PIO that writes to the CoCo data bus
  offset = pio_add_program(pioblk_rw, &datawrite_program);
  printf("datawrite PIO installed at %d\n", offset);
  datawrite_program_init(pioblk_rw, SM_WRITE, offset);
  pio_sm_set_enabled(pioblk_rw, SM_WRITE, true);
}

static bool becker_data_available = false;
static inline uint8_t becker_get_status()
{
  return becker_data_available ? 0b10 : 0;
}

static inline void becker_set_status(bool s)
{
  becker_data_available = s;
}

static inline uint8_t becker_get_char()
{
  becker_set_status(false);
  return ccc;
}

static inline uint8_t becker_put_char(uint8_t c)
{
  uart_tx_program_putc(pioblk_rw, SM_UART_TX, c);
}

void __time_critical_func(cococart)()
{
  // need a ring buffer for the output data. set the status flag value based on head and tail pointers == or !=

	while (true)
	{

		uint32_t addr = pio_sm_get_blocking(pioblk_ro, SM_ADDR);

		if (gpio_get(RWPIN)) // coco MC6809 is in read mode
			switch (addr)
			{
			case 0x41:
				// return a byte ...
				pio_sm_put(pioblk_rw, SM_WRITE, becker_get_status());
				// printf("read %02x\n", addr);
				break;
			case 0x42:
        pio_sm_put(pioblk_rw, SM_WRITE, becker_get_char());
				// printf("read %02x\n", addr);
				break;
			default:
				break;
			}
	  else          // coco MC6809 is in write mode
		  switch (addr)
		  {
		  case 0x42:
			  // get the byte
			  {
				  pio_sm_put(pioblk_ro, SM_READ, 0);
				  uint32_t b = pio_sm_get_blocking(pioblk_ro, SM_READ);
				  becker_put_char(b);
          // printf("write %02x = %03d\n", addr, b);
			  }
			  break;
		  default:
			  break;
      }
	}
}

int main()
{
	
  multicore_launch_core1(cococart); // launch the Cart ROM emulator
	
  stdio_init_all(); // enable USB serial I/O as specified in cmakelists
	initio();
	busy_wait_ms(2000); // wait for minicom to connect so I can see message
	printf("\nwelcome to cococart\n");

  setup_pio_uart();
  setup_becker_port();
	setup_rom_emulator();

	// talk to serial fujinet
	while (true)
	{
		if (becker_get_status() == 0)
		{
			ccc = uart_rx_program_getc(pioblk_ro, SM_UART_RX);
			becker_set_status(true);
      printf("%02x",ccc);
	  }
	}

}

// OBE but don't want to lose this thought in case it's needed later:
	// multicore using queue - old method

	// queue_entry_t CoCoIO;
	// queue_init(&push_queue, sizeof(queue_entry_t), 512);

  	// multicore_launch_core1(cococart);

	// while (true)
	// {
	// 	queue_remove_blocking(&push_queue, &CoCoIO);
	// 	if (CoCoIO.addr == 0x50) 
	// 		printf("%d ", CoCoIO.data);
  	// }

//     void __not_in_flash_func(cococart)()
// //void cococart()
// {
//   const uint32_t addrmask = 0x1fff << PINROMADDR;
//   const uint32_t datamask = 0xff << PINROMDATA;
//   const uint32_t enablemask = 1 << CTSPIN;
//   uint32_t allpins, addr, bank = 0;

//   gpio_init_mask(addrmask | datamask | enablemask);
//   gpio_set_dir_all_bits(0);

//   for (int i = 0; i < DATAWIDTH; i++)
//     gpio_disable_pulls(PINROMDATA + i);

//   for (int i = 0; i < ADDRWIDTH; i++)
//     gpio_disable_pulls(PINROMADDR + i);

//   gpio_disable_pulls(CTSPIN);

//   while (true)
//   {
//     allpins = gpio_get_all();
//     if ((allpins & enablemask) == 0)
//     {
//       allpins = gpio_get_all();
//       addr = (allpins & addrmask) >> PINROMADDR;
//         gpio_set_dir_out_masked(datamask);
//         gpio_put_masked(datamask, ((uint32_t)rom[addr | bank]) << PINROMDATA);
// 	}
//     else
//     {
//       gpio_set_dir_all_bits(0);
//     }
//   }
// }
