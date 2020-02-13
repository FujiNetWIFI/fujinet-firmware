/**
 * Thwap - A Timing Diagnostic Tool for #FujiNet
 *
 * Author: Thomas Cherryhomes
 *   <thom.cherryhomes@gmail.com>
 *
 * Version 1.0
 *
 * See LICENSE for copying details.
 */

#include <atari.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sio.h"

unsigned char buf[256];

unsigned short t0,t1,t2,t3,t4,t5;
unsigned char t0_s[8],t1_s[8],t2_s[8],t3_s[8],t4_s[8],t5_s[8];
unsigned char ddevic=0x70;
unsigned char dunit=0x01;
unsigned char dcomnd,dstats,dtimlo;
unsigned char dcomnd_s[8],dstats_s[8],dtimlo_s[8];
unsigned short dbyt,daux,reps;
unsigned char dbyt_s[8],daux_s[8],reps_s[8];
unsigned char patth_s[5],pattl_s[5];
unsigned char patth,pattl;
unsigned char doit_s[8];

void dump_buffer(unsigned short dbyt)
{
  unsigned short i;
  
  for (i=0;i<dbyt;i+=8)
    {
      printf("%02x: %02x %02x %02x %02x %02x %02x %02x %02x\n", i,
	     buf[i+0], buf[i+1], buf[i+2], buf[i+3],
	     buf[i+4], buf[i+5], buf[i+6], buf[i+7]);
    }
}

unsigned char write_timings(unsigned short t0,
			    unsigned short t1,
			    unsigned short t2,
			    unsigned short t3,
			    unsigned short t4,
			    unsigned short t5)
{
  unsigned char i;
  unsigned short tmp[6];

  tmp[0]=t0;
  tmp[1]=t1;
  tmp[2]=t2;
  tmp[3]=t3;
  tmp[4]=t4;
  tmp[5]=t5;

  for (i=0;i<6;i++)
    {
      OS.dcb.ddevic=0x70;
      OS.dcb.dunit=1;
      OS.dcb.dcomnd=0xb0+i; // Timing diagnostic commands begin at $B0
      OS.dcb.dstats=dstats;
      OS.dcb.dbuf=&buf;
      OS.dcb.dtimlo=dtimlo;
      OS.dcb.dbyt=0;
      OS.dcb.daux=tmp[i];
      siov();

      if (OS.dcb.dstats!=0x01)
	{
	  printf("-");
	  return OS.dcb.dstats;
	}
      else
	{
	  printf("+");
	}
    }
  return OS.dcb.dstats;
}

void main(void)
{
  unsigned short count;

  OS.lmargn=2;
  
  printf("THWAP - Timing Diagnostics\n\n");
  printf("This program can cause damage if used incorrectly.\n");
  printf("Only use if you know what you are doing!\n");

 top:
  printf("\n");

  printf("T0 (750): ");
  scanf("%d",t0_s);
  t0=atoi(t0_s);

  printf("T1 (750): ");
  scanf("%d",t1_s);
  t1=atoi(t1_s);

  printf("T2 (750): ");
  scanf("%d",t2_s);
  t2=atoi(t2_s);
  
  printf("T3 (750): ");
  scanf("%d",t3_s);
  t3=atoi(t3_s);
  
  printf("T4 (750): ");
  scanf("%d",t4_s);
  t4=atoi(t4_s);
  
  printf("T5 (750): ");
  scanf("%d",t5_s);
  t5=atoi(t5_s);

  printf("DDEVIC: $70\n");

  printf("DCOMND: $");
  scanf("%x",dcomnd_s);
  dcomnd=atoi(dcomnd_s);
  
  printf("DUNIT : $01\n");
  
  printf("DSTATS: $");
  scanf("%x",dstats_s);
  dstats=atoi(dstats_s);

  printf("DTIMLO: $");
  scanf("%x",dtimlo_s);
  dtimlo=atoi(dtimlo_s);

  printf("DBYT  : #");
  scanf("%u",dbyt_s);
  dbyt=atoi(dbyt_s);

  printf("DAUX  : #");
  scanf("%u",daux_s);
  daux=atoi(daux_s);

  if (dstats==0x80)
    {
      unsigned short i=0;
      printf("Write detected. Please enter desired pattern.\n\n");

      printf("PATTH : $");
      scanf("%x",patth_s);
      patth=atoi(patth_s);

      printf("PATTL : $");
      scanf("%x",pattl_s);
      pattl=atoi(pattl_s);

      memset(buf,patth,sizeof(buf));

      for (i=0; i<128; i++)
	buf[i<<1]=patth;

      printf("Target Buffer:\n");
      dump_buffer(dbyt);
    }
  
  printf("\n\n");

  printf("REPS  : #");
  scanf("%u",reps_s);
  reps=atoi(reps_s);

  printf("\n\nIf sure, type DOIT: ");
  scanf("%s",doit_s);

  if (strcmp(doit_s,"DOIT")==0)
    {
      printf("Writing Timing Values to #FujiNet...");

      if (write_timings(t0,t1,t2,t3,t4,t5)!=1)
	{
	  printf("failed. Please reset.\n");
	  exit(1);
	}
      else
	{
	  printf("done.\n");
	}
      
      for (count=0;count<reps;count++)
	{
	  OS.dcb.ddevic=0x70;
	  OS.dcb.dunit=1;
	  OS.dcb.dcomnd=dcomnd;
	  OS.dcb.dstats=dstats;
	  OS.dcb.dbuf=&buf;
	  OS.dcb.dtimlo=dtimlo;
	  OS.dcb.dbyt=dbyt;
	  OS.dcb.daux=daux;
	  siov();

	  printf("Rep #%d: DSTATS = %d ($%02x)\n",OS.dcb.dstats,OS.dcb.dstats);
	  
	  if (dstats==0x40)
	    {
	      printf("Returned buffer:\n");
	      dump_buffer(dbyt);
	    }
	}
    }
  else
    goto top;

}
