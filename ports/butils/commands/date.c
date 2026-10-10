#include <unistd.h>
int main(int argc,char **argv){(void)argv;if(argc>1){const char m[]="Usage: date\n";write(2,m,sizeof m-1);return 2;}const char m[]="date: BlockOS kernel currently exposes uptime, not wall-clock/RTC time\n";write(2,m,sizeof m-1);return 1;}
