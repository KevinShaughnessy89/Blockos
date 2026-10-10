#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int selected(const char *spec,int field){while(*spec){char *e;long a=strtol(spec,&e,10);if(e==spec)return 0;long b=a;spec=e;if(*spec=='-'){++spec;if(*spec>='0'&&*spec<='9'){b=strtol(spec,&e,10);spec=e;}}if(field>=a&&field<=b)return 1;if(*spec==',')++spec;else if(*spec)return 0;}return 0;}
int main(int argc,char **argv){char delim='\t';const char *fields=0,*file=0;int i=1;while(i<argc&&argv[i][0]=='-'){if(!strcmp(argv[i],"-d")&&i+1<argc){delim=argv[i+1][0];i+=2;}else if(!strcmp(argv[i],"-f")&&i+1<argc){fields=argv[i+1];i+=2;}else{write(2,"Usage: cut -f LIST [-d CHAR] [FILE]\n",36);return 2;}}if(!fields){write(2,"cut: -f is required\n",20);return 2;}if(i<argc)file=argv[i];FILE *f=file?fopen(file,"r"):stdin;if(!f){write(2,"cut: cannot open file\n",22);return 1;}char line[4096];while(fgets(line,sizeof line,f)){int field=1;char *p=line,*start=line;for(;;){if(*p==delim||*p=='\n'||*p=='\0'){char save=*p;*p=0;if(selected(fields,field)){if(field>1){}write(1,start,strlen(start));if(save==delim)write(1,&delim,1);}if(save=='\n'||save==0)break;start=p+1;++field;}++p;}}if(file)fclose(f);return 0;}
