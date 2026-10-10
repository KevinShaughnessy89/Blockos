#include <unistd.h>
#include "blockos_syscall.h"
#ifndef __SYS_rmdir
#define __SYS_rmdir 72
#endif
int main(int argc,char**argv){if(argc<2){write(2,"Usage: rmdir DIR...\n",20);return 2;}int rc=0;for(int i=1;i<argc;i++)if(__blockos_syscall(__SYS_rmdir,(long)argv[i],0,0,0,0,0)<0){write(2,"rmdir: failed\n",14);rc=1;}return rc;}
