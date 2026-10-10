#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "blockos_syscall.h"
int main(int argc,char**argv){(void)argv;if(argc>1){write(2,"Usage: pwd\n",11);return 2;}char p[512];long r=__blockos_syscall(__SYS_getcwd,(long)p,(long)sizeof p,0,0,0,0);if(r<0){write(2,"pwd: getcwd failed\n",19);return 1;}write(1,p,strlen(p));write(1,"\n",1);return 0;}
