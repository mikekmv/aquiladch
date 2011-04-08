#include "dns.h"

#include <stdlib.h>
#include <string.h>

#define RESOLVE_BUFSIZE 1024

#define LOCK	 pthread_mutex_lock (&dns->mutex);
#define UNLOCK	 pthread_mutex_unlock (&dns->mutex);

/************************* RESOLVER THREAD ROUTINES **************************/

void *dns_thread (void *arg)
{
  dns_t *dns = (dns_t *) arg;
  sigset_t sigset;
  dns_request_t list, *req;

  sigemptyset (&sigset);
  pthread_sigmask (SIG_SETMASK, &sigset, NULL);

  LOCK;
  for (;;) {
    pthread_cond_wait (&dns->cond, &dns->mutex);

    if (dns->tasklist.next == &dns->tasklist)
      continue;

    for (;;) {
      /* dequeue all outstanding requests */
      list.next = dns->tasklist.next;
      list.prev = dns->tasklist.prev;
      list.next->prev = &list;
      list.prev->next = &list;
      dns->tasklist.next = &dns->tasklist;
      dns->tasklist.prev = &dns->tasklist;

      /* run the loop unlocked. */
      UNLOCK;
      for (req = list.next; req != &list; req = list.next) {
	/* dequeue element from list */
	req->next->prev = req->prev;
	req->prev->next = req->next;

	/* hey! we actually get to the resolving! */
	getaddrinfo (req->node, NULL, NULL, &req->addr);

	/* lock and queue answer */
	LOCK req->next = &dns->resultlist;
	req->prev = dns->resultlist.prev;
	req->next->prev = req;
	req->prev->next = req;
      UNLOCK}

      LOCK;
      /* if no new request have been queued, break out */
      if (dns->tasklist.next == &dns->tasklist)
	break;
    }
  }
  UNLOCK;
}

/*************************** MAIN THREAD ROUTINES ****************************/

int dns_resolve (dns_t * dns, void *ctxt, unsigned char *name)
{
  dns_request_t *req;

  req = malloc (sizeof (dns_request_t));
  if (!req)
    return -1;

  memset (req, 0, sizeof (dns_request_t));
  req->node = strdup (name);
  req->ctxt = ctxt;

  LOCK;

  req->next = &dns->tasklist;
  req->prev = dns->tasklist.prev;
  req->next->prev = req;
  req->prev->next = req;

  pthread_cond_signal (&dns->cond);

  UNLOCK;

  return 0;
};

void *dns_retrieve (dns_t * dns, struct addrinfo **addr)
{
  void *ret = NULL;
  dns_request_t *req;

  LOCK;
  req = dns->resultlist.next;
  if (req != &dns->resultlist) {
    req->next->prev = req->prev;
    req->prev->next = req->next;
  } else {
    req = NULL;
  }
  UNLOCK;

  if (!req)
    return NULL;

  *addr = req->addr;
  free (req->node);
  ret = req->ctxt;
  free (req);

  return ret;
}

dns_t *dns_init ()
{
  dns_t *dns;

  dns = malloc (sizeof (dns_t));
  if (!dns)
    return NULL;

  memset (dns, 0, sizeof (dns_t));
  pthread_mutex_init (&dns->mutex, NULL);
  pthread_cond_init (&dns->cond, NULL);

  dns->tasklist.next = &dns->tasklist;
  dns->tasklist.prev = &dns->tasklist;

  dns->resultlist.next = &dns->resultlist;
  dns->resultlist.prev = &dns->resultlist;

  pthread_create (&dns->thread, NULL, dns_thread, (void *) dns);

  return dns;
}