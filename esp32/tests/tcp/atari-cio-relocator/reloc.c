/*
 * reloc.c
 *
 * test relocatable code
 *
 * build: 
 *  cl65 -m reloc.map -t atari -C atari.cfg -Osir -o reloc.xex reloc.c rel.s
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


/*
 * exported symbols from rel.s
 */
extern word reloc_begin, reloc_end;
extern word reloc_code_begin;

extern void function1( void );
extern void function2( void );
extern void function3( void );


/*
 * globals
 */
word (*funcptr1)( void );
word (*funcptr2)( void );
word (*funcptr3)( void );
word memory_delta = 0;
word code_size = 0;
word destination = 0;
word fixes = 0;
word base_function_table = 0;
word index = 0;
word functions = 0;
word code_offset = 0;

void remap( byte instruction ) {
  word index = 0;

  printf("[RELOC] Instruction 0x%02x\n", instruction );
  
  for( index = code_offset; index < MEMLO + code_size; index++ ) {
    if( PEEK( index ) == instruction  ) {
      destination = PEEKW( index + 1 );
      if( destination >= (word)&reloc_begin && destination <= (word)&reloc_end ) {
	destination -= memory_delta;
	POKEW( index + 1, destination );
	fixes += 1;
	index += 3;
      }
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
  byte attempts    = 0;
  
 try_again:

  if( attempts > 200 )
    return false;
  
  for( index = MEMLO; index < MEMLO + code_size; index++ ) {
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


void main( void ) {
  
  /*
   * copy relocable code to MEMLO
   * and adjust MEMLO up.
   */
  clrscr();
  printf("Copying...\n");
  
  code_size    = (word)&reloc_end - (word)&reloc_begin;
  memory_delta = (word)&reloc_begin - MEMLO;
  code_offset  = MEMLO + ((word)&reloc_code_begin - (word)&reloc_begin);
  memcpy( MEMLO, &reloc_begin, code_size );

  printf( "[RELOC] Code:   %5u\n", code_size );
  printf( "[RELOC] Offset: %5u\n", code_offset );
  printf( "[RELOC] Reloc Begin: 0x%04x\n", &reloc_begin );
  printf( "[RELOC] Reloc Code:  0x%04x\n", &reloc_code_begin );
  printf( "[RELOC] Reloc End:   0x%04x\n", &reloc_end );
  
  /*
   * handle possible JMP (addr) bug of 0x??FF
   */
  printf("[RELOC] Indirect Jump Patch...\n");
  if( false == indirect_jump_patch() ) {
    printf("[RELOC] Patch failed.  Aborting...\n");
    exit( 0 );
  }
  
  /*
   * adjust function pointers
   *
   * format of function table:
   *     function count (byte)
   *     function1 (word)
   *     function2 (word)
   *     functionN (word)
   */
  base_function_table = MEMLO;
  functions = PEEK( MEMLO );	/* get the number of functions we need to adjust */
  printf( "[RELOC] %u public funcs...\n", functions );
  for( index = 0; index < functions; index++ ) {
    printf("[RELOC] Mapping 0x%4x to 0x%4x...\n",
	   PEEKW( MEMLO + (index << 1) + 1),
	   PEEKW( MEMLO + (index << 1) + 1 ) - memory_delta);
    POKEW( MEMLO + (index << 1) + 1,
	   PEEKW( MEMLO + (index << 1) + 1 ) - memory_delta );
  }
  
  /*
   * fix certain types of addresses
   */
  printf( "[RELOC] Remapping instructions...\n" );
  remap( JMP  );
  remap( JSR  );
  remap( LDA  );
  remap( LDAX );
  remap( LDAY );
  remap( STA  );
  remap( STAX );
  remap( STAY );
  remap( STX  );
  remap( LDX  );
  remap( LDXY );
  remap( STY  );
  remap( LDY  );
  remap( LDYX );
  remap( ADC  );
  remap( ADCX );
  remap( ADCY );
  remap( AND  );
  remap( ANDX );
  remap( ANDY );
  remap( ASL  );
  remap( ASLX );
  remap( BIT  );
  remap( CMP  );
  remap( CMPX );
  remap( CMPY );
  remap( CPX  );
  remap( CPY  );
  remap( DEC  );
  remap( DECX );
  remap( EOR  );
  remap( EORX );
  remap( EORY );
  remap( INC  );
  remap( INCX );
  remap( JMPI );
  remap( LSR  );
  remap( LSRX );
  remap( ORA  );
  remap( ORAX );
  remap( ORAY );
  remap( ROL  );
  remap( ROLX );
  remap( SBC  );
  remap( SBCX );
  remap( SBCY );
  
  clrscr();
  
  /* 
   * adjust memlo up to protect our routines
   */
  MEMLO += code_size + 1;

  /*
   * print report
   */
  base_function_table += 1;	/* skip over byte count */
  
  funcptr1 = PEEKW( base_function_table );
  funcptr2 = PEEKW( base_function_table + 2 );
  funcptr3 = PEEKW( base_function_table + 4 );

  printf("\n");
  printf("-[ Relocator ]--------------------\n\n");
  printf("MEMLO     = %6u\n", MEMLO );
  printf("Fixes     = %6u\n", fixes );
  printf("F1 addr   = %6u\n", funcptr1 );
  printf("F2 addr   = %6u\n", funcptr2 );
  printf("F3 addr   = %6u\n", funcptr3 );
  printf("\nUsage:\n");
  printf("  ? usr( %u ) [prints 1]\n", funcptr1 );
  printf("  ? usr( %u ) [prints 2]\n", funcptr2 );
  printf("  ? usr( %u ) [prints 4]\n", funcptr3 );
  printf("\n--------------[ Ready for BASIC ]-\n");
}
