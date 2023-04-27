
#include "reciter.h"

#include <stdio.h>
#include <string.h>

#include "ReciterTabs.h"
#include "samdebug.h"

unsigned char rA, rX, rY;
extern int debug;

static unsigned char inputtemp[256]; // secure copy of input tab36096

void Code37055(unsigned char mem59)
{
    rX = mem59;
    rX--;
    rA = inputtemp[rX];
    rY = rA;
    rA = tab36376[rY];
    return;
}

void Code37066(unsigned char mem58)
{
    rX = mem58;
    rX++;
    rA = inputtemp[rX];
    rY = rA;
    rA = tab36376[rY];
}

unsigned char GetRuleByte(unsigned short mem62, unsigned char rY)
{
    unsigned int address = mem62;

    if (mem62 >= 37541)
    {
        address -= 37541;
        return rules2[address + rY];
    }
    address -= 32000;
    return rules[address + rY];
}

int TextToPhonemes(unsigned char *input) // Code36484
{
    //unsigned char *tab39445 = &mem[39445];   //input and output
    //unsigned char mem29;
    unsigned char mem56; //output position for phonemes
    unsigned char mem57;
    unsigned char mem58;
    unsigned char mem59;
    unsigned char mem60;
    unsigned char mem61;
    unsigned short mem62; // memory position of current rule

    unsigned char mem64; // position of '=' or current character
    unsigned char mem65; // position of ')'
    unsigned char mem66; // position of '('
    unsigned char mem36653;

    inputtemp[0] = 32;

    // secure copy of input
    // because input will be overwritten by phonemes
    rX = 1;
    rY = 0;
    do
    {
        //pos36499:
        rA = input[rY] & 127;
        if (rA >= 112)
            rA = rA & 95;
        else if (rA >= 96)
            rA = rA & 79;

        inputtemp[rX] = rA;
        rX++;
        rY++;
    } while (rY != 255);

    rX = 255;
    inputtemp[rX] = 27;
    mem61 = 255;

pos36550:
    rA = 255;
    mem56 = 255;

pos36554:
    while (1)
    {
        mem61++;
        rX = mem61;
        rA = inputtemp[rX];
        mem64 = rA;
        if (rA == '[')
        {
            mem56++;
            rX = mem56;
            rA = 155;
            input[rX] = 155;
            //goto pos36542;
            //          Code39771();    //Code39777();
            return 1;
        }

        //pos36579:
        if (rA != '.')
            break;
        rX++;
        rY = inputtemp[rX];
        rA = tab36376[rY] & 1;
        if (rA != 0)
            break;
        mem56++;
        rX = mem56;
        rA = '.';
        input[rX] = '.';
    } //while

    //pos36607:
    rA = mem64;
    rY = rA;
    rA = tab36376[rA];
    mem57 = rA;
    if ((rA & 2) != 0)
    {
        mem62 = 37541;
        goto pos36700;
    }

    //pos36630:
    rA = mem57;
    if (rA != 0)
        goto pos36677;
    rA = 32;
    inputtemp[rX] = ' ';
    mem56++;
    rX = mem56;
    if (rX > 120)
        goto pos36654;
    input[rX] = rA;
    goto pos36554;

    // -----

    //36653 is unknown. Contains position

pos36654:
    input[rX] = 155;
    rA = mem61;
    mem36653 = rA;
    //  mem29 = rA; // not used
    //  Code36538(); das ist eigentlich
    return 1;
    //Code39771();
    //go on if there is more input ???
    mem61 = mem36653;
    goto pos36550;

pos36677:
    rA = mem57 & 128;
    if (rA == 0)
    {
        //36683: BRK
        return 0;
    }

    // go to the right rules for this character.
    rX = mem64 - 'A';
    mem62 = tab37489[rX] | (tab37515[rX] << 8);

    // -------------------------------------
    // go to next rule
    // -------------------------------------

pos36700:

    // find next rule
    rY = 0;
    do
    {
        mem62 += 1;
        rA = GetRuleByte(mem62, rY);
    } while ((rA & 128) == 0);
    rY++;

    //pos36720:
    // find '('
    while (1)
    {
        rA = GetRuleByte(mem62, rY);
        if (rA == '(')
            break;
        rY++;
    }
    mem66 = rY;

    //pos36732:
    // find ')'
    do
    {
        rY++;
        rA = GetRuleByte(mem62, rY);
    } while (rA != ')');
    mem65 = rY;

    //pos36741:
    // find '='
    do
    {
        rY++;
        rA = GetRuleByte(mem62, rY);
        rA = rA & 127;
    } while (rA != '=');
    mem64 = rY;

    rX = mem61;
    mem60 = rX;

    // compare the string within the bracket
    rY = mem66;
    rY++;
    //pos36759:
    while (1)
    {
        mem57 = inputtemp[rX];
        rA = GetRuleByte(mem62, rY);
        if (rA != mem57)
            goto pos36700;
        rY++;
        if (rY == mem65)
            break;
        rX++;
        mem60 = rX;
    }

    // the string in the bracket is correct

    //pos36787:
    rA = mem61;
    mem59 = mem61;

pos36791:
    while (1)
    {
        mem66--;
        rY = mem66;
        rA = GetRuleByte(mem62, rY);
        mem57 = rA;
        //36800: BPL 36805
        if ((rA & 128) != 0)
            goto pos37180;
        rX = rA & 127;
        rA = tab36376[rX] & 128;
        if (rA == 0)
            break;
        rX = mem59 - 1;
        rA = inputtemp[rX];
        if (rA != mem57)
            goto pos36700;
        mem59 = rX;
    }

    //pos36833:
    rA = mem57;
    if (rA == ' ')
        goto pos36895;
    if (rA == '#')
        goto pos36910;
    if (rA == '.')
        goto pos36920;
    if (rA == '&')
        goto pos36935;
    if (rA == '@')
        goto pos36967;
    if (rA == '^')
        goto pos37004;
    if (rA == '+')
        goto pos37019;
    if (rA == ':')
        goto pos37040;
    //  Code42041();    //Error
    //36894: BRK
    return 0;

    // --------------

pos36895:
    Code37055(mem59);
    rA = rA & 128;
    if (rA != 0)
        goto pos36700;
pos36905:
    mem59 = rX;
    goto pos36791;

    // --------------

pos36910:
    Code37055(mem59);
    rA = rA & 64;
    if (rA != 0)
        goto pos36905;
    goto pos36700;

    // --------------

pos36920:
    Code37055(mem59);
    rA = rA & 8;
    if (rA == 0)
        goto pos36700;
pos36930:
    mem59 = rX;
    goto pos36791;

    // --------------

pos36935:
    Code37055(mem59);
    rA = rA & 16;
    if (rA != 0)
        goto pos36930;
    rA = inputtemp[rX];
    if (rA != 72)
        goto pos36700;
    rX--;
    rA = inputtemp[rX];
    if ((rA == 67) || (rA == 83))
        goto pos36930;
    goto pos36700;

    // --------------

pos36967:
    Code37055(mem59);
    rA = rA & 4;
    if (rA != 0)
        goto pos36930;
    rA = inputtemp[rX];
    if (rA != 72)
        goto pos36700;
    if ((rA != 84) && (rA != 67) && (rA != 83))
        goto pos36700;
    mem59 = rX;
    goto pos36791;

    // --------------

pos37004:
    Code37055(mem59);
    rA = rA & 32;
    if (rA == 0)
        goto pos36700;

pos37014:
    mem59 = rX;
    goto pos36791;

    // --------------

pos37019:
    rX = mem59;
    rX--;
    rA = inputtemp[rX];
    if ((rA == 'E') || (rA == 'I') || (rA == 'Y'))
        goto pos37014;
    goto pos36700;
    // --------------

pos37040:
    Code37055(mem59);
    rA = rA & 32;
    if (rA == 0)
        goto pos36791;
    mem59 = rX;
    goto pos37040;

    //---------------------------------------

pos37077:
    rX = mem58 + 1;
    rA = inputtemp[rX];
    if (rA != 'E')
        goto pos37157;
    rX++;
    rY = inputtemp[rX];
    rX--;
    rA = tab36376[rY] & 128;
    if (rA == 0)
        goto pos37108;
    rX++;
    rA = inputtemp[rX];
    if (rA != 'R')
        goto pos37113;
pos37108:
    mem58 = rX;
    goto pos37184;
pos37113:
    if ((rA == 83) || (rA == 68))
        goto pos37108; // 'S' 'D'
    if (rA != 76)
        goto pos37135; // 'L'
    rX++;
    rA = inputtemp[rX];
    if (rA != 89)
        goto pos36700;
    goto pos37108;

pos37135:
    if (rA != 70)
        goto pos36700;
    rX++;
    rA = inputtemp[rX];
    if (rA != 85)
        goto pos36700;
    rX++;
    rA = inputtemp[rX];
    if (rA == 76)
        goto pos37108;
    goto pos36700;

pos37157:
    if (rA != 73)
        goto pos36700;
    rX++;
    rA = inputtemp[rX];
    if (rA != 78)
        goto pos36700;
    rX++;
    rA = inputtemp[rX];
    if (rA == 71)
        goto pos37108;
    //pos37177:
    goto pos36700;

    // -----------------------------------------

pos37180:

    rA = mem60;
    mem58 = rA;

pos37184:
    rY = mem65 + 1;

    //37187: CPY 64
    //  if(? != 0) goto pos37194;
    if (rY == mem64)
        goto pos37455;
    mem65 = rY;
    //37196: LDA (62),y
    rA = GetRuleByte(mem62, rY);
    mem57 = rA;
    rX = rA;
    rA = tab36376[rX] & 128;
    if (rA == 0)
        goto pos37226;
    rX = mem58 + 1;
    rA = inputtemp[rX];
    if (rA != mem57)
        goto pos36700;
    mem58 = rX;
    goto pos37184;
pos37226:
    rA = mem57;
    if (rA == 32)
        goto pos37295; // ' '
    if (rA == 35)
        goto pos37310; // '#'
    if (rA == 46)
        goto pos37320; // '.'
    if (rA == 38)
        goto pos37335; // '&'
    if (rA == 64)
        goto pos37367; // ''
    if (rA == 94)
        goto pos37404; // ''
    if (rA == 43)
        goto pos37419; // '+'
    if (rA == 58)
        goto pos37440; // ':'
    if (rA == 37)
        goto pos37077; // '%'
    //pos37291:
    //  Code42041(); //Error
    //37294: BRK
    return 0;

    // --------------
pos37295:
    Code37066(mem58);
    rA = rA & 128;
    if (rA != 0)
        goto pos36700;
pos37305:
    mem58 = rX;
    goto pos37184;

    // --------------

pos37310:
    Code37066(mem58);
    rA = rA & 64;
    if (rA != 0)
        goto pos37305;
    goto pos36700;

    // --------------

pos37320:
    Code37066(mem58);
    rA = rA & 8;
    if (rA == 0)
        goto pos36700;

pos37330:
    mem58 = rX;
    goto pos37184;

    // --------------

pos37335:
    Code37066(mem58);
    rA = rA & 16;
    if (rA != 0)
        goto pos37330;
    rA = inputtemp[rX];
    if (rA != 72)
        goto pos36700;
    rX++;
    rA = inputtemp[rX];
    if ((rA == 67) || (rA == 83))
        goto pos37330;
    goto pos36700;

    // --------------

pos37367:
    Code37066(mem58);
    rA = rA & 4;
    if (rA != 0)
        goto pos37330;
    rA = inputtemp[rX];
    if (rA != 72)
        goto pos36700;
    if ((rA != 84) && (rA != 67) && (rA != 83))
        goto pos36700;
    mem58 = rX;
    goto pos37184;

    // --------------

pos37404:
    Code37066(mem58);
    rA = rA & 32;
    if (rA == 0)
        goto pos36700;
pos37414:
    mem58 = rX;
    goto pos37184;

    // --------------

pos37419:
    rX = mem58;
    rX++;
    rA = inputtemp[rX];
    if ((rA == 69) || (rA == 73) || (rA == 89))
        goto pos37414;
    goto pos36700;

    // ----------------------

pos37440:

    Code37066(mem58);
    rA = rA & 32;
    if (rA == 0)
        goto pos37184;
    mem58 = rX;
    goto pos37440;
pos37455:
    rY = mem64;
    mem61 = mem60;

    if (debug)
        PrintRule(mem62);

pos37461:
    //37461: LDA (62),y
    rA = GetRuleByte(mem62, rY);
    mem57 = rA;
    rA = rA & 127;
    if (rA != '=')
    {
        mem56++;
        rX = mem56;
        input[rX] = rA;
    }

    //37478: BIT 57
    //37480: BPL 37485  //not negative flag
    if ((mem57 & 128) == 0)
        goto pos37485; //???
    goto pos36554;
pos37485:
    rY++;
    goto pos37461;
}
