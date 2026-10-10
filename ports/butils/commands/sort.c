#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(int argc,char**argv){int rev=0,i=1;if(i<argc&&!strcmp(argv[i],"-r")){rev=1;i++;}FILE*f=i<argc?fopen(argv[i],"r"):stdin;if(!f)return 1;char**lines=0;size_t n=0,cap=0;char b[1024];while(fgets(b,sizeof b,f)){if(n==cap){size_t nc=cap?cap*2:32;char**q=realloc(lines,nc*sizeof *q);if(!q)return 1;lines=q;cap=nc;}lines[n]=strdup(b);if(!lines[n])return 1;n++;}if(f!=stdin)fclose(f);for(size_t x=1;x<n;x++){char*t=lines[x];size_t y=x;while(y>0&&((strcmp(lines[y-1],t)>0)^rev)){lines[y]=lines[y-1];y--;}lines[y]=t;}for(size_t x=0;x<n;x++){write(1,lines[x],strlen(lines[x]));free(lines[x]);}free(lines);return 0;}
