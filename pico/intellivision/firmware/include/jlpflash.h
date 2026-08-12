
#ifndef JLPFLASH_H
#define JLPFLASH_H

void readFlash(int row, uint16_t addr);
void writeFlash(int row, uint16_t addr);
void eraseFlash(int row);

#endif
