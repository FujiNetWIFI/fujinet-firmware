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
//		CTSPIN	   26 - defined in cococart.pio	
#define ADDRWIDTH  16 // 64k address space
// #define ROMWIDTH   14 // 16k cart rom space
// #define DATAWIDTH   8

#include "cococart.pio.h"



PIO pioblk_ro = pio0;
#define SM_WRITE 0

PIO pioblk_rw = pio1;
#define SM_ROM 0

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
    channel_config_set_dreq(&cfg_rom_data, pio_get_dreq(pioblk_rw, SM_ROM, true)); // mux PIO
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
    channel_config_set_dreq(&cfg_rom_addr, pio_get_dreq(pioblk_rw, SM_ROM, false)); // mux PIO
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
        true                               // do not Start immediately.      
      );
}


void initio()
{
  const uint32_t addrmask = 0x1fff << PINROMADDR;
  const uint32_t datamask = 0xff << PINROMDATA;
  const uint32_t enablemask = 1 << CTSPIN;
  uint32_t allpins, addr, bank = 0;

  gpio_init(CLKPIN);
  gpio_init_mask(addrmask | datamask | enablemask);
  gpio_set_dir_all_bits(0);

  for (int i = 0; i < DATAWIDTH; i++)
    gpio_disable_pulls(PINROMDATA + i);

  for (int i = 0; i < ADDRWIDTH; i++)
    gpio_disable_pulls(PINROMADDR + i);

  gpio_set_pulls(CTSPIN, true, false);




}

int main()
{
	// multicore_launch_core1(cococart); // launch the Cart ROM emulator
	stdio_init_all(); // enable USB serial I/O as specified in cmakelists
	initio();
	busy_wait_ms(2000); // wait for minicom to connect so I can see message
	printf("\nwelcome to cococart\n");

	setup_rom_emulator();

	// install the PIO that captures CoCo writes to $FFxx
	uint offset = pio_add_program(pioblk_ro, &cocowrite_program);
	printf("cocowrite PIO installed at %d\n", offset);
	cocowrite_program_init(pioblk_ro, SM_WRITE, offset);
	pio_sm_set_enabled(pioblk_ro, SM_WRITE, true);

	// handle output from the CoCoWrite PIO
	while (true)
	{
		uint32_t w = pio_sm_get_blocking(pio0, 0);
		// w - A0-A7 , D0-D7, ctrl, A8-A15
		uint8_t addr = (w >> 24) & 0xFF;
		if (addr == 0x50)
		{
			uint8_t data = (w >> 16) & 0xFF;
			printf("%03d from %02x\n", data, addr);
		}
	}

}

// don't want to lose this thought in case it's needed later:
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
