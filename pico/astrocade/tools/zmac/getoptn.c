/* getoptn.c - a getopt() clone, so that cmdline option parsing will
 *         work on non-Unix systems.
 *
 * PD by RJM
 */

#include <stdio.h>
#include <string.h>
#include "getoptn.h"


/* equivalents of optopt, opterr, optind, and optarg */
int optnopt=0,optnerr=0,optnind=1;
char *optnarg=NULL;

/* holds offset in current argv[] value */
static unsigned int optnpos=1;


/* This routine assumes that the caller is pretty sane and doesn't
 * try passing an invalid 'optstring' or varying argc/argv.
 */
int getoptn(int argc,char *argv[],char *optstring)
{
char *ptr;

/* check for end of arg list */
if(optnind==argc || *(argv[optnind])!='-' || strlen(argv[optnind])<=1)
  return(-1);

if((ptr=strchr(optstring,argv[optnind][optnpos]))==NULL)
  return('?');		/* error: unknown option */
else
  {
  optnopt=*ptr;
  if(ptr[1]==':')
    {
    if(optnind==argc-1) return(':');	/* error: missing option */
    optnarg=argv[optnind+1];
    optnpos=1;
    optnind+=2;
    return(optnopt);	/* return early, avoiding the normal increment */
    }
  }

/* now increment position ready for next time.
 * no checking is done for the end of args yet - this is done on
 * the next call.
 */
optnpos++;
if(optnpos>=strlen(argv[optnind]))
  {
  optnpos=1;
  optnind++;
  }

return(optnopt);	/* return the found option */
}
