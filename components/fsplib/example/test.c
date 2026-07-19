#include <stdio.h>
#include <assert.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "fsplib.h"

int main (int argc, char *argv[])
{
    int i;
    FSP_SESSION* s;
    FSP_PKT p;
    FSP_PKT r;
    FSP_FILE *f;
    struct dirent *d;
    FSP_DIR *dir;
    int port=2000;
    
    printf("Checking for fsp/udp service\n");
    s=fsp_open_session("localhost",0,NULL);
    assert(s);
    fsp_close_session(s);
    
    if(argc>1)
	port=atoi(argv[1]);
    printf("Running tests against fspd on localhost %d\n",port);
    s=fsp_open_session("localhost",port,NULL);
    assert(s);
    s->timeout=9000;

    p.cmd=FSP_CC_VERSION;
    for(i=0;i<100;i++)
    {
       assert (fsp_transaction(s,&p,&r)==0 );
    }

    /* dir list test */
    dir=fsp_opendir(s,"/");
    assert(dir);

    while ( (d=fsp_readdir(dir)) != NULL )
	printf("%s\n",d->d_name);

    fsp_closedir(dir);	

    /* get a file */
    f=fsp_fopen(s,"system.log","rb");
    assert(f);
    while( ( i=fsp_fread(p.buf,1,1000,f) ) )
	write(1,p.buf,i);
    fsp_fclose(f);

    printf("resends %d, dupes %d, cum. rtt %ld, last rtt %d\n",s->resends,s->dupes,s->rtts/s->trips,s->last_rtt);
    /* bye! */ 
    fsp_close_session(s);
    return 0;
}
