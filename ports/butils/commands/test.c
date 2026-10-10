#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "blockos_syscall.h"
static int isdir(const struct stat*s){return(s->st_mode&S_IFMT)==S_IFDIR;}
int main(int argc,char**argv){int i=1,n=argc;int bracket=argv[0]&&argv[0][strlen(argv[0])-1]=='[';if(bracket){if(argc<2||strcmp(argv[argc-1],"]"))return 2;n--;}if(n-i==0)return 1;if(n-i==1)return argv[i][0]?0:1;if(n-i==2){const char*op=argv[i],*p=argv[i+1];struct stat s;if(!strcmp(op,"-e"))return __blockos_syscall(__SYS_access,(long)p,0,0,0,0,0)>=0?0:1;if(!strcmp(op,"-f"))return stat(p,&s)==0&&!isdir(&s)?0:1;if(!strcmp(op,"-d"))return stat(p,&s)==0&&isdir(&s)?0:1;if(!strcmp(op,"-s"))return stat(p,&s)==0&&s.st_size>0?0:1;if(!strcmp(op,"-n"))return *p?0:1;if(!strcmp(op,"-z"))return !*p?0:1;return 2;}if(n-i==3){const char*a=argv[i],*op=argv[i+1],*b=argv[i+2];if(!strcmp(op,"=")||!strcmp(op,"=="))return strcmp(a,b)==0?0:1;if(!strcmp(op,"!="))return strcmp(a,b)!=0?0:1;long x=strtol(a,0,10),y=strtol(b,0,10);if(!strcmp(op,"-eq"))return x==y?0:1;if(!strcmp(op,"-ne"))return x!=y?0:1;if(!strcmp(op,"-gt"))return x>y?0:1;if(!strcmp(op,"-ge"))return x>=y?0:1;if(!strcmp(op,"-lt"))return x<y?0:1;if(!strcmp(op,"-le"))return x<=y?0:1;}return 2;}
