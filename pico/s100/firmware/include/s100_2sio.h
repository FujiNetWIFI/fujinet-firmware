/* s100_2sio.h -- an 88-2SIO-compatible pair of 6850 ACIAs, polled only.
 * Port +0/+1 is channel A (the FujiNet modem), +2/+3 channel B (the printer). */
#ifndef S100_2SIO_H
#define S100_2SIO_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "s100_ring.h"

#define SIO_CH_MODEM   0
#define SIO_CH_PRINTER 1
#define SIO_RING       1024

/* 6850 status */
#define SIO_RDRF 0x01
#define SIO_TDRE 0x02
#define SIO_DCD  0x04
#define SIO_CTS  0x08
#define SIO_OVRN 0x20
#define SIO_IRQ  0x80

/* 6850 control */
#define SIO_CR_RESET 0x03
#define SIO_CR_RIE   0x80
#define SIO_CR_TXMSK 0x60
#define SIO_CR_TIE   0x20

typedef struct {
    uint8_t ctrl;
    uint8_t last;
    ring8_t rx; /* link side -> bus side */
    ring8_t tx; /* bus side -> link side */
    _Atomic uint8_t ovrn;
    uint8_t rxbuf[SIO_RING];
    uint8_t txbuf[SIO_RING];
} sio_chan_t;

typedef struct {
    sio_chan_t ch[2];
} sio2_t;

void sio2_init(sio2_t *s);
uint8_t sio2_in(sio2_t *s, uint8_t port);
void sio2_out(sio2_t *s, uint8_t port, uint8_t v);

/* link side */
void sio2_rx(sio2_t *s, int ch, const uint8_t *buf, uint32_t n);

#endif /* S100_2SIO_H */
