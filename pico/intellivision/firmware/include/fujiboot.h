#ifndef FUJIBOOT_H
#define FUJIBOOT_H

// First cart.ROM[] word a network push may write: the console keeps running
// CONFIG (words 0x0000-0x33F4) during the push. fujiboot.c static-asserts it.
#define FUJI_STAGE_BASE 0x3500u

// Boots into the FujiNet CONFIG program (fujiconfigrom.h), in place of
// Minty's launcher.
void RunFujiConfig(void);

// rebuild CONFIG's map + mailbox ident; recovery after a failed commit
void fuji_config_map(void);

#endif /* FUJIBOOT_H */
