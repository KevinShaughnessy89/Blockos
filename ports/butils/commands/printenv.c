#include <stdlib.h>
#include <string.h>
#include <unistd.h>
extern char **environ;
int main(int argc,char**argv){int rc=0;if(argc==1){for(char**p=environ;p&&*p;p++){write(1,*p,strlen(*p));write(1,"\n",1);}return 0;}for(int i=1;i<argc;i++){char*v=getenv(argv[i]);if(!v){rc=1;continue;}write(1,v,strlen(v));write(1,"\n",1);}return rc;}
