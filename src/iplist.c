
#include <time.h>
#include <string.h>

#include "iplist.h"
#include "aqtime.h"

unsigned long iplist_interval = IPLIST_TIME;


int iplist_add (iplist_t * list, unsigned long ip)
{
  iplistentry_t *entry;

  if ((list->free == list->first) && (list->ips[list->first].stamp != 0)) {
    list->first = ((list->first + 1) % IPLIST_SIZE);
  }

  entry = &list->ips[list->free];
  entry->stamp = now.tv_sec;
  entry->ip = ip;

  list->free = ((list->free + 1) % IPLIST_SIZE);
};

int iplist_find (iplist_t * list, unsigned long ip)
{
  unsigned int i, first = IPLIST_SIZE;
  time_t age;
  iplistentry_t *entry;

  age = now.tv_sec - iplist_interval;
  if (list->first > list->free) {
    for (i = list->first, entry = &(list->ips[list->first]); i < IPLIST_SIZE; i++) {
      if (entry->stamp < age) {
	first = i;
	continue;
      }
      if (entry->ip == ip)
	goto succes;
    }
    for (i = 0, entry = &(list->ips[0]); i < list->free; i++) {
      if (entry->stamp < age) {
	first = i;
	continue;
      }
      if (entry->ip == ip)
	goto succes;
    }
  } else {
    for (i = list->first, entry = &(list->ips[list->first]); i < list->free; i++, entry++) {
      if (entry->stamp < age) {
	first = i;
	continue;
      }
      if (entry->ip == ip)
	goto succes;
    }
  }

  return 0;

succes:
  if (first != IPLIST_SIZE)
    list->first = (first + 1) % IPLIST_SIZE;

  return 1;
};

int iplist_init (iplist_t * list)
{
  memset (list, 0, sizeof (iplist_t));
};
