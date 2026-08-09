/**
 * @brief   Build 64K of address space with ROM at 0x5000
 * @author  Thom Cherryhomes
 * @email   thom dot cherryhomes at gmail dot com
 * @license gpl v. 3, see LICENSE.md for details.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define CART_START_ADDRESS 0x5000

uint16_t ROM[65536]; // The whole address space.

int read_into_rom_space(char *fn)
{
  FILE *fp = fopen(fn,"rb");
  uint8_t col=0;
  uint16_t o=CART_START_ADDRESS;

  if (!fp)
    {
      fprintf(stderr,"Could not open file %s. Aborting.\n",fn);
      return 1;
    }

  while (!feof(fp))
    {
      uint16_t w;

      fread(&w,sizeof(uint16_t),1,fp);

      ROM[o++] = __builtin_bswap16 (w);
    }

  fclose(fp);

  return 0;
}

void output_rom_data(void)
{
  uint8_t col=0;
  uint32_t o=0x0000;

  printf("unsigned short ROM[65536] = {");

  for (o=0x0000;o<0x10000;o++)
    {
      if (!col)
        printf("\n\t");

      printf("0x%04X, ",ROM[o]);

      col++;
      col &= 0x07;
    }

  printf("\n};\n");
}

int build_rom_space(char *fn)
{
  if (read_into_rom_space(fn))
    return 1;

  output_rom_data();

  return 0;
}

int main(int argc, char* argv[])
{
  if (argc < 2)
    {
      printf("%s <rom_binary.rom>\n",argv[0]);
      return 1;
    }
  else
    return build_rom_space(argv[1]);
}
