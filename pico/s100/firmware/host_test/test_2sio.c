// The 6850 pair: status, data, overrun and master reset.
#include "check.h"
#include "s100_2sio.h"

static sio2_t s;

int main(void)
{
    sio2_init(&s);

    // Master reset, then 8N1 /16 with RX interrupts on.
    sio2_out(&s, 0, SIO_CR_RESET);
    sio2_out(&s, 0, 0x95);
    uint8_t st = sio2_in(&s, 0);
    CHECK_EQ(st & SIO_RDRF, 0);
    CHECK(st & SIO_TDRE);
    CHECK_EQ(st & (SIO_DCD | SIO_CTS), 0);
    CHECK_EQ(st & SIO_IRQ, 0);

    // RX from the link side.
    sio2_rx(&s, SIO_CH_MODEM, (const uint8_t *)"\xC0OK", 3);
    st = sio2_in(&s, 0);
    CHECK(st & SIO_RDRF);
    CHECK(st & SIO_IRQ); // RIE set and a byte waiting
    CHECK_EQ(sio2_in(&s, 1), 0xC0);
    CHECK_EQ(sio2_in(&s, 1), 'O');
    CHECK_EQ(sio2_in(&s, 1), 'K');
    CHECK_EQ(sio2_in(&s, 0) & SIO_RDRF, 0);
    CHECK_EQ(sio2_in(&s, 1), 'K'); // empty: the last byte again

    // TX to the link side; channel B is separate.
    sio2_out(&s, 3, 'P');
    sio2_out(&s, 1, 'M');
    uint8_t b = 0;
    CHECK(ring8_get(&s.ch[SIO_CH_PRINTER].tx, &b));
    CHECK_EQ(b, 'P');
    CHECK(ring8_get(&s.ch[SIO_CH_MODEM].tx, &b));
    CHECK_EQ(b, 'M');

    // Overrun when the ring is full, cleared by a data read.
    uint8_t junk[SIO_RING + 4] = {0};
    sio2_rx(&s, SIO_CH_PRINTER, junk, sizeof(junk));
    CHECK(sio2_in(&s, 2) & SIO_OVRN);
    sio2_in(&s, 3);
    CHECK_EQ(sio2_in(&s, 2) & SIO_OVRN, 0);

    // TX interrupt mode reports IRQ while TDRE.
    sio2_out(&s, 2, SIO_CR_TIE);
    CHECK(sio2_in(&s, 2) & SIO_IRQ);

    // TX full: TDRE drops.
    for (int i = 0; i < SIO_RING; i++)
        sio2_out(&s, 1, 'x');
    CHECK_EQ(sio2_in(&s, 0) & SIO_TDRE, 0);

    return check_done("test_2sio");
}
