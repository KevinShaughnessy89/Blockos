#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
int main(int argc,char**argv){if(argc<2)return 2;int rc=0;for(int i=1;i<argc;i++){struct stat s;if(stat(argv[i],&s)<0){printf("stat: cannot stat %s\n",argv[i]);rc=1;continue;}const char*t=(s.st_mode&S_IFMT)==S_IFDIR?"directory":(s.st_mode&S_IFMT)==S_IFREG?"regular file":"other";printf("File: %s\nSize: %lu\nType: %s\nMode: %lu\n",argv[i],(unsigned long)s.st_size,t,(unsigned long)s.st_mode);}return rc;}
