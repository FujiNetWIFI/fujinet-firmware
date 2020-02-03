/*
 * reloc.c
 *
 * test relocatable code
 *
 * build: 
 *  cl65 -m reloc.map -t atari -C atari.cfg -Osir -o reloc.xex reloc.c rel.s
 */


#include <stdio.h>
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


void remap( byte instruction ) {
  word index = 0;

  printf("%4u", instruction );
  
  for( index = MEMLO; index < MEMLO + code_size; index++ ) {
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


void main( void ) {
  
  /*
   * copy relocable code to MEMLO
   * and adjust MEMLO up.
   */
  clrscr();
  printf("Copying...\n");
  
  code_size    = (word)&reloc_end - (word)&reloc_begin;
  memory_delta = (word)&reloc_begin - MEMLO;
  memcpy( MEMLO, &reloc_begin, code_size );

  /*
   * adjust function pointers
   */
  base_function_table = MEMLO;
  POKEW( MEMLO,     PEEKW( MEMLO     ) - memory_delta );
  POKEW( MEMLO + 2, PEEKW( MEMLO + 2 ) - memory_delta );
  POKEW( MEMLO + 4, PEEKW( MEMLO + 4 ) - memory_delta );

  /*
   * fix certain types of addresses
   */
  printf("Remapping: ");
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
