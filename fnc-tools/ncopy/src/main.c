/**
 * Network Testing tools
 *
 * ncopy - copy files 
 *  N:<->D: D:<->N: or N:<->N:
 *
 * Author: Thomas Cherryhomes
 *  <thom.cherryhomes@gmail.com>
 *
 * Released under GPL 3.0
 * See COPYING for details.
 */

#include <atari.h>
#include <string.h>
#include <stdlib.h>
#include <peekpoke.h>
#include <stdbool.h>
#include "sio.h"
#include "conio.h"
#include "err.h"
#include "nsio.h"
#include "blockio.h"

unsigned char yvar;

unsigned char i;
unsigned char argbuf[255];
unsigned char sourceDeviceSpec[255];
unsigned char destDeviceSpec[255];
unsigned char data[16384];
unsigned short data_len;
unsigned short block_len;
unsigned char* dp;
char errnum[4];

char* pToken;
unsigned char sourceUnit=1, destUnit=1;

void print_error(void)
{
  print("ERROR- ");
  itoa(yvar,errnum,10);
  print(errnum);
  print("\x9b");
}

bool parse_filespec(char* buf)
{
  // Find comma.
  pToken=strtok(buf,",");
  
  if (pToken==NULL)
    {
      print("NO COMMA\x9b");
      return false;
    }

  strcpy(sourceDeviceSpec,pToken);
  pToken=strtok(NULL,",");

  while (*pToken==0x20) { pToken++; }
  
  strcpy(destDeviceSpec,pToken);

  // Put EOLs on the end.
  sourceDeviceSpec[strlen(sourceDeviceSpec)]=0x9B;
  destDeviceSpec[strlen(destDeviceSpec)]=0x9B;

  // Check for valid device name chars
  if (sourceDeviceSpec[0]<0x41 || sourceDeviceSpec[0]>0x5A)
    return false;
  else if (destDeviceSpec[0]<0x41 || destDeviceSpec[0]>0x5A)
    return false;

  // Check for proper colon placement
  if (sourceDeviceSpec[1]!=':' && sourceDeviceSpec[2]!=':')
    return false;
  else if (destDeviceSpec[1]!=':' && destDeviceSpec[2]!=':')
    return false;

  // Try to assign unit numbers.
  if (sourceDeviceSpec[1] != ':')
    sourceUnit=sourceDeviceSpec[1]-0x30;
  if (destDeviceSpec[1] != ':')
    destUnit=destDeviceSpec[1]-0x30;

  print(sourceDeviceSpec);
  print(destDeviceSpec);
  
  return true;
}

int copy_d_to_n(void)
{
  open(sourceDeviceSpec,strlen(sourceDeviceSpec),4);

  if (yvar!=1)
    {
      print_error();
      close();
      return yvar;
    }

  nopen(destUnit,destDeviceSpec,8);

  if (OS.dcb.dstats!=1)
    {
      nstatus(destUnit);
      yvar=OS.dvstat[3];
      print_error();
      close();
      nclose(destUnit);
    }

  while (yvar==1)
    {
      get(data,sizeof(data));
      data_len=OS.iocb[2].buflen;

      nwrite(destUnit,data,data_len);
      data_len-=data_len;	  
    }

  close();
  nclose(destUnit);
  
  return 0;
}

int copy_n_to_d(void)
{
  nopen(sourceUnit,sourceDeviceSpec,4);

  if (OS.dcb.dstats!=1)
    {
      nstatus(destUnit);
      yvar=OS.dvstat[3];
      print_error();
      nclose(destUnit);
    }

  open(destDeviceSpec,strlen(destDeviceSpec),8);

  if (yvar!=1)
    {
      print_error();
      close();
      return yvar;
    }  

  do
    {
      nstatus(sourceUnit);
      data_len=OS.dvstat[1]*256+OS.dvstat[0];

      if (data_len==0)
	break;
      
      // Be sure not to overflow buffer!
      if (data_len>sizeof(data))
	data_len=sizeof(data);

      nread(sourceUnit,data,data_len); // add err chk
      put(data,data_len);
    } while (data_len>0);

  nclose(sourceUnit);
  close();
  return 0;
}

int copy_n_to_n(void)
{
  nopen(sourceUnit,sourceDeviceSpec,4);

  if (OS.dcb.dstats!=1)
    {
      nstatus(sourceUnit);
      yvar=OS.dvstat[3];
      print_error();
      nclose(sourceUnit);
    }
  
  nopen(destUnit,destDeviceSpec,8);

  if (OS.dcb.dstats!=1)
    {
      nstatus(destUnit);
      yvar=OS.dvstat[3];
      print_error();
      nclose(sourceUnit);
      nclose(destUnit);
    }

  do
    {
      nstatus(sourceUnit);
      data_len=OS.dvstat[1]*256+OS.dvstat[0];

      if (data_len==0)
	break;
      
      // Be sure not to overflow buffer!
      if (data_len>sizeof(data))
	data_len=sizeof(data);

      nread(sourceUnit,data,data_len); // add err chk
      nwrite(destUnit,data,data_len);
      
    } while (data_len>0);  
  
  nclose(sourceUnit);
  nclose(destUnit);
  
  return 0;
}

int main(int argc, char* argv[])
{
  OS.lmargn=2;

  // Args processing.  
  if (argc>1)
    {
      // CLI DOS, concatenate arguments together.
      for (i=1;i<argc;i++)
	{
	  strcat(argbuf,argv[i]);
	  if (i<argc-1)
	    strcat(argbuf," ");
	}
    }
  else
    {
      // Interactive
      print("NET COPY--FROM, TO?\x9b");
      get_line(argbuf,255);
    }

  if (parse_filespec(argbuf)==0)
    {
      print("COULD NOT PARSE FILESPEC.\x9b");
      return(1);
    }

  if (sourceDeviceSpec[0]=='D' && destDeviceSpec[0]=='N')
    return copy_d_to_n();
  else if (sourceDeviceSpec[0]=='N' && destDeviceSpec[0]=='D')
    return copy_n_to_d();
  else if (sourceDeviceSpec[0]=='N' && destDeviceSpec[0]=='N')
    return copy_n_to_n();
  
}
