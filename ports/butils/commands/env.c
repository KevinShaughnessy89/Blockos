#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
extern char **environ;
int main(int argc,char **argv){int i=1;while(i<argc&&strchr(argv[i],'=')&&argv[i][0]!='='){char *eq=strchr(argv[i],'=');*eq=0;setenv(argv[i],eq+1,1);*eq='=';++i;}if(i<argc){execvp(argv[i],&argv[i]);write(2,"env: command failed\n",20);return 127;}for(char **p=environ;p&&*p;p++){write(1,*p,strlen(*p));write(1,"\n",1);}return 0;}
