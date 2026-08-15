#ifndef FUJIBOOT_H
#define FUJIBOOT_H

// Boots the Intellivision straight into the FujiNet CONFIG program
// (fujiconfigrom.h) -- the only boot ROM this firmware runs. Replaces
// Minty's own RunLauncher() (removed; see PROVENANCE.md/README.md).
void RunFujiConfig(void);

// rebuild CONFIG's map + mailbox ident; recovery after a failed commit
void fuji_config_map(void);

#endif /* FUJIBOOT_H */
