// See s100_2sio.h.
#include "s100_2sio.h"

#include <string.h>

void sio2_init(sio2_t *s)
{
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < 2; i++)
    {
        ring8_init(&s->ch[i].rx, s->ch[i].rxbuf, SIO_RING);
        ring8_init(&s->ch[i].tx, s->ch[i].txbuf, SIO_RING);
    }
}

static uint8_t status(sio_chan_t *c)
{
    uint8_t st = 0;
    if (ring8_count(&c->rx))
        st |= SIO_RDRF;
    if (ring8_space(&c->tx))
        st |= SIO_TDRE;
    if (atomic_load_explicit(&c->ovrn, memory_order_acquire))
        st |= SIO_OVRN;
    if (((c->ctrl & SIO_CR_RIE) && (st & (SIO_RDRF | SIO_OVRN))) ||
        ((c->ctrl & SIO_CR_TXMSK) == SIO_CR_TIE && (st & SIO_TDRE)))
        st |= SIO_IRQ;
    return st;
}

uint8_t sio2_in(sio2_t *s, uint8_t port)
{
    sio_chan_t *c = &s->ch[(port >> 1) & 1];
    if ((port & 1) == 0)
        return status(c);
    // A 6850 clears overrun when the data register is read.
    atomic_store_explicit(&c->ovrn, 0, memory_order_release);
    ring8_get(&c->rx, &c->last);
    return c->last;
}

void sio2_out(sio2_t *s, uint8_t port, uint8_t v)
{
    sio_chan_t *c = &s->ch[(port >> 1) & 1];
    if ((port & 1) == 0)
    {
        // Master reset only resets the ACIA; nothing queued is lost.
        c->ctrl = (v & SIO_CR_RESET) == SIO_CR_RESET ? 0 : v;
        return;
    }
    ring8_put(&c->tx, v); // full: dropped, as a 6850 written without TDRE would
}

void sio2_rx(sio2_t *s, int ch, const uint8_t *buf, uint32_t n)
{
    sio_chan_t *c = &s->ch[ch];
    for (uint32_t i = 0; i < n; i++)
        if (!ring8_put(&c->rx, buf[i]))
            atomic_store_explicit(&c->ovrn, 1, memory_order_release);
}
