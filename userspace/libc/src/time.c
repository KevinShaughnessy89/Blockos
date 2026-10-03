#include "time.h"
#include "sys/time.h"
#include "errno.h"
#include "string.h"
#include <stdint.h>

time_t time(time_t *out){struct timespec ts;if(clock_gettime(CLOCK_REALTIME,&ts)<0)return (time_t)-1;if(out)*out=ts.tv_sec;return ts.tv_sec;}
static struct tm gtm;
static int leap(int y){return (y%4==0&&y%100!=0)||y%400==0;}
struct tm *gmtime(const time_t*t){if(!t)return 0;int64_t days=*t/86400;int64_t rem=*t%86400;if(rem<0){rem+=86400;--days;}gtm.tm_hour=(int)(rem/3600);rem%=3600;gtm.tm_min=(int)(rem/60);gtm.tm_sec=(int)(rem%60);int64_t epoch_days=days;int y=1970;while(days>=365+leap(y)){days-=365+leap(y);++y;}while(days<0){--y;days+=365+leap(y);}gtm.tm_year=y-1900;gtm.tm_yday=(int)days;static const int md[12]={31,28,31,30,31,30,31,31,30,31,30,31};int m=0;while(m<11){int d=md[m]+(m==1&&leap(y));if(days<d)break;days-=d;++m;}gtm.tm_mon=m;gtm.tm_mday=(int)days+1;int wd=(int)((epoch_days+4)%7);if(wd<0)wd+=7;gtm.tm_wday=wd;gtm.tm_isdst=0;return &gtm;}
struct tm *localtime(const time_t*t){return gmtime(t);}
static size_t put2(char*s,size_t n,int v){if(n<3)return 0;s[0]=(char)('0'+(v/10)%10);s[1]=(char)('0'+v%10);s[2]=0;return 2;}
size_t strftime(char*s,size_t max,const char*fmt,const struct tm*t){if(!s||!max||!fmt||!t)return 0;size_t p=0;for(size_t i=0;fmt[i]&&p+1<max;++i){if(fmt[i]!='%'){s[p++]=fmt[i];continue;}char b[16];const char*txt=0;++i;if(!fmt[i])break;switch(fmt[i]){case '%':b[0]='%';b[1]=0;txt=b;break;case 'Y':{int y=t->tm_year+1900;b[0]='0'+(y/1000)%10;b[1]='0'+(y/100)%10;b[2]='0'+(y/10)%10;b[3]='0'+y%10;b[4]=0;txt=b;break;}case 'm':put2(b,sizeof(b),t->tm_mon+1);txt=b;break;case 'd':put2(b,sizeof(b),t->tm_mday);txt=b;break;case 'H':put2(b,sizeof(b),t->tm_hour);txt=b;break;case 'M':put2(b,sizeof(b),t->tm_min);txt=b;break;case 'S':put2(b,sizeof(b),t->tm_sec);txt=b;break;default:b[0]='%';b[1]=fmt[i];b[2]=0;txt=b;break;}size_t n=strlen(txt);if(p+n>=max)break;memcpy(s+p,txt,n);p+=n;}s[p]=0;return p;}
