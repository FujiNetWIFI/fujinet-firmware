/* fujilib.c -- the console's half of the FujiNet mailbox. */

#include "fujilib.h"

static unsigned char nparam;

static void fn_regwr(unsigned char reg, unsigned char val)
{
    *(volatile unsigned char *)(FN_REGSEL + reg) = val;
}

bool fn_present(void)
{
    return FN_MAGIC0 == 'F' && FN_MAGIC1 == 'N';
}

void fn_start(unsigned char device, unsigned char command)
{
    nparam = 0;
    fn_regwr(FNR_DATA_RST, 0);
    fn_regwr(FNR_DEVICE, device);
    fn_regwr(FNR_CMD, command);
    fn_regwr(FNR_NPARAM, 0);
}

void fn_tx(unsigned char b)
{
    FN_TXPAGE = b;
}

void fn_tx_bytes(const unsigned char *p, unsigned int n)
{
    while (n--)
        FN_TXPAGE = *p++;
}

void fn_tx_padded(const char *s, unsigned int total)
{
    unsigned int n = 0;

    while (*s && n < total) {
        FN_TXPAGE = (unsigned char)*s++;
        n++;
    }
    while (n++ < total)
        FN_TXPAGE = 0;
}

void fn_tx_path(const char *prefix, volatile unsigned char *name, unsigned int total)
{
    unsigned int n = 0;

    while (*prefix && n < total) {
        FN_TXPAGE = (unsigned char)*prefix++;
        n++;
    }
    while (n < total) {
        unsigned char c = *name++;

        if (c == 0)
            break;
        FN_TXPAGE = c;
        n++;
    }
    while (n++ < total)
        FN_TXPAGE = 0;
}

void fn_param8(unsigned char v)
{
    FN_TXPAGE = 1;
    FN_TXPAGE = v;
    fn_regwr(FNR_NPARAM, ++nparam);
}

void fn_param16(unsigned int v)
{
    FN_TXPAGE = 2;
    FN_TXPAGE = (unsigned char)v;
    FN_TXPAGE = (unsigned char)(v >> 8);
    fn_regwr(FNR_NPARAM, ++nparam);
}

unsigned char fn_commit(void)
{
    unsigned char want = (unsigned char)(FN_ACKSEQ + 1);
    unsigned int outer, inner;

    if (want == 0)
        want = 1;               /* 0 means "never used" */
    nparam = 0;
    fn_regwr(FNR_SEQ, want);

    /* Longer than the cart's own budget (60 s for MOUNT_IMAGE), so a real
     * timeout is reported as the cart's error code rather than ours. */
    for (outer = 0; outer < 4000u; outer++)
        for (inner = 0; inner < 250u; inner++)
            if (FN_ACKSEQ == want)
                return FN_ERRCODE;
    return FN_EWAIT;
}

bool fn_acked(void)
{
    return FN_REPLYCMD == FN_ACK;
}

unsigned int fn_reply_len(void)
{
    return (unsigned int)FN_RXLEN_LO | ((unsigned int)FN_RXLEN_HI << 8);
}

void fn_boot(void)
{
    fn_regwr(FNR_BOOTLOCK, FN_BOOTLOCK_MAGIC);
#asm
    di
    jp  0xB800
#endasm
}

void fn_exit_to_config(void)
{
#asm
    di
    jp  0xB803
#endasm
}
