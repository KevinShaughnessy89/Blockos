#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
#include "blockos_syscall.h"
int main(int argc,char**argv){(void)argv;struct utsname u;if(argc>1){write(2,"hostname: changing the hostname is not supported yet\n",53);return 2;}if(__blockos_syscall(__SYS_uname,(long)&u,0,0,0,0,0)<0)return 1;write(1,u.nodename,strlen(u.nodename));write(1,"\n",1);return 0;}
