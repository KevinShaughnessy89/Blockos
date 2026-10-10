#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(int argc,char**argv){FILE*f=argc>1?fopen(argv[1],"r"):stdin;if(!f)return 1;char prev[2048],line[2048];int have=0;while(fgets(line,sizeof line,f)){if(!have||strcmp(prev,line)){write(1,line,strlen(line));strcpy(prev,line);have=1;}}if(f!=stdin)fclose(f);return 0;}
