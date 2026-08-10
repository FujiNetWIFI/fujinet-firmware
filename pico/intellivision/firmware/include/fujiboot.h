#ifndef FUJIBOOT_H
#define FUJIBOOT_H

// Boots the Intellivision straight into the FujiNet CONFIG program
// (fujiconfigrom.h) -- the only boot ROM this firmware runs. Replaces
// Minty's own RunLauncher() (removed; see PROVENANCE.md/README.md).
void RunFujiConfig(void);

#endif /* FUJIBOOT_H */
