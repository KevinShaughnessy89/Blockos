#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
static int contains(const char *s,const char *p,int fold){if(!*p)return 1;for(;*s;s++){const char*a=s,*b=p;while(*a&&*b){char x=*a,y=*b;if(fold&&x>='A'&&x<='Z')x+=32;if(fold&&y>='A'&&y<='Z')y+=32;if(x!=y)break;a++;b++;}if(!*b)return 1;}return 0;}
int main(int argc,char**argv){int number=0,inv=0,fold=0,i=1;while(i<argc&&argv[i][0]=='-'&&argv[i][1]){if(!strcmp(argv[i],"-n"))number=1;else if(!strcmp(argv[i],"-v"))inv=1;else if(!strcmp(argv[i],"-i"))fold=1;else break;i++;}if(i>=argc){write(2,"Usage: grep [-nvi] PATTERN [FILE ...]\n",38);return 2;}const char*pat=argv[i++];int files=argc-i,rc=1;if(!files){char line[2048];long ln=0;while(fgets(line,sizeof line,stdin)){ln++;int hit=contains(line,pat,fold);if(hit!=inv){if(number)printf("%ld:",ln);write(1,line,strlen(line));rc=0;}}return rc;}for(;i<argc;i++){FILE*f=fopen(argv[i],"r");if(!f){printf("grep: cannot open %s\n",argv[i]);continue;}char line[2048];long ln=0;while(fgets(line,sizeof line,f)){ln++;if(contains(line,pat,fold)!=inv){if(files>1)printf("%s:",argv[i]);if(number)printf("%ld:",ln);write(1,line,strlen(line));rc=0;}}fclose(f);}return rc;}
