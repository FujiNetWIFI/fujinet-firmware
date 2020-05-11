Building Self-Relocating 'Terminate and Stay Resident' programs for Atari XL/XE
===============================================================================

*Daniel Kolakowski, 06-05-2020*

Introduction
------------

'Terminate and Stay Resident' (TSR) programs are the programs that attach themselves
to the system and stay there after giving control back to DOS.
Typically they implement additional device drivers or background helper tools
invoked for example by a combination of keys.

### Requirements

QuickAssembler (QA) written by Janusz B Wiśniewski in 1991.

Note: [MAD-ASSEMBLER](https://mads.atari8.info/mads_eng.html) seems to be almost
compatible with QA.

### References and acknowledgments

Example device handler, the Relocator and QuickAssembler has been written by Janusz B Wiśniewski.

> DK: The following example is a translation of a part of the "Programowanie 6502"
> ("Programming 6502") article from the Polish computer magazine "Tajemnice Atari"
> ("Secrets of Atari") 8/91, pages 4-5
> [see](http://tajemnice.atari8.info/8_91/8_91_6502.html).
> The original Relocator source code from this article has been replaced with
> the updated version printed in TA 6-7/92, page 19
> [see](http://tajemnice.atari8.info/6-7_92/6-7_92_6502.html).


Example of Self-Relocating TSR Device Handler
---------------------------------------------

*Janusz B Wiśniewski, Tajemnice Atari 8/91*

Let's create a primitive resident device handler identified by "B:" (aka screen
 border)

```
        OPT %100101
        ORG $9000
```

`OPT` directive instructs QA to compile to disk instead of to memory to avoid
self-destruction of the assembler.

> DK: QA emulates OS loading behavior while compiling to memory which means it
> executes code pointed by `INIAD` and `RUNAD` vectors.)

The program will be loaded into $9000 address from where it's going to be relocated
to some memory location indicated by the current `MEMLO` pointer.

Now lets define behavior of our unusual device handler:

* Store input data in memory location 712 which controls screen's border color.
* When reading data get them from random numbers register and if it's zero report error 136 (EOF)

```
        RANDOM EQU $D20A
        NEWDEV EQU $EEBC
        BORDER EQU 712
        MEMLO  EQU $2E7
        EOF    EQU 136
        DOSINI EQU 12

        START  EQU *
```

Below code is a placeholder for initialization routine that is going to replace
existing one invoked during the warm boot (i.e. to survive RESET key press.)

> DK: Atari OS executes code pointed by `DOSINI` vector after warm boot. Because
> it's a single vector it's up to programmer to play nicely with others and preserve
> previous value before updating it to create a call chain to the previous handlers.
> In the following line `0` address is going to be replaced by setup routine.

```
        INIT   JSR 0
```

Next step registers the new device handler identified as "B" in `HATABS` table.

> DK: Notice the usage of indirect pointers.

```
        LDX #'B'
        LDY BHADR
        LDA BHADR+1
        JSR NEWDEV
```

Now it's time to update `MEMLO` vector to point beyond our program:

> DK: This step basically makes the program a part of DOS and protects it from
> destruction.


```
        LDA NEWML
        STA MEMLO
        LDA NEWML+1
        STA MEMLO+1
        RTS
```

Next come I/O handlers for the new device: OPEN, CLOSE, GET, PUT, STATUS, SPECIAL.
Note how some instructions has been reused to save memory.

```
CLOSE   LDA #0
PUT     STA BORDER
OPEN    EQU *
STATUS  EQU *
SPEC    EQU *
OK      LDY #1
        RTS
GET     LDA RANDOM
        BNE OK
        LDY #EOF
        RTS
        BRK (koniec programu) DK: (end of executable code)
```

Single `BRK`* instruction serves as a marker for the relocator marking the end of
the executable code. After that begins a list of pointers to the program's data
that is used in place of hardcoded values to enable relocation:

> DK: * This means you can't use `BRK` instruction in your code for anything else.

```
NEWML   DTA A(ENDBH)
BHADR   DTA A(BHTAB)
```

> DK: I've just noticed that the new `MEMLO` value (NEWML) would leave static
> data section unprotected. I think it's a bug in the original code. It only
> works because this example doesn't have any static data.
> The correct line should read `NEWML   DTA A(SETINI)` in my opinion.

Here's device handler's entry to be added to `HATABS`:

```
BHTAB   DTA A(OPEN-1)
        DTA A(CLOSE-1)
        DTA A(GET-1)
        DTA A(PUT-1)
        DTA A(STATUS-1)
        DTA A(SPEC-l)
```

Original Atari OS specification requires a `JMP` instruction to some unspecified
initialization route at the end of this entry but it seems no acutal OS executes
that so I've decided to omit it.

```
ENDBH   EQU *
        DTA A(O)
```

A null pointer indicates the end of relocatable list of memory addresses.
After that follow remaining program's data: text messages, buffers, etc.
But this device handler doesn't have such data so here's an actual end of
the resident program and data.
The next piece of code is going to be executed only once and doesn't need to be
included in the relocated part.

```
SETINI  LDX #1
SI      LDA DOSINI,X
        STA INIT+1,X
        LDA MEMLO,X
        STA DOSINI,X
        DEX
        BPL SI
        RTS
```

Procedure `SETINI` stores the current DOS initialization vector before updating
it to the new one; and this concludes the one-time setup of the new device driver.


General Structure of Relocatable Program
----------------------------------------

*Daniel Kolakowski*

After reading both articles I've distilled the following skeleton structure of
a relocatable program:

```
* Initial program address should be set as high as possible allowing for enough
* room before screen memory to load the code, data and the Relocator.
* Assume BASIC/cartridge is present.

        ORG [some_safe_high_address] (e.g. $9000)

* The entry point must be at the beggining of the code.
MYMAIN  NOP

* Pointers to program data must be loaded from Pointers Section

* So instead of doing this:
        LDA <DATAZ
        STA $10
        LDA >DATAZ
        STA $11

* you should be doing this:
        LDA DATAZREF
        STA $10
        LDA DATAZREF+1
        STA $11

        RTS
* Indicate the end of the program with dummy BRK marker
        BRK

* Right after the marker begins Pointers Section.
* It's a list of addresses which values will be adjusted during
* the relocation process.
DATAZREF DTA A(DATAZ)
*Indicate the end of Pointers Section with null entry:
        DTA A(0)

* After this marker begins a section of program's data that will be moved unaltered.
DATAZ   DTA C'HELLO WORLD!'

* This is also the place where you can put your one-time initialization code
* executed _before_ the relocation takes place. Typically used to update DOSINI
* vector to build programs that survive warm reset (see example above):
MYSETUP  NOP
         RTS

* Here you must include the Relocator source code as it also serves as an indicator
* of the end of the program's data section.
* Use "INC" directive if your assembler supports such or copy-paste the code if it doesn't.
MAIN__ EQU MYMAIN
USER__ EQU MYSETUP

...

```


Relocator Source Code
---------------------

*Janusz B Wiśniewski, Tajemnice Atari 6-7/92*

TA 6-7/92: This updated version allows execution of the relocated program again
after returning to DOS using `RUN` command.
It uses `INITAD` $2E2 vector to perform relocation right before the end of
the loading process and sets up program's relocated MAIN__ entry in `RUNAD` $2E0
vector to be used by DOS.

```
MAIN__ EQU [main_func]
USER__ EQU [setup_func]


*------------------*
*  Relocator 1.1   *
*                  *
*      by JBW      *
*                  *
*    1992-02-10    *
*                  *
*------------------*


*--- page 0 --------

BYTE__ EQU $CE
DATF__ EQU $CF
DIST__ EQU $D0 (2)
SRCE__ EQU $D2 (2)
DEST__ EQU $D4 (2)
ADDR__ EQU $D6 (2)

*--- system --------

RUNA__ EQU $2E0  (2)
INIA__ EQU $2E2  (2)
MELO__ EQU $2E7  (2)

*--- move ----------

MOVE__ EQU *
       JSR USER__
* clear data flag
       LDA #0
       STA DATF__
* destination
       LDA MELO__
       STA DEST__
       STA RUNA__
       LDA MELO__+1
       STA DEST__+1
       STA RUNA__+1
* code source, distance
       SEC
       LDA <MAIN__
       STA SRCE__
       SBC DEST__
       STA DIST__
       LDA >MAIN__
       STA SRCE__+1
       SBC DEST__+1
       STA DIST__+1
*** move process ***
       LDY #0
       BEQ MOVL__ (JMP)
SEDA__ SEC
       ROR DATF__
MOVL__ EQU *
       LDA SRCE__
       CMP <MOVE__
       LDA SRCE__+1
       SBC >MOVE__
       BCC DCHK__
* done !
       RTS
* data flag check
DCHK__ BIT DATF__
       BVS MOV1__
       BMI TPE3__
INST__ EQU *
       LDA (SRCE__),Y
       STA BYTE__
       STA (DEST__),Y
       JSR INCA__
       TAX
       BEQ SEDA__
* instr type check
       CMP #$20  JSR
       BEQ TPE3__
       CMP #$40  RTI
       BEQ MOVL__
       CMP #$60  RTS
       BEQ MOVL__
       AND #$0D
       CMP #$08  x8,XA
       BEQ MOVL__
       BCC MOV1__
       TXA
       AND #$1F
       CMP #$09
       BEQ MOV1__
* 3-byte instruction
TPE3__ EQU *
       LDA (SRCE__),Y
       INY
       CMP <MAIN__
       LDA (SRCE__),Y
       DEY
       SBC >MAIN__
       BCC MOV2__
       LDA (SRCE__),Y
       INY
       CMP <MOVE__+1
       LDA (SRCE__),Y
       DEY
       SBC >MOVE__+1
       BCS MOV2__
* alter abs adresses
       LDA DIST__
       LDX DIST__+1
       BCC MOVA__
* move w/o changes
MOV2__ BIT DATF__
       BMI SEDA__
       LDA #0
       TAX
* move 2b address
MOVA__ EQU *
       STA ADDR__
       STX ADDR__+1
       SEC
       LDA (SRCE__),Y
       SBC ADDR__
       STA (DEST__),Y
       JSR INCA__
       LDA (SRCE__),Y
       SBC ADDR__+1
       JMP SD__
* move 1b data
MOV1__ EQU *
       LDA (SRCE__),Y
SD_    STA (DEST__),Y
       JSR INCA__
       JMP MOVL__


*--- inc SRCE,DEST -

INCA__ INC SRCE__
       BNE *+4
       INC SRCE__+1
       INC DEST__
       BNE *+4
       INC DEST__+1
       RTS


*--- start ---------

       ORG INIA__
       DTA A(MOVE__)


       END
```