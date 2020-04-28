#include <stdio.h>

#define SUB 32

int main()
{
  FILE *f;
  int i, j, k;

  f = fopen("grid.txt", "w");
  fprintf(f, " ");
  for (j = 0; j < 16; j++)
  {
    fprintf(f, "   %x ", j);
  }
  fprintf(f, "\n\n");
  for (i = 2; i < 16; i++)
  {
    fprintf(f, "%x", i);
    for (j = 0; j < 16; j++)
    {
      fprintf(f, "   ");
      unsigned char b = i * 16 + j;
      //prestige elite
      if ( (b > 126 && b<161)  || b==173 || b==183)
        b = SUB;
      fputc(b, f);
      fprintf(f, " ");
    }
    fprintf(f, "\n ");
    for (j = 0; j < 16; j++)
    {
      unsigned char b = i * 16 + j;
      fprintf(f, "  %03u", b);
    }
    fprintf(f, "\n\n");
  }
  fclose(f);
  return 0;
} 
