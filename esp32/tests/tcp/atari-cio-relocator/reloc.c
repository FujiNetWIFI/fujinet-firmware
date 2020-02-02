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

typedef unsigned int  word;
typedef unsigned char byte;

#define MEMLO *((word *) 0x02e7)

/*
 * exported symbols from rel.s
 */
extern word reloc_begin, reloc_end;
extern void function1( void );
extern void function2( void );
extern void function3( void );


word (*funcptr1)( void );
word (*funcptr2)( void );
word (*funcptr3)( void );

void main( void ) {
  word memory_delta = 0;
  word code_size = 0;
  word destination = 0;
  word fixes = 0;
  word base_function_table = 0;
  word index = 0;
  
  /*
   * copy relocable code to MEMLO
   * and adjust MEMLO up.
   */
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
   * fix JMPs and JSRs within memory region
   */
  for( index = MEMLO; index < MEMLO + code_size; index++ ) {
    if( PEEK( index ) == 0x4c || PEEK( index ) == 0x20 ) {
      destination = PEEKW( index + 1 );
      if( destination >= (word)&reloc_begin && destination <= (word)&reloc_end ) {
	destination -= memory_delta;
	POKEW( index + 1, destination );
	fixes += 1;
	index += 3;		/* skip over JMP/JSR we modified */
      }
    }
  }

  /* 
   * adjust memlo up to protect our routines
   */
  MEMLO += code_size + 1;
  
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
