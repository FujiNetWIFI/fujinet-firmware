// Provided under the LGPL v2 license. See the included LICENSE.txt for details.

// sign7800.c
// Encrypts a digital signature for Atari 7800 cartridges.
// 2004-06-20 by Bruce Tomlin

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define version "1.0 of 2004-06-20"

unsigned char acc[256];
unsigned char cart[65536];
const char *progname;		// pointer to argv[0]
int testonly;
int writeback;
char fname[256];
FILE *f;

int starta;
int offseta;
int offsetr;
int sizea;
int sizer0;
int sizer1;
int sizer5;
int sizer7;
unsigned char *modmod;
unsigned char *addmod;
int adacmod0;
unsigned char *cmpmod;
int cpacmod0;
unsigned char *submod;
int sbacmod0;
unsigned char *mdmmod0;
int mdmmod1;

unsigned char acc[256];
unsigned char reg0[128];
unsigned char reg1[128];
unsigned char reg2[128];
unsigned char reg3[128];
unsigned char reg4[128];
unsigned char reg5[128];
unsigned char reg6[128];
unsigned char reg7[128];
unsigned char reg8[128];
unsigned char reg9[128];
unsigned char reg10[128];
unsigned char reg11[128];
unsigned char reg12[128];
unsigned char reg13[128];
unsigned char reg14[128];

unsigned char *evenregs[8] = { reg0, reg2, reg4, reg6, reg8, reg10, reg12, reg14 };

unsigned char s[] = { 0xC7, 0x65, 0xAB, 0xCA, 0xEE, 0xF7, 0x83, 0x09,	// note: s and t overlap
//unsigned char t[] =
    0xE1, 0xD0, 0x92, 0x67, 0x62, 0xB6, 0x72, 0x55, 0x8E, 0x91, 0xDC, 0xC5,
    0x81, 0xBE, 0x78, 0x20,
    0x59, 0xB7, 0xE6, 0x3D, 0x06, 0x45, 0xAF, 0xC8, 0x08, 0x31, 0x38, 0xD1,
    0xFB, 0x73, 0x84, 0xA9,
    0x17, 0xFC, 0x34, 0x87, 0xA3, 0x94, 0xFA, 0x90, 0xB8, 0xED, 0xCE, 0x3B,
    0x5B, 0x0A, 0x43, 0xD9,
    0xF3, 0x53, 0x82, 0xB3, 0x0D, 0x6D, 0x5A, 0x60, 0x9D, 0x51, 0xA7, 0xB9,
    0x11, 0x10, 0xBC, 0xE4,
    0x7F, 0x80, 0x41, 0xE7, 0xE3, 0xF6, 0x56, 0x26, 0x35, 0xEC, 0xD6, 0xDF,
    0x0C, 0x7F, 0xF4, 0x9E,
    0xAC, 0x52, 0x46, 0xEF, 0xCF, 0xBF, 0xA2, 0x3F, 0xA4, 0x13, 0x15, 0x97,
    0x4A, 0x1C, 0xB0, 0x42,
    0x8C, 0xB1, 0x05, 0x58, 0x80, 0x18, 0x77, 0x2B, 0x02, 0x3E, 0xA8, 0x49,
    0x1A, 0x6A, 0xCB, 0x6E,
    0x0B, 0x8A, 0xEB, 0xF1, 0x4F, 0x14, 0x79, 0x8B, 0xD8, 0x9F, 0x9B, 0x57,
    0x19, 0xF8, 0x2A, 0x2D,
    0x76, 0x0E, 0xE8, 0x2E, 0x4B, 0xF9, 0x07, 0x03, 0xDE, 0x93, 0x16, 0x7E,
    0xD4, 0xE5, 0xB2, 0xF0,
    0x7D, 0x7A, 0xDA, 0xD2, 0xA1, 0xCC, 0x1D, 0xE0, 0x5E, 0x23, 0xA0, 0x95,
    0x22, 0x1E, 0x36, 0x85,
    0xFE, 0x1F, 0x39, 0xAA, 0x89, 0x96, 0xAD, 0x0F, 0x2F, 0xC0, 0x47, 0x27,
    0x5D, 0x24, 0xEA, 0xC3,
    0xA5, 0xF5, 0x21, 0x5F, 0x1B, 0x40, 0x8F, 0xAE, 0x74, 0x25, 0xDD, 0xC1,
    0x7C, 0xCD, 0xA6, 0x70,
    0xD7, 0x33, 0x7B, 0x2C, 0x75, 0xBB, 0x86, 0x99, 0xBD, 0x54, 0x9A, 0x6C,
    0x63, 0x32, 0x48, 0x4C,
    0x8D, 0xBA, 0x5C, 0x61, 0xC4, 0x4E, 0x29, 0x37, 0x12, 0xC6, 0x98, 0x9C,
    0xD5, 0x69, 0x6B, 0xE2,
    0x04, 0x4D, 0xE9, 0xC2, 0x88, 0x3A, 0xDB, 0x64, 0x01, 0x44, 0x6F, 0xB5,
    0xF2, 0x30, 0x28, 0xFD,
    0x50, 0x71, 0x3C, 0xB4, 0x66, 0x68, 0xC9, 0xD3, 0xCA, 0x83, 0xC7, 0xAB,
    0xF7, 0x65, 0x09, 0xEE
};

char p[] = { 0x70, 0x3A, 0xC1, 0x6F, 0x9A, 0x92, 0xE5, 0x29, 0x18, 0xE7, 0x9E, 0x50,
    0xD6, 0xA5, 0x8D, 0xCC,
    0x4D, 0x52, 0x2A, 0xD4, 0x5C, 0x10, 0x71, 0x81, 0x24, 0xCF, 0xDA, 0x6A,
    0x4A, 0x72, 0xEE, 0xD5,
    0xCA, 0x36, 0x1E, 0x1B, 0x2A, 0x20, 0xC0, 0xDC, 0x15, 0xE8, 0xEE, 0x53,
    0xEB, 0xF3, 0x2E, 0x08,
    0x72, 0x59, 0x35, 0xF8, 0x99, 0x57, 0x3B
};

char pexp[] = { 0x1C, 0x0E, 0xB0, 0x5B, 0xE6, 0xA4, 0xB9, 0x4A, 0x46, 0x39, 0xE7, 0x94,
    0x35, 0xA9, 0x63, 0x73,
    0x13, 0x54, 0x8A, 0xB5, 0x17, 0x04, 0x1C, 0x60, 0x49, 0x33, 0xF6, 0x9A,
    0x92, 0x9C, 0xBB, 0xB5,
    0x72, 0x8D, 0x87, 0x86, 0xCA, 0x88, 0x30, 0x37, 0x05, 0x7A, 0x3B, 0x94,
    0xFA, 0xFC, 0xCB, 0x82,
    0x1C, 0x96, 0x4D, 0x7E, 0x26, 0x55, 0xCF
};

char q[] = { 0x16, 0x56, 0x15, 0x79, 0x9A, 0x53, 0x60, 0x4F, 0x87, 0x55, 0x9F, 0xB2,
    0x78, 0x72, 0xE5, 0xBC,
    0x7D, 0xF8, 0x6F, 0xCA, 0x83, 0x2A, 0x1F, 0xA7, 0x63, 0xA3, 0x55, 0x2B,
    0xD8, 0xB8, 0x45, 0xE2,
    0xA1, 0x1F, 0x41, 0x2B, 0x04, 0x1B, 0x9B, 0x5B, 0xE1, 0x28, 0xC5, 0xE0,
    0x6E, 0x7D, 0xC8, 0x0B,
    0x22, 0x1E, 0xE7, 0xD4, 0x47, 0x4C, 0xE5, 0xC8, 0x92, 0x22, 0xAD, 0xEF,
    0x70, 0x18, 0xF1, 0x3D,
    0x1B
};

char qexp[] = { 0x05, 0x95, 0x85, 0x5E, 0x66, 0x94, 0xD8, 0x13, 0xE1, 0xD5, 0x67, 0xEC,
    0x9E, 0x1C, 0xB9, 0x6F,
    0x1F, 0x7E, 0x1B, 0xF2, 0xA0, 0xCA, 0x87, 0xE9, 0xD8, 0xE8, 0xD5, 0x4A,
    0xF6, 0x2E, 0x11, 0x78,
    0xA8, 0x47, 0xD0, 0x4A, 0xC1, 0x06, 0xE6, 0xD6, 0xF8, 0x4A, 0x31, 0x78,
    0x1B, 0x9F, 0x72, 0x02,
    0xC8, 0x87, 0xB9, 0xF5, 0x11, 0xD3, 0x39, 0x72, 0x24, 0x88, 0xAB, 0x7B,
    0xDC, 0x06, 0x3C, 0x4F,
    0x47
};

unsigned char n[] = { 0x09, 0xCA, 0xC9, 0xC6, 0xB4, 0x12, 0x08, 0x1B, 0x60, 0x58, 0x81, 0x4B,
    0x86, 0x01, 0xD8, 0xBF,
    0xD9, 0x25, 0xA0, 0x7B, 0xDC, 0x32, 0x79, 0x84, 0x3B, 0x7C, 0xBC, 0x2F,
    0xE2, 0xE2, 0xFA, 0x8D,
    0x0A, 0x00, 0x3B, 0xC5, 0xEC, 0xAF, 0x2D, 0x8A, 0xCD, 0x06, 0x93, 0x6A,
    0xA5, 0x14, 0x46, 0x77,
    0xC4, 0x6A, 0xB2, 0x53, 0x36, 0xEF, 0x8C, 0xCE, 0x0C, 0xA2, 0x68, 0x71,
    0xD3, 0x73, 0xE8, 0xF7,
    0x6D, 0x06, 0xB5, 0x20, 0xEF, 0x23, 0x47, 0x0C, 0x51, 0x55, 0xC8, 0xFE,
    0xF4, 0x58, 0xC4, 0x3F,
    0x20, 0xA7, 0x67, 0x38, 0xB0, 0x76, 0xE2, 0xC4, 0xD8, 0x05, 0x63, 0xF8,
    0x3C, 0x58, 0x3B, 0x2D,
    0x22, 0xCC, 0x88, 0xB3, 0x71, 0x8F, 0x1D, 0x80, 0x0A, 0x87, 0xBD, 0xA1,
    0x59, 0x23, 0xE9, 0x70,
    0xE2, 0xD3, 0xEC, 0x46, 0x68, 0x80, 0x42, 0x39
};

char ap[] = { 0x04, 0x81, 0xBE, 0x5B, 0x4C, 0x70, 0x0F, 0xBC, 0x42, 0xA2, 0xCD, 0x8C,
    0xE7, 0x00, 0x26, 0x56,
    0xAE, 0x76, 0x65, 0x29, 0x3A, 0x00, 0xD1, 0xE6, 0xA2, 0x9E, 0x58, 0x77,
    0x62, 0x04, 0xF3, 0x54,
    0x2A, 0x30, 0xC3, 0x7D, 0xDF, 0x69, 0x9D, 0xED, 0xC5, 0x23, 0xB6, 0x9D,
    0x7F, 0x18, 0xCE, 0xF6,
    0x39, 0x0F, 0x14, 0x28, 0xF4, 0xDF, 0xF4, 0x98, 0xF5, 0x20, 0xC7, 0x64,
    0xA7, 0x7E, 0x77, 0x0D,
    0x87, 0x8D, 0x88, 0xEB, 0xCE, 0xD6, 0x59, 0x63, 0xD7, 0x70, 0xF6, 0x4A,
    0xE6, 0xFD, 0x83, 0x6D,
    0xBC, 0x6C, 0xEE, 0xF6, 0x4F, 0x88, 0x27, 0xBD, 0xEE, 0xBD, 0x61, 0x5D,
    0xA5, 0xB2, 0x5D, 0x78,
    0xB0, 0xA4, 0x71, 0x5E, 0x05, 0xC7, 0xBD, 0x6E, 0xAC, 0x4E, 0x5B, 0x2C,
    0x89, 0x16, 0x2B, 0x7F,
    0x2F, 0xB0, 0x7B, 0x14, 0x64, 0x7D, 0x44, 0xE7
};

char aq[] = { 0x05, 0x49, 0x0B, 0x6B, 0x67, 0xA1, 0xF8, 0x5F, 0x1D, 0xB5, 0xB3, 0xBE,
    0x9F, 0x01, 0xB2, 0x69,
    0x2A, 0xAF, 0x3B, 0x52, 0xA2, 0x31, 0xA7, 0x9D, 0x98, 0xDE, 0x63, 0xB8,
    0x80, 0xDE, 0x07, 0x38,
    0xDF, 0xCF, 0x78, 0x48, 0x0D, 0x45, 0x8F, 0x9D, 0x07, 0xE2, 0xDC, 0xCD,
    0x25, 0xFB, 0x77, 0x81,
    0x8B, 0x5B, 0x9E, 0x2A, 0x42, 0x0F, 0x98, 0x35, 0x17, 0x81, 0xA1, 0x0D,
    0x2B, 0xF5, 0x71, 0xE9,
    0xE5, 0x79, 0x2C, 0x35, 0x20, 0x4C, 0xED, 0xA8, 0x79, 0xE4, 0xD2, 0xB4,
    0x0D, 0x5B, 0x40, 0xD1,
    0x64, 0x3A, 0x78, 0x42, 0x60, 0xEE, 0xBB, 0x06, 0xE9, 0x48, 0x02, 0x9A,
    0x96, 0xA5, 0xDD, 0xB4,
    0x72, 0x28, 0x17, 0x55, 0x6B, 0xC7, 0x60, 0x11, 0x5E, 0x39, 0x62, 0x74,
    0xD0, 0x0D, 0xBD, 0xF1,
    0xB3, 0x23, 0x71, 0x32, 0x04, 0x02, 0xFD, 0x53
};


void dump (int n, unsigned char *data)
{
    int i;

    for (i = 0; i < n; i++)
    {
	if ((i & 15) == 8)
	    printf ("  ");
	else if (i & 15)
	    printf (" ");
	printf ("%.2x", data[i]);
	if ((i & 15) == 15)
	    printf ("\n");
    }
    printf ("\n");
    if (n & 15)
	printf ("\n");
}


//======================================================


void setregs (void)		// puts shifted copies of reg0 into even regs reg2..reg14
{
    int i, j, n, carry;
    unsigned char *r1, *r2;

    offsetr = sizer0 + 1;
    reg0[0] = 0;

    for (i = 0; i < 7; i++)
    {
	r1 = evenregs[i];
	r2 = evenregs[i + 1];

	carry = 0;
	for (j = offsetr; j >= 0; j--)
	{
	    n = r1[j] * 2 + carry;
	    r2[j] = n & 0xFF;
	    carry = (n >> 8) & 1;
	}
    }
}


void multadd (void)		// acc = acc + addmod
{
    int i, n, carry;

    carry = 0;

    for (i = offsetr; i >= 0; i--)
    {
	n = acc[i + adacmod0] + addmod[i] + carry;
	acc[i + adacmod0] = n & 0xFF;
	carry = (n >> 8) & 1;
    }

    while (carry)
    {
	n = acc[i + adacmod0] + carry;
	acc[i + adacmod0] = n & 0xFF;
	carry = (n >> 8) & 1;
	i--;
    }
}


void multiply (void)		// acc = reg0 * reg1
{
    int i, newoffseta;

    setregs ();

    offseta = sizer1 + 1;

    newoffseta = offseta + offsetr;

    for (i = 0; i <= newoffseta; i++)
	acc[i] = 0;

    adacmod0 = offseta + 1;

    while (offseta--)
    {
	adacmod0--;
	for (i = 0; i <= 7; i++)
	{
	    if (reg1[offseta] & 1 << i)
	    {
		addmod = evenregs[i];
		multadd ();
	    }
	}
    }
    offseta = newoffseta;
    starta = 1;
}


void divsub (void)		// acc = acc - submod
{
    int i, n, borrow;

    borrow = 0;

    for (i = offsetr; i >= 0; i--)
    {
	n = acc[i + sbacmod0] - submod[i] - borrow;
	acc[i + sbacmod0] = n & 0xFF;
	borrow = (n >> 8) & 1;
    }

    while (borrow)
    {
	n = acc[i + sbacmod0] - borrow;
	acc[i + sbacmod0] = n;
	borrow = (n >> 8) & 1;
	i--;
    }
}


int divcmp (void)		// returns 1 if acc > cmpmod
{
    int i;

    for (i = 0; i < offsetr; i++)
	if (acc[i + cpacmod0] != cmpmod[i])
	    return acc[i + cpacmod0] > cmpmod[i];
    return 0;
}


// this may do division, but it's only used for modulus
// support for saving the quotient has been added as comments
void divide (void)		// acc = acc mod reg0
{
    int i;

    setregs ();

    starta = offseta = sizea - sizer0;
    cpacmod0 = sbacmod0 = -1;
    acc[0] = 0;

    while (offseta--)
    {
	cpacmod0++;
	sbacmod0++;
	for (i = 7; i >= 0; i--)
	{
	    cmpmod = submod = evenregs[i];
	    if (divcmp ())
	    {
		divsub ();
//                              quotient[offseta+1] |= (1 << i);
	    }
	}
    }
    offseta = sizea;
}


void simplmod (void)		// if acc > modmod, acc = acc - modmod
{
    cmpmod = submod = modmod;
    cpacmod0 = sbacmod0 = 1;

    if (acc[0] == 0)
	if (!divcmp ())
	    return;

    divsub ();
}


void hseroff (void)		// reg0 * reg1 mod mdmmod0
{
    multiply ();

    sizer0 = mdmmod1;
    memcpy (reg0 + 1, mdmmod0, sizer0 + 1);

    sizea = offseta;
    divide ();
}


void square (void)		// reg3 * reg3 mod mdmmod0
{
    sizer0 = sizer7;
    memcpy (reg0 + 1, reg3, sizer7 + 1);
    sizer1 = sizer7;
    memcpy (reg1, reg3, sizer7 + 1);

    hseroff ();

    memcpy (reg3, acc + starta, sizer7 + 1);
}


void power (void)		// inputs reg3, reg5, reg7; output reg9
{
    int byt, bit;

    mdmmod0 = reg7;
    mdmmod1 = sizer7;

    reg9[sizer7] = 1;
    memset (reg9, 0, sizer7);

    byt = sizer5 + 1;
    while (byt--)
    {
	for (bit = 0; bit <= 7; bit++)
	{
	    if (reg5[byt] & (1 << bit))
	    {
		sizer0 = sizer1 = sizer7;
		memcpy (reg0 + 1, reg3, sizer7 + 1);
		memcpy (reg1, reg9, sizer7 + 1);

		hseroff ();

		memcpy (reg9, acc + starta, sizer7 + 1);
	    }
	    square ();
	}
    }
    offsetr = sizer7;
}


void hashpower (void)		// reg9 = reg11 ^ reg5 mod reg7
{
    memcpy (acc + 1, reg11, sizeof (n));

    acc[0] = 0;

    sizer0 = sizer7;

    memcpy (reg0 + 1, reg7, sizer7 + 1);

    sizea = sizeof (n);
    divide ();

    memcpy (reg3, acc + starta, sizer7 + 1);

    power ();
}


void dectest (void)		// decrypt hash for validation; acc = reg0 * reg1 mod n
{
    sizer0 = sizeof (n) - 1;
    sizer1 = sizeof (n) - 1;

    multiply ();

    memcpy (reg0 + 1, n, sizeof (n));

    sizea = offseta;
    divide ();
}


// input: cartridge hash in reg11
// output: returns 1 if success and cartridge signature in reg1
int encrypt_hash ()
{
    int i, tries;

    printf ("Encrypting...");

    for (tries = 0; tries < 256; tries++)
    {
	printf (" %.2X", reg11[4]);

	sizer7 = sizeof (p) - 1;
	memcpy (reg7, p, sizeof (p));	// reg7 = p
	sizer5 = sizeof (pexp) - 1;
	memcpy (reg5, pexp, sizeof (pexp));	// reg5 = pexp

	hashpower ();		// reg9 = reg11 ^ reg5 mod reg7

	memcpy (reg13, reg9, sizeof (p));	// reg13 = reg9

	sizer7 = sizeof (q) - 1;
	memcpy (reg7, q, sizeof (q));	// reg7 = q
	sizer5 = sizeof (qexp) - 1;
	memcpy (reg5, qexp, sizeof (qexp));	// reg5 = qexp

	hashpower ();		// reg9 = reg11 & reg5 mod reg7

	sizer1 = sizeof (q) - 1;
	memcpy (reg1, reg9, sizeof (q));	// reg1 = reg9

	sizer0 = sizeof (aq) - 1;
	memcpy (reg0 + 1, aq, sizeof (aq));	// reg0[1] = aq

	multiply ();		// acc = reg0 * reg1

	memcpy (reg0 + 1, n, sizeof (n));	// reg0[1] = n

	sizea = offseta;
	divide ();		// acc = acc mod reg0

	memcpy (reg3, acc + starta, sizeof (n));	// reg3 = acc[starta]

	sizer1 = sizeof (p) - 1;
	memcpy (reg1, reg13, sizeof (p));	// reg1 = reg13

	sizer0 = sizeof (ap) - 1;
	memcpy (reg0 + 1, ap, sizeof (ap));	// reg0[1] = ap

	multiply ();		// acc = reg0 * reg1

	memcpy (reg0 + 1, n, sizeof (n));	// reg0[1] = n

	sizea = offseta;
	divide ();		// acc = acc mod reg0

	offsetr = sizeof (n) - 1;
	memmove (acc + 1, acc + starta, sizeof (n));	// acc[1] = acc[starta]

	addmod = reg3;
	adacmod0 = 1;
	multadd ();		// acc = acc + reg3

	modmod = n;
	simplmod ();		// if acc > n, acc = acc - n

	memcpy (reg0 + 1, acc + 1, sizeof (n));	// reg0[1] = acc[1]
	memcpy (reg1, acc + 1, sizeof (n));	// reg1 = acc[1]

	dectest ();		// try decrypting this signature

	// compare decrypted signature
	for (i = 0; i < sizeof (n) && acc[i + starta] == reg11[i]; i++);
	if (i == sizeof (n))	// if decrypted properly...
	{
	    printf (" success!\n");
	    return 1;		//return success
	}

	reg11[4]++;		// try encrypting a different number
    }
    printf (" failed!\n");
    return 0;			// we've tried all possible numbers, so give up
}


void decrypt_sig (void)
{
    memcpy (reg0 + 1, cart + 0xFF80, sizeof (n));
    memcpy (reg1, cart + 0xFF80, sizeof (n));

    dectest ();

    memcpy (reg1, acc + starta, sizeof (reg1));

    reg1[0] = reg1[0] & 0x07;	// zero out the don't-care bits
    reg1[4] = 0;
}


//======================================================


void cscheck (int page, unsigned char *perm, int *a, int *carry)
{
    int i, n;
    int addr;

    addr = page << 8;
    for (i = 0; i < 256; i++)
    {
	n = *a + acc[i] + *carry;
	*carry = (n >> 8) & 1;
	n = (n & 0xFF) + cart[addr + i] + *carry;
	*a = perm[n & 0xFF];
	acc[i] = *a & 0xFF;
	*carry = (n >> 8) & 1;
    }
}


void csrotate (int *carry)
{
    int i, n;

    for (i = 0; i < 256; i++)
    {
	n = (acc[i] << 1) + *carry;
	acc[i] = n & 255;
	*carry = (n >> 8) & 1;
    }
}


void hashcart (void)
{
    int i, a, carry, startpage;

    startpage = cart[0xFFF9] & 0xF0;

    memcpy (acc, cart + 0xFF00, 256);	// copy FFxx page
    memset (acc + 0x80, 0, 0x78);	// zero out signature zone

    a = 0;
    carry = 1;
    for (i = startpage; i <= 0xFE; i++)
    {
	cscheck (i, s, &a, &carry);	// do s permutation
	carry = 0;
    }

    carry = 1;
    csrotate (&carry);
    csrotate (&carry);

    for (i = 0xFE; i >= startpage; i--)
    {
	cscheck (i, s + 8, &a, &carry);	// do t permutation
	carry = 1;
    }

    for (i = 0; i <= 0x77; i++)	// xor the whole mess together
	reg11[i] = acc[i] ^ acc[i + 0x50] ^ acc[i + 0x88];

    reg11[0] = reg11[0] & 0x07;	// zero out the don't-care bits
    reg11[4] = 0;
}


//======================================================


void usage (void)
{
    fprintf (stderr, "Atari 7800 digital signature generator - version %s\n", version);
    fprintf (stderr, "by Bruce Tomlin");
    fprintf (stderr, "\n");
    fprintf (stderr, "Usage:\n");
    fprintf (stderr, "    %s [options] image\n", progname);
    fprintf (stderr, "\n");
    fprintf (stderr, "Options:\n");
    fprintf (stderr, "    -t             test existing cartridge signature only\n");
    fprintf (stderr, "    -w             write back update signature to cartridge\n");
    exit (1);
}


void getopts (int argc, char *const argv[])
{
    int ch;

    while ((ch = getopt (argc, argv, "tw?")) != -1)
    {
	switch (ch)
	{
	case 't':
	    testonly = 1;
	    break;

	case 'w':
	    writeback = 1;
	    break;

	case '?':
	default:
	    usage ();
	}
    }
    argc -= optind;
    argv += optind;

    // now argc is the number of remaining arguments
    // and argv[0] is the first remaining argument

    if (argc != 1)
	usage ();

    strncpy (fname, argv[0], 255);

    // check for conflicting options
    if (testonly && writeback)
	usage ();

    // note: this won't work if there's a single-char filename in the current directory!
    if (fname[0] == '?' && fname[1] == 0)
	usage ();

}


int main (int argc, char *const argv[])
{
    int i, valid;
    long fsize;

    setvbuf (stdout, NULL, _IONBF, 0);	// turn off C library file buffering

    progname = argv[0];
    testonly = 0;
    writeback = 0;
    memset (fname, 0, sizeof (fname));
    getopts (argc, argv);

    f = fopen (fname, "rb");
    if (!f)
    {
	printf ("Can't open '%s'!\n", fname);
	exit (1);
    }

    // get size of file
    fseek (f, 0, SEEK_END);
    fsize = ftell (f);
    fseek (f, 0, SEEK_SET);

    if (((fsize & 0xFFF) != 0) && ((fsize & 0x0FFF) != 128))
    {
	printf ("Cartridge size not a multiple of 4K bytes!\n");
	exit (1);
    }
    if (fsize < 0x1000)
    {
	printf ("Cartridge data file must be at least 4K!\n");
	exit (1);
    }

    // skip to last 48K of file
    if (fsize > 0xC000)
    {
	fseek (f, fsize - 0xC000, SEEK_SET);
	fsize = 0xC000;
    }

    // read cartridge data
    i = fread (cart + 0x10000 - fsize, 1, fsize, f);
    fclose (f);

    printf ("Read $%.4X bytes of cartridge data.\n", i);

    // perform simple validaitions

    if ((cart[0xFFF8] & 0xF1) != 0xF1)
    {
	printf ("Invalid byte at $FFF8, should be $FF!\n");
	exit (1);
    }
    if (((cart[0xFFF9] & 0xF0) << 8) < 0x10000 - fsize)
    {
	printf ("Invalid byte at $FFF9, hash space larger than cartridge image!\n");
	exit (1);
    }
    if ((cart[0xFFF9] & 0x0B) != 3)
    {
	printf ("Invalid byte at $FFF9, low nibble should be 3 or 7!\n");
	exit (1);
    }
    if (cart[0xFFFD] < (cart[0xFFF9] & 0xF0))
    {
	printf ("Cartridge reset vector points outside hashed area!\n");
	exit (1);
    }

    printf ("Cartridge hash area is from $%.4X to $FFFF.\n", (cart[0xFFF9] & 0xF0) << 8);

    decrypt_sig ();
    hashcart ();

    for (i = 0; i <= 0x77 && reg1[i] == reg11[i]; i++);
    valid = (i == 0x78);

    if (valid)
	printf ("Cartridge signature for '%s' is valid!\n", fname);
    else
    {
	for (i = 0; i < 0x78 && ((cart[0xFF80 + i] == 0) || (cart[0xFF80 + i] == 0xff)); i++);
	if (i == 0x78)
	    printf ("Cartridge signature for '%s' appears to be empty.\n", fname);
	else
	    printf ("Cartridge signature for '%s' is not valid.\n", fname);
    }

    if (!testonly)
    {
	if (encrypt_hash ())
	{
	    printf ("\nA valid cartridge signature is:\n");
	    dump (120, reg1);

	    if (writeback)
	    {
		if (valid)
		{
		    printf ("Cartridge already has a valid signature, not re-writing file.");
		}
		else
		{
		    f = fopen (fname, "rb+");
		    if (!f)
		    {
			printf ("Can't open '%s' for writing!\n", fname);
			exit (1);
		    }

		    // get size of file
		    fseek (f, 0, SEEK_END);
		    fsize = ftell (f);
		    fseek (f, 0, SEEK_SET);

		    fseek (f, fsize - 128, SEEK_SET);	// seek to start of signature area
		    i = fwrite (reg1, 1, 120, f);	// write signature
		    printf ("Wrote back %d bytes to '%s'.\n", i, fname);

		    fclose (f);
		}
	    }
	}
    }
    else
	exit (1);

    return 0;
}
