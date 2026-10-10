#include <stdio.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>
#include "blockos_syscall.h"
int main(void){unsigned long uid=(unsigned long)__blockos_syscall(__SYS_getuid,0,0,0,0,0,0);struct passwd*p=getpwuid(uid);if(p&&p->pw_name){write(1,p->pw_name,strlen(p->pw_name));write(1,"\n",1);}else printf("%lu\n",uid);return 0;}
