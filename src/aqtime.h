#ifndef _AQTIME_H_

#ifdef __USE_W32_SOCKETS
#include <winsock2.h>
#else
#include <sys/time.h>
#endif

extern struct timeval now;

extern int gettime ();

#endif
