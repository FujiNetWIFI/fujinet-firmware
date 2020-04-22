#include <stdio.h>

void main(void)
{
  for (int x=32;x<128;x++)
    {
      for (int y=0;y<254;y++)
	{
	  printf("%c",x);
	}
      printf("\n");
    }
}
