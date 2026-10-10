#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int isdir(const struct stat *s){return (s->st_mode&S_IFMT)==S_IFDIR;}
static int list(const char *path,int all){struct stat st;if(stat(path,&st)<0){printf("ls: cannot access %s\n",path);return 1;}if(!isdir(&st)){printf("%s\n",path);return 0;}DIR *d=opendir(path);if(!d){printf("ls: cannot open %s\n",path);return 1;}struct dirent *e;int rc=0;while((e=readdir(d))){if(!all&&e->d_name[0]=='.')continue;printf("%s\n",e->d_name);}closedir(d);return rc;}
int main(int argc,char **argv){int all=0,i=1;while(i<argc&&argv[i][0]=='-'){if(strchr(argv[i],'a'))all=1;else if(!strcmp(argv[i],"--")){i++;break;}else{write(2,"Usage: ls [-a] [PATH ...]\n",26);return 2;}i++;}if(i==argc)return list(".",all);int rc=0;for(;i<argc;i++)if(list(argv[i],all))rc=1;return rc;}
