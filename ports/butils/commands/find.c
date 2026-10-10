#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int idir(const struct stat*s){return (s->st_mode&S_IFMT)==S_IFDIR;}
static int match(const char*p,const char*s){if(!*p)return !*s;if(*p=='*'){p++;if(!*p)return 1;while(*s){if(match(p,s))return 1;s++;}return match(p,s);}if(*p=='?')return *s&&match(p+1,s+1);return *p==*s&&match(p+1,s+1);}
static void walk(const char *p,const char *pat,char type,int depth){struct stat s;if(stat(p,&s)<0)return;int d=idir(&s);const char*base=p;for(const char*q=p;*q;q++)if(*q=='/')base=q+1;if((!pat||match(pat,base))&&(!type||(type=='d'&&d)||(type=='f'&&!d)))printf("%s\n",p);if(!d||depth>64)return;DIR*dir=opendir(p);if(!dir)return;struct dirent*e;while((e=readdir(dir))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;char q[1024];int n=snprintf(q,sizeof q,"%s%s%s",p,(!strcmp(p,"/"))?"":"/",e->d_name);if(n>0&&(unsigned long)n<sizeof q)walk(q,pat,type,depth+1);}closedir(dir);}
int main(int argc,char**argv){const char*path=".";const char*pat=0;char type=0;int i=1;if(i<argc&&argv[i][0]!='-')path=argv[i++];while(i<argc){if(!strcmp(argv[i],"-name")&&i+1<argc){pat=argv[i+1];i+=2;}else if(!strcmp(argv[i],"-type")&&i+1<argc){type=argv[i+1][0];i+=2;}else{write(2,"Usage: find [PATH] [-name PATTERN] [-type f|d]\n",47);return 2;}}walk(path,pat,type,0);return 0;}
