#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "blockos_syscall.h"
#ifndef __SYS_rmdir
#define __SYS_rmdir 72
#endif
static int blockos_rmdir(const char *p){long r=__blockos_syscall(__SYS_rmdir,(long)p,0,0,0,0,0);return r<0?-1:0;}
static int idir(const struct stat*s){return(s->st_mode&S_IFMT)==S_IFDIR;}
static int remove_path(const char*p,int recursive){struct stat s;if(stat(p,&s)<0)return -1;if(idir(&s)){if(!recursive)return -1;DIR*d=opendir(p);if(!d)return -1;struct dirent*e;while((e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;char q[1024];int n=snprintf(q,sizeof q,"%s%s%s",p,(!strcmp(p,"/"))?"":"/",e->d_name);if(n<0||(unsigned long)n>=sizeof q||remove_path(q,1)<0){closedir(d);return -1;}}closedir(d);return blockos_rmdir(p);}return unlink(p);}
int main(int argc,char**argv){int rec=0,force=0,i=1;while(i<argc&&argv[i][0]=='-'){if(!strcmp(argv[i],"--")){i++;break;}for(const char*p=argv[i]+1;*p;p++){if(*p=='r'||*p=='R')rec=1;else if(*p=='f')force=1;else if(*p=='v'){}else return 2;}i++;}if(i==argc)return force?0:2;int rc=0;for(;i<argc;i++)if(remove_path(argv[i],rec)<0&&!force){write(2,"rm: remove failed\n",18);rc=1;}return rc;}
