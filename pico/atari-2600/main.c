#include "pico/stdlib.h"
#include "rom.h"

#define PINROMADDR  6
#define PINROMDATA 18
#define ENABLE      2
#define ADDRWIDTH  12
#define DATAWIDTH   8

int main()
{
  const uint32_t addrmask = 0xfff << PINROMADDR;
  const uint32_t datamask = 0xff << PINROMDATA;
  const uint32_t enablemask = 1 << ENABLE;
  uint32_t allpins, addr, bank = 0;

  gpio_init_mask(addrmask | datamask | enablemask);
  gpio_set_dir_all_bits(0);

  for (int i = 0; i < DATAWIDTH; i++)
    gpio_disable_pulls(PINROMDATA + i);

  for (int i = 0; i < ADDRWIDTH; i++)
    gpio_disable_pulls(PINROMADDR + i);

  gpio_set_pulls(ENABLE, true, false);

  while (true)
  {
    allpins = gpio_get_all();
    if ((allpins & enablemask) == 0)
    {
      allpins = gpio_get_all();
      addr = (allpins & addrmask) >> PINROMADDR;
      switch (addr)
      {
      case 0xFF8:
        bank = 0;
        break;
      case 0xFF9:
        bank = 0x1000;
        break;
      default:
        gpio_set_dir_out_masked(datamask);
        gpio_put_masked(datamask, ((uint32_t)rom[addr | bank]) << PINROMDATA);
        break;
      }
      
    }
    else
    {
      gpio_set_dir_all_bits(0);
    }
  }
}
