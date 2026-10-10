#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int isdir(const struct stat*s){return (s->st_mode&S_IFMT)==S_IFDIR;}
static unsigned long long size_of(const char*p,int depth){struct stat s;if(stat(p,&s)<0)return 0;if(!isdir(&s))return s.st_size>0?s.st_size:0;if(depth>64)return 0;DIR*d=opendir(p);if(!d)return 0;unsigned long long total=0;struct dirent*e;while((e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;char q[1024];int n=snprintf(q,sizeof q,"%s%s%s",p,(!strcmp(p,"/"))?"":"/",e->d_name);if(n>0&&(unsigned long)n<sizeof q)total+=size_of(q,depth+1);}closedir(d);return total;}
int main(int argc,char **argv){if(argc==1){argv[1]=".";argc=2;}for(int i=1;i<argc;i++){unsigned long long b=size_of(argv[i],0);printf("%lu %s\n",(unsigned long)((b+1023)/1024),argv[i]);}return 0;}
