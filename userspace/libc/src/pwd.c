#include "pwd.h"
#include <string.h>
static struct passwd root_pw;
static char root_name[]="root";
static char root_home[]="/root";
static char root_shell[]="/System/bin/sh";
struct passwd *getpwuid(unsigned long uid){ if(uid!=0) return 0; root_pw=(struct passwd){root_name,(char*)"x",0,0,(char*)"root",root_home,root_shell}; return &root_pw; }
struct passwd *getpwnam(const char *name){ if(!name||strcmp(name,"root")!=0) return 0; return getpwuid(0); }
