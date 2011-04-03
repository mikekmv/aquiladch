#ifndef _DNS_H_
#define _DNS_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>

#define HAVE_GETHOSTBYNAME_R
                     
typedef struct dns_request dns_request_t;

struct dns_request {
  struct dns_request *next, *prev;
  unsigned char *node;
  struct addrinfo *addr;
  unsigned int error;
  void * ctxt;
};

typedef struct dns {
  pthread_t thread;

  /* sync objects */
  pthread_mutex_t  mutex;
  pthread_cond_t   cond;
  
  /* protected tasklists */
  dns_request_t tasklist;
  dns_request_t resultlist;
} dns_t;

extern int dns_resolve (dns_t *dns, void * ctxt, unsigned char *name);
extern void *dns_retrieve (dns_t *, struct addrinfo **addr);
extern dns_t * dns_init ();

#endif
