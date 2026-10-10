#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
#include "blockos_syscall.h"
int main(void){struct utsname u;if(__blockos_syscall(__SYS_uname,(long)&u,0,0,0,0,0)<0)return 1;write(1,u.machine,strlen(u.machine));write(1,"\n",1);return 0;}
