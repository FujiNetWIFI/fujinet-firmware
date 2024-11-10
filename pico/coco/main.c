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
#define NMIPIN     27
#define ADDRWIDTH  16 // 64k address space
// #define ROMWIDTH   14 // 16k cart rom space
// #define DATAWIDTH   8

#include "cococart.pio.h"
#include "uart_rx.pio.h"
#include "uart_tx.pio.h"

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
PIO pioblk_ro = pio0;
#define SM_ADDR 0
#define SM_READ 1
#define SM_UART_RX 2

PIO pioblk_rw = pio1;
#define SM_ROM 3
#define SM_WRITE 2
#define SM_UART_TX 0

uint8_t ccc, fff;

#define SERIAL_BAUD 115200
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
  const uint32_t ctrlmask = (1 << CLKPIN) | (1 << CTSPIN) | (1 << RWPIN) | (1 << NMIPIN);
  
  gpio_init_mask(addrmask | datamask | ctrlmask);
  gpio_set_dir_all_bits(0);

  for (int i = 0; i < DATAWIDTH; i++)
    gpio_disable_pulls(PINROMDATA + i);

  for (int i = 0; i < ADDRWIDTH; i++)
    gpio_disable_pulls(PINROMADDR + i);

  gpio_set_pulls(CTSPIN, true, false);
  gpio_disable_pulls(CLKPIN);
  gpio_disable_pulls(RWPIN);
  gpio_disable_pulls(NMIPIN);
  // gpio_set_pulls(BUGPIN, false, true);

}

/*
ring fifo buffers to feed uart and becker port

array of bytes
pointer to next byte
pointer to end of buffer

push bytes to end of buffer and inc pointer
pull bytes from next byte and inc buffer
don't pull if next byte pointer == end of buffer pointer

need "becker to uart" and "uart to becker" buffers

 */

// brute force the storage and pointers
#define BUFFERSIZE 2048
char fifo_becker_to_uart[BUFFERSIZE];
char fifo_uart_to_becker[BUFFERSIZE];
uint16_t b2u_end, b2u_next, u2b_end, u2b_next;

static inline bool becker_to_uart_available()
{
  return b2u_next != b2u_end;
}

static inline bool uart_to_becker_available()
{
  return u2b_next != u2b_end;
}

static inline void push_byte_from_becker_into_fifo(char c)
{
	fifo_becker_to_uart[b2u_end++] = c;
	b2u_end %= BUFFERSIZE;
	// assert(b2u_end != b2u_next);
}

static inline char pull_byte_from_fifo_send_to_becker()
{
	char c = fifo_uart_to_becker[u2b_next++];
	u2b_next %= BUFFERSIZE;
	return c;
}

static inline void push_byte_from_uart_into_fifo(char c)
{
  fifo_uart_to_becker[u2b_end++] = c;
  u2b_end %= BUFFERSIZE;
  // assert(u2b_end != u2b_next);
}

static inline char pull_byte_from_fifo_send_to_uart()
{
  char c = fifo_becker_to_uart[b2u_next++];
  b2u_next %= BUFFERSIZE;
  return c;
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

// static bool becker_data_available = false;
static inline uint8_t becker_get_status()
{
  return uart_to_becker_available() ? 0b10 : 0;
}

void __time_critical_func(cococart)()
{
  // need a ring buffer for the output data. set the status flag value based on head and tail pointers == or !=

	while (true)
	{

    // assume that the RWPIN is valid and associated with the ADDR state machine output
    // and the DATA output or input is close enough to be synched
		uint32_t addr = pio_sm_get_blocking(pioblk_ro, SM_ADDR);

		if (gpio_get(RWPIN)) // coco MC6809 is in read mode
			switch (addr)
			{
			case 0x41:
				// return status byte ...
				pio_sm_put(pioblk_rw, SM_WRITE, becker_get_status());
				// printf("read %02x\n", addr);
				break;
			case 0x42:
        // return data byte ...
        pio_sm_put(pioblk_rw, SM_WRITE, pull_byte_from_fifo_send_to_becker());
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
				  push_byte_from_becker_into_fifo(b);
          // printf("write %02x = %03d\n", addr, b);
			  }
			  break;
		  default:
			  break;
      }
	}
}

void __time_critical_func(mainloop)()
{
	// talk to serial fujinet
  // might need to spin lock these buffer accesses?
	while (true)
	{
    while (becker_to_uart_available())
    {
      char c = pull_byte_from_fifo_send_to_uart();
      uart_tx_program_putc(pioblk_rw, SM_UART_TX, c);
    }
    if (uart_rx_program_available(pioblk_ro, SM_UART_RX))
		{
			char c = uart_rx_program_getc(pioblk_ro, SM_UART_RX);
      push_byte_from_uart_into_fifo(c);
			// printf("%02x",c);
	  }
	}
}

int main()
{
	
  multicore_launch_core1(cococart); // launch the Cart ROM emulator
	
  stdio_init_all(); // enable USB serial I/O as specified in cmakelists
	initio();
	// busy_wait_ms(2000); // wait for minicom to connect so I can see message
	// printf("\nwelcome to cococart\n");

  setup_pio_uart();

// begin jeff hack to send data to S3
  // char c = ' ' + 1;
  // while(1)
  // {
  //   uart_tx_program_putc(pioblk_rw, SM_UART_TX, c++);
  //   busy_wait_ms(1000);
  // }
// end jeff hack

  setup_becker_port();
	setup_rom_emulator();

  mainloop();

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
