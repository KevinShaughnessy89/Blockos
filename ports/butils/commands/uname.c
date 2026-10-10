#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
#include "blockos_syscall.h"
int main(int argc,char**argv){struct utsname u;if(__blockos_syscall(__SYS_uname,(long)&u,0,0,0,0,0)<0)return 1;if(argc==1){printf("%s %s %s %s %s\n",u.sysname,u.nodename,u.release,u.version,u.machine);return 0;}int any=0;for(int i=1;i<argc;i++){const char*s=argv[i];if(!strcmp(s,"-a")){printf("%s %s %s %s %s\n",u.sysname,u.nodename,u.release,u.version,u.machine);return 0;}for(;*s;s++){const char*v=0;if(*s=='-')continue;if(*s=='s')v=u.sysname;else if(*s=='n')v=u.nodename;else if(*s=='r')v=u.release;else if(*s=='v')v=u.version;else if(*s=='m')v=u.machine;else return 2;if(any)write(1," ",1);write(1,v,strlen(v));any=1;}}write(1,"\n",1);return 0;}
