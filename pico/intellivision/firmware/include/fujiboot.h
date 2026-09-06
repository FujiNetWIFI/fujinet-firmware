#ifndef FUJIBOOT_H
#define FUJIBOOT_H

// First cart.ROM[] word a network push may write. CONFIG's image occupies
// 0x0000-0x3036 (12343 words) and the console is still executing out of it
// while the ESP32 streams a game in, so everything staged during a push has
// to land above it; the committed map biases its ROM offsets by this much.
// 0x3100 is the next 256-word boundary past CONFIG -- fujiboot.c static-
// asserts that against the real _bootrom size, which is where this must be
// re-checked if CONFIG ever grows.
#define FUJI_STAGE_BASE 0x3100u

// Boots the Intellivision straight into the FujiNet CONFIG program
// (fujiconfigrom.h) -- the only boot ROM this firmware runs. Replaces
// Minty's own RunLauncher() (removed; see PROVENANCE.md/README.md).
void RunFujiConfig(void);

// rebuild CONFIG's map + mailbox ident; recovery after a failed commit
void fuji_config_map(void);

#endif /* FUJIBOOT_H */
