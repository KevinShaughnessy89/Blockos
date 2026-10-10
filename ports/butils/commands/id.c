#include <stdio.h>
#include <pwd.h>
#include <grp.h>
#include "blockos_syscall.h"
int main(void){long u=__blockos_syscall(__SYS_getuid,0,0,0,0,0,0),g=__blockos_syscall(__SYS_getgid,0,0,0,0,0,0);struct passwd*p=getpwuid((unsigned long)u);struct group*q=getgrgid((unsigned long)g);printf("uid=%ld(%s) gid=%ld(%s)\n",u,p?p->pw_name:"?",g,q?q->gr_name:"?");return 0;}
