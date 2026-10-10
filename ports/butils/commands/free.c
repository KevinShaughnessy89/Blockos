#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static unsigned long value(const char*s){while(*s&&*s!=':')s++;if(*s==':')s++;while(*s==' '||*s=='\t')s++;return (unsigned long)strtol(s,0,10);}
int main(void){FILE*f=fopen("/proc/meminfo","r");if(!f){write(2,"free: /proc/meminfo unavailable\n",32);return 1;}unsigned long total=0,freeb=0,avail=0;char line[256];while(fgets(line,sizeof line,f)){if(!strncmp(line,"MemTotal:",9))total=value(line);else if(!strncmp(line,"MemFree:",8))freeb=value(line);else if(!strncmp(line,"MemAvailable:",13))avail=value(line);}fclose(f);printf("total used free available (kB)\n");printf("Mem: %lu %lu %lu %lu\n",total,total>freeb?total-freeb:0,freeb,avail);return 0;}
