/*
 * reloc.c
 *
 * test relocatable code
 *
 * build: 
 *  cl65 -m reloc.map -t atari -C atari.cfg -Osir -o reloc.xex reloc.c rel.s
 *
 * dos 2.5: fre(0)      = 32274
 * dos 2.5 w/ N: fre(0) = 31021
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <peekpoke.h>
#include <conio.h>

typedef unsigned int  word;
typedef unsigned char byte;

/*
 * atari symbols - sometimes generates shorter code than 
 * using the atari.h symbols.
 */
#define MEMLO *((word *) 0x02e7)

/*
 * three byte assembly instructions we need to remap
 */
#define ADC  0x6d
#define ADCX 0x7d
#define ADCY 0x79
#define AND  0x2d
#define ANDX 0x3d
#define ANDY 0x39
#define ASL  0x0e
#define ASLX 0x1e
#define BIT  0x2c
#define CMP  0xcd
#define CMPX 0xdd
#define CMPY 0xd9
#define CPX  0xec
#define CPY  0xcc
#define DEC  0xce
#define DECX 0xde
#define EOR  0x4d
#define EORX 0x5d
#define EORY 0x59
#define INC  0xee		/* INC oper    */
#define INCX 0xfe		/* INC oper,x  */
#define JMPI 0x6c		/* JMP (oper)  */
#define LSR  0x4e
#define LSRX 0x5e
#define ORA  0x0d
#define ORAX 0x1d
#define ORAY 0x19
#define ROL  0x2e
#define ROLX 0x3d
#define ROR  0x6e
#define RORX 0x7e
#define SBC  0xed
#define SBCX 0xfd
#define SBCY 0xf9
#define JMP  0x4c		/* JMP oper    */
#define JSR  0x20		/* JSR oper    */
#define LDA  0xad		/* LDA oper    */
#define LDAX 0xbd		/* LDA oper,x  */
#define LDAY 0xb9		/* LDA oper,y  */
#define STA  0x8d		/* STA oper    */
#define STAX 0x9d		/* STA oper,x  */
#define STAY 0x99		/* STA oper,y  */
#define STX  0x8e		/* STX oper    */
#define LDX  0xae		/* LDX oper    */
#define LDXY 0xbe               /* LDX oper,y  */
#define STY  0x8c		/* STY oper    */
#define LDY  0xac		/* LDY oper    */
#define LDYX 0xbc 		/* LDY oper,x  */

#define insiz( which, value ) instruction_bytes[ which ] = value

/*
 * exported symbols from rel.s
 */
extern word reloc_begin, reloc_end;
extern word reloc_code_begin;

/*
 * globals
 */
char *banner            = "#FujiNET N: Handler v0.0.1";
char *banner_error      = "#FujiNET Not Responding.";
char *banner_functional = "#FujiNET Active.";

byte instruction_bytes[ 256 ] = {
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2
};

word memory_delta = 0;
word code_size = 0;
word destination = 0;
word fixes = 0;
word base_function_table = 0;
word index = 0;
word functions = 0;
word code_offset = 0;
word reclaimed = 0;
word init_function = 0;
word old_address = 0;
word new_address = 0;
byte (*init_function_ptr)( void );

void remap_all( void ) {
  byte instruction_size;
  word index = code_offset;
  word length = MEMLO + code_size;

  for( index = code_offset; index < length; ) {
    instruction_size = instruction_bytes[ PEEK( index ) ];
    switch( instruction_size ) {
    case 1:
      index += 1;
      break;
    case 2:
      index += 2;
      break;
    case 3:
      destination  = PEEKW( index + 1 );
      if( destination >= (word)&reloc_begin && destination <= (word)&reloc_end ) {
	destination -= memory_delta;
	POKEW( index + 1, destination );
	fixes += 1;
      }
      index += 3;
      break;
    }
  }    
}


/*
 * from StickJock @ atariage
 *
 * 6502s apparently have a bug that is triggered when
 * it encounters an indirect JMP instruction whos destination's
 * low byte is 0xff - so see if that will occur post-move and
 * if so adjust MEMLO up by one byte, recopy, and rescan.
 *
 * can be time consuming as it (potentially) recopies source 
 * over and over but that's ok for now.  perhaps later we'll 
 * scan the source  BEFORE copy - low priority as this is 
 * boot code that runs once and is thrown away.
 *
 * from: http://nesdev.com/6502bugs.txt
 *
 * "An indirect JMP (xxFF) will fail because the MSB will be 
 *  fetched from address xx00 instead of page xx+1."
 *
 * returns true if we're good to go or false if we're taking 
 * too long ( attemps > 200 ).
 */
bool indirect_jump_patch( void ) {
  word index       = 0;
  word destination = 0;
  word length      = MEMLO + code_size;
  byte attempts    = 0;
  
 try_again:

  if( attempts > 200 )
    return false;

  for( index = MEMLO; index < length; index++ ) {
    if( PEEK( index ) == JMPI ) {
      destination = PEEKW( index  + 1 );
      if( destination >= (word)&reloc_begin && destination <= (word)&reloc_end ) {
	destination -= memory_delta;
	if(( destination & 0x00ff ) == 0x00ff ) {
	  fixes    += 1;		/* count this as a fix made */
	  MEMLO    += 1;		/* adjust MEMLO one byte higher */
	  attempts += 1;		/* burn one of 200 attemps */
	  memcpy( MEMLO, &reloc_begin, code_size );
	  goto try_again;
	}
      }
    }
  }
  return true;
}


void setup_instruction_table( void ) {
  insiz( 0x0a, 1 ); // asl accum
  insiz( 0x00, 1 ); // brk
  insiz( 0x18, 1 ); // clc
  insiz( 0xd8, 1 ); // cld
  insiz( 0x58, 1 ); // cli
  insiz( 0xba, 1 ); // clv
  insiz( 0xca, 1 ); // dex
  insiz( 0x88, 1 ); // dey
  insiz( 0xe8, 1 ); // inx
  insiz( 0xc8, 1 ); // iny
  insiz( 0x4a, 1 ); // lsr accum
  insiz( 0xea, 1 ); // nop
  insiz( 0x48, 1 ); // pha
  insiz( 0x08, 1 ); // php
  insiz( 0x68, 1 ); // pla
  insiz( 0x28, 1 ); // plp
  insiz( 0x2a, 1 ); // rol accum
  insiz( 0x6a, 1 ); // ror accum
  insiz( 0x40, 1 ); // rti
  insiz( 0x60, 1 ); // rts
  insiz( 0x38, 1 ); // sec
  insiz( 0xf8, 1 ); // sed
  insiz( 0x78, 1 ); // sei
  insiz( 0xaa, 1 ); // tax
  insiz( 0xa8, 1 ); // tay
  insiz( 0xba, 1 ); // tsx
  insiz( 0x8a, 1 ); // txa
  insiz( 0x9a, 1 ); // txs
  insiz( 0x98, 1 ); // tya
  
  insiz( JMP,  3 );
  insiz( JSR,  3 );
  insiz( LDA,  3 );
  insiz( LDAX, 3 );
  insiz( LDAY, 3 );
  insiz( STA,  3 );
  insiz( STAX, 3 );
  insiz( STAY, 3 );
  insiz( STX,  3 );
  insiz( LDX,  3 );
  insiz( LDXY, 3 );
  insiz( STY,  3 );
  insiz( LDY,  3 );
  insiz( LDYX, 3 );
  insiz( ADC,  3 );
  insiz( ADCX, 3 );
  insiz( ADCY, 3 );
  insiz( AND,  3 );
  insiz( ANDX, 3 );
  insiz( ANDY, 3 );
  insiz( ASL,  3 );
  insiz( ASLX, 3 );
  insiz( BIT,  3 );
  insiz( CMP,  3 );
  insiz( CMPX, 3 );
  insiz( CMPY, 3 );
  insiz( CPX,  3 );
  insiz( CPY,  3 );
  insiz( DEC,  3 );
  insiz( DECX, 3 );
  insiz( EOR,  3 );
  insiz( EORX, 3 );
  insiz( EORY, 3 );
  insiz( INC,  3 );
  insiz( INCX, 3 );
  insiz( JMPI, 3 );
  insiz( LSR,  3 );
  insiz( LSRX, 3 );
  insiz( ORA,  3 );
  insiz( ORAX, 3 );
  insiz( ORAY, 3 );
  insiz( ROL,  3 );
  insiz( ROLX, 3 );
  insiz( SBC,  3 );
  insiz( SBCX, 3 );
  insiz( SBCY, 3 );
}

void main( void ) {
  
  /*
   * copy relocable code to MEMLO
   * and adjust MEMLO up.
   */
  clrscr();
  printf("\n");
  printf("-[ Relocator ]--------------------\n\n");

  setup_instruction_table();
  
  code_size    = (word)&reloc_end - (word)&reloc_begin;
  memory_delta = (word)&reloc_begin - MEMLO;
  code_offset  = MEMLO + ((word)&reloc_code_begin - (word)&reloc_begin);
  memcpy( MEMLO, &reloc_begin, code_size );

  /*
  printf( "MEMLO:       %u\n", MEMLO );
  printf( "Code:        %u\n", code_size );
  printf( "Offset:      %u\n", code_offset );
  printf( "Delta:       %u\n", memory_delta );
  printf( "Reloc Begin: %u\n", &reloc_begin );
  printf( "Reloc Code:  %u\n", &reloc_code_begin );
  printf( "Reloc End:   %u\n", &reloc_end );
  */
  
  /*
   * handle possible JMP (addr) bug of 0x??FF
   */
  cprintf( "Indirect Jump Patch...   \r");
  if( false == indirect_jump_patch() ) {
    cprintf("Patch failed.  Aborting...      \r");
    exit( 0 );
   }
  
  /*
   * adjust function pointers
   *
   * format of function table:
   *     function count (byte)
   *     function1 (word) [normally module init function]
   *     function2 (word)
   *     functionN (word)
   */
  base_function_table = MEMLO;
  functions = PEEK( MEMLO );	/* get the number of functions we need to adjust */
  cprintf( "%u public func(s)...    \r", functions );
  
  for( index = 0; index < functions; index++ ) {
    old_address = PEEKW( MEMLO + ( index << 1 ) + 1 );
    new_address = old_address - memory_delta;
    cprintf("Mapping %u to %u...      \r",
	   old_address,
	   new_address );

    /*
     * adjust function entry point
     */
    POKEW( MEMLO + ( index << 1 ) + 1, new_address );
    
    /*
     * are we processing the init function?
     */
    if( index == 0 )
      init_function = new_address; 

  }

  /*
   * patch 3-byte instructions
   */
  cprintf( "Remapping Instructions...        \r" );
  remap_all();
  cprintf( "                                 \r" );
  
  /*
   * print report
   */
  base_function_table += 1;	/* skip over byte count */ 
  
  printf("Current MEMLO   = %6u\n", init_function );
  printf("Address Fixes   = %6u\n", fixes );
  printf("Resident Bytes  = %6u\n", init_function - MEMLO );
  printf("Reclaimed Bytes = %6u\n", code_size - ( init_function - MEMLO ));
  printf("\n-----------[ Relocator Complete ]-\n\n");
  puts( banner );

  init_function_ptr = init_function;
  
  if( 1 == init_function_ptr() )
    puts( banner_functional );
  else
    puts( banner_error );
  
  MEMLO = init_function;
}


