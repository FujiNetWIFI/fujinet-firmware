// test_jlpflash -- JLP flash sector and range checks (include/jlpflash.h).

#include <stdio.h>

#include "jlpflash.h"

static int fails;

static void check(const char *what, int got, int want)
{
   if (got != want) {
      printf("FAIL %s: got %d, want %d\n", what, got, want);
      fails++;
   }
}

int main(void)
{
   // Erasing any row erases its whole 8-row sector.
   check("row 0 sector", jlp_sector_first_row(0), 0);
   check("row 7 sector", jlp_sector_first_row(7), 0);
   check("row 8 sector", jlp_sector_first_row(8), 8);
   check("row 15 sector", jlp_sector_first_row(15), 8);
   check("row 17 sector", jlp_sector_first_row(17), 16);

   check("row -1", jlp_row_ok(-1, 48), 0);
   check("row 0", jlp_row_ok(0, 48), 1);
   check("row 47", jlp_row_ok(47, 48), 1);
   check("row 48", jlp_row_ok(48, 48), 0);
   check("no flash", jlp_row_ok(0, 0), 0);

   // No mailbox: any 96 words of $8040-$9F7F.
   check("$8040", jlp_span_ok(0x8040, 1, 0), 1);
   check("$803F", jlp_span_ok(0x803F, 1, 0), 0);
   check("$9F20 ends $9F7F", jlp_span_ok(0x9F20, 1, 0), 1);
   check("$9F21 ends $9F80", jlp_span_ok(0x9F21, 1, 0), 0);
   check("$9F40 into accelerators", jlp_span_ok(0x9F40, 1, 0), 0);
   check("$9C00", jlp_span_ok(0x9C00, 1, 0), 1);

   // Mailbox at $9C00-$9F3F.
   check("$8040, mailbox", jlp_span_ok(0x8040, 0x9C00, 0x9F3F), 1);
   check("$9BA0 ends $9BFF", jlp_span_ok(0x9BA0, 0x9C00, 0x9F3F), 1);
   check("$9BA1 ends $9C00", jlp_span_ok(0x9BA1, 0x9C00, 0x9F3F), 0);
   check("$9D00 inside", jlp_span_ok(0x9D00, 0x9C00, 0x9F3F), 0);
   check("$9F3F", jlp_span_ok(0x9F3F, 0x9C00, 0x9F3F), 0);

   if (fails)
      return 1;
   printf("test_jlpflash: ok\n");
   return 0;
}
