#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void putn(const char *s, size_t n) { while (n) { ssize_t w=write(1,s,n); if(w<=0)return; s+=w; n-=(size_t)w; } }
int main(int argc, char **argv) {
    if (argc < 2) return 0;
    const char *f=argv[1]; int a=2;
    while (*f) {
        if (*f=='\\' && f[1]) { ++f; char c=*f++; if(c=='n')putn("\n",1);else if(c=='t')putn("\t",1);else if(c=='r')putn("\r",1);else if(c=='\\')putn("\\",1);else putn(&c,1); continue; }
        if (*f!='%' || !f[1]) { putn(f,1); ++f; continue; }
        ++f; if (*f=='%') { putn("%",1); ++f; continue; }
        const char *s=a<argc?argv[a++] : ""; char c=*f++;
        if(c=='s')putn(s,strlen(s));
        else if(c=='c'){ if(*s)putn(s,1); }
        else if(c=='d'||c=='i')printf("%ld",strtol(s,0,0));
        else if(c=='u'||c=='x') { long v=strtol(s,0,0); if(c=='x')printf("%lx",(unsigned long)v);else printf("%lu",(unsigned long)v); }
        else { putn("%",1); putn(&c,1); }
    }
    return 0;
}
