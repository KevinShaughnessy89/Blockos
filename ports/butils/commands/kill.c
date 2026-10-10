#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(int argc,char**argv){int sig=SIGTERM,i=1;if(i<argc&&argv[i][0]=='-'&&argv[i][1]){const char*s=argv[i]+1;if(!strcmp(s,"TERM"))sig=SIGTERM;else if(!strcmp(s,"KILL"))sig=SIGKILL;else if(!strcmp(s,"INT"))sig=SIGINT;else if(!strcmp(s,"QUIT"))sig=SIGQUIT;else sig=(int)strtol(s,0,10);i++;}if(i>=argc)return 2;int rc=0;for(;i<argc;i++)if(kill((int)strtol(argv[i],0,10),sig)<0)rc=1;return rc;}
