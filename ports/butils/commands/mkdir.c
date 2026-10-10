#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int isdir(const struct stat*s){return(s->st_mode&S_IFMT)==S_IFDIR;}
static int make(const char*p,int parents){if(!parents)return mkdir(p,0777);char b[512];size_t n=strlen(p);if(!n||n>=sizeof b)return -1;memcpy(b,p,n+1);for(char*q=b+1;*q;q++){if(*q=='/'){*q=0;if(mkdir(b,0777)<0){struct stat s;if(stat(b,&s)<0||!isdir(&s)){*q='/';return -1;}}*q='/';}}if(mkdir(b,0777)<0){struct stat s;if(stat(b,&s)<0||!isdir(&s))return -1;}return 0;}
int main(int argc,char**argv){int p=0,i=1;if(i<argc&&!strcmp(argv[i],"-p")){p=1;i++;}if(i>=argc){write(2,"Usage: mkdir [-p] DIR...\n",25);return 2;}int rc=0;for(;i<argc;i++)if(make(argv[i],p)<0){write(2,"mkdir: failed\n",14);rc=1;}return rc;}
