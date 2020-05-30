/**
 * Network Testing tools
 *
 * ncopy - return N: directory.
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
#include "sio.h"
#include "conio.h"
#include "err.h"
#include "nsio.h"

char primary_buf[12288];
unsigned char i=0;
unsigned char valid=0;
unsigned char source_device=0;
unsigned char destination_device=0;
unsigned char source_unit=1;
unsigned char destination_unit=1;
unsigned char source_pos=0;
unsigned char destination_pos=0;
unsigned char comma_pos=0;

unsigned char parse_filespec(char* buf)
{
  // Find comma.
  
  for (comma_pos=0;comma_pos<strlen(buf);comma_pos++)
    {
      if (buf[comma_pos]==',')
	break;
    }

  // Invalid device character.
  if (buf[0]<0x41 || buf[0]>0x5A)
    return 0;
  
  // Get source device.
  source_device=buf[0];
  
  // Get destination device.
  for (i=comma_pos;i<strlen(buf);i++)
    {
      if (buf[i]==0x20) // SPACE
	continue;
      else
	break; // i now has pos of destination device.
    }
  destination_device=buf[i];
  destination_pos=i;
  
  if (destination_device<0x41 || destination_device>0x5A)
    return 0;

  // Get unit numbers, if available.
  if (buf[source_pos+1]>0x30 || buf[source_pos+1]<0x40)
    {
      source_unit=buf[source_pos+1]-0x30;
    }

  if (buf[destination_pos+1]>0x30 || buf[destination_pos+1]<0x40)
    {
      destination_unit=buf[destination_pos+1]-0x30;
    }
}

int copy_d_to_n(void)
{
  
  return 0;
}

int copy_n_to_d(void)
{
  return 0;
}

int copy_n_to_n(void)
{
  return 0;
}

int main(int argc, char* argv[])
{  
  OS.lmargn=2;

  memset(primary_buf,0,sizeof(primary_buf));

  // Args processing.
  
  if (argc>2)
    {
      // CLI DOS, concatenate arguments together.
      for (i=0;i<argc;i++)
	{
	  strcat(primary_buf,argv[i]);
	  if (i<argc-1)
	    strcat(primary_buf," ");
	}
    }
  else
    {
      // Interactive
      print("NET COPY--FROM, TO?\x9b");
      get_line(primary_buf,256);
    }

  // Validate for comma.
  for (i=0;i<strlen(primary_buf);i++)
    {
      if (primary_buf[i]==',')
	{
	  valid=1;
	  break;
	}
    }

  if (valid==0)
    {
      print("ERROR- NO COMMA FOUND.\x9b");
      return(1);
    }

  if (parse_filespec(primary_buf)==0)
    {
      print("COULD NOT PARSE FILESPEC.\x9b");
      return(1);
    }

  if (source_device=='D' && destination_device=='D')
    {
      print("USE DOS COPY.\x9b");
      return(1);
    }

  if (source_device=='D' && destination_device=='N')
    return(copy_d_to_n());
  else if (source_device=='N' && destination_device=='D')
    return(copy_n_to_d());
  else if (source_device=='N' && destination_device=='N')
    return(copy_n_to_n());
  
}
