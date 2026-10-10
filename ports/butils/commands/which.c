#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "blockos_syscall.h"
int main(int argc,char**argv){if(argc<2)return 2;const char*path=getenv("PATH");if(!path)path="/System/bin:/bin:/usr/bin";int rc=0;for(int i=1;i<argc;i++){if(strchr(argv[i],'/')){if(__blockos_syscall(__SYS_access,(long)argv[i],0,0,0,0,0)>=0){write(1,argv[i],strlen(argv[i]));write(1,"\n",1);}else rc=1;continue;}const char*p=path;int found=0;while(*p){char candidate[512];size_t n=0;while(*p&&*p!=':'&&n+1<sizeof candidate)candidate[n++]=*p++;if(*p==':')p++;if(!n)candidate[n++]='.';if(n+strlen(argv[i])+2>=sizeof candidate)continue;candidate[n++]='/';strcpy(candidate+n,argv[i]);if(__blockos_syscall(__SYS_access,(long)candidate,0,0,0,0,0)>=0){write(1,candidate,strlen(candidate));write(1,"\n",1);found=1;break;}}if(!found)rc=1;}return rc;}
