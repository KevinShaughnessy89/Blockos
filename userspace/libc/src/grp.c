#include "grp.h"
#include <string.h>
static char *root_members[] = { 0 };
static struct group root_gr;
static char root_name[]="root";
struct group *getgrgid(unsigned long gid){ if(gid!=0) return 0; root_gr=(struct group){root_name,(char*)"x",0,root_members}; return &root_gr; }
struct group *getgrnam(const char *name){ if(!name||strcmp(name,"root")!=0) return 0; return getgrgid(0); }
