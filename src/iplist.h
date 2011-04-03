#ifndef _IPLIST_H_
#define _IPLIST_H_

#include "hash.h"
#include "defaults.h"

typedef struct iplistentry {
  time_t	stamp;
  unsigned long ip;
} iplistentry_t;

typedef struct iplist {
  unsigned int first, free;
  iplistentry_t ips[IPLIST_SIZE];
} iplist_t;

extern int iplist_add (iplist_t *list, unsigned long ip);
extern int iplist_find (iplist_t *list, unsigned long ip);
extern int iplist_init (iplist_t *list);

extern unsigned long iplist_interval;

#endif
