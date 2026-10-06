#include "sys/select.h"
#include "sys/poll.h"
#include "errno.h"

int select(int nfds,fd_set*rf,fd_set*wf,fd_set*ef,struct timeval*tv){
 struct pollfd p[FD_SETSIZE];int n=0,i,ms=-1,ready=0;
 if(tv){long long x=(long long)tv->tv_sec*1000+tv->tv_usec/1000;ms=x<0?0:(x>2147483647LL?2147483647:(int)x);}
 for(i=0;i<nfds&&i<FD_SETSIZE;i++){short e=0;if(rf&&FD_ISSET(i,rf))e|=POLLIN;if(wf&&FD_ISSET(i,wf))e|=POLLOUT;if(ef&&FD_ISSET(i,ef))e|=POLLERR;if(e){p[n].fd=i;p[n].events=e;p[n].revents=0;++n;}}
 int r=poll(p,(size_t)n,ms);if(r<0)return -1;if(r==0){if(rf)FD_ZERO(rf);if(wf)FD_ZERO(wf);if(ef)FD_ZERO(ef);return 0;}
 if(rf)FD_ZERO(rf);if(wf)FD_ZERO(wf);if(ef)FD_ZERO(ef);
 for(i=0;i<n;i++){short e=p[i].revents;if(e&(POLLIN|POLLHUP|POLLERR)){if(rf)FD_SET(p[i].fd,rf);}if(e&(POLLOUT|POLLERR)){if(wf)FD_SET(p[i].fd,wf);}if(e&(POLLERR|POLLHUP)){if(ef)FD_SET(p[i].fd,ef);}if(e)++ready;}
 return ready;
}
