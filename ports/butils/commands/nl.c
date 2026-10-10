#include <string.h>
#include <unistd.h>
int main(int argc,char**argv){(void)argv;if(argc>1)return 2;char line[2048];unsigned long n=0;int start=1;while(start){size_t len=0;char c;int got=0;while(read(0,&c,1)==1){got=1;if(len<sizeof line-1)line[len++]=c;if(c=='\n')break;}if(!got)break;line[len]=0;char num[24];int j=0;unsigned long x=++n;if(!len||line[0]=='\n')n--;do{num[j++]=(char)('0'+x%10);x/=10;}while(x&&j<(int)sizeof num);for(int k=0;k<6-j;k++)write(1," ",1);while(j)write(1,&num[--j],1);write(1,"  ",2);write(1,line,len);}return 0;}
