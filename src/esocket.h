/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *  
 */

#ifndef _ESOCKET_H_
#define _ESOCKET_H_

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

/*
 * Decide if we want to use epoll.
 *   We checked for: sys/epoll.h header, linux/eventpoll.h (we don't need this
 *     but if this isn't there, the kernel does not have epoll support) and 
 *     epoll_wait in the libc
 */
#ifdef ALLOW_EPOLL
# ifdef HAVE_SYS_EPOLL_H
#  ifdef HAVE_EPOLL_WAIT
#    ifdef HAVE_LINUX_EVENTPOLL_H
#      define USE_EPOLL 1
#    endif
#  endif
# endif
#endif

/*
 * Decide if we want to use poll().
 */

#ifndef USE_EPOLL
#  ifdef ALLOW_POLL
#    define POLL
#  endif
#endif

/*
 * Fallback to select(). Warn if not possible.
 */

#ifndef USE_EPOLL
#  ifndef POLL
#    ifndef HAVE_SELECT
#      warning "You are missing epoll, poll and select. This code will not work."
#    endif
#  endif
#endif

#ifdef __CYGWIN__
#define MAX_FD		16384
#define FD_SETSIZE 	16384
#endif

#include <sys/types.h>

#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
#  include <stdint.h>
# endif
#endif

#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#if !defined(HAVE_GETADDRINFO) || !defined(HAVE_GETNAMEINFO)
# include "getaddrinfo.h"
#endif

#include "rbt.h"

#ifdef USE_EPOLL
#include <sys/epoll.h>
#define ESOCKET_MAX_FDS 	16384
#define ESOCKET_ASK_FDS 	1024
#endif

#define SOCKSTATE_INIT		0
#define SOCKSTATE_CONNECTING	1
#define SOCKSTATE_CONNECTED	2
#define SOCKSTATE_CLOSED	3
#define SOCKSTATE_ERROR		4
#define SOCKSTATE_FREED		5

#ifndef USE_EPOLL

#define ESOCKET_EVENT_IN	0x0001
#define ESOCKET_EVENT_OUT	0x0004
#define ESOCKET_EVENT_ERR	0x0008
#define ESOCKET_EVENT_HUP	0x0010

#else

#define ESOCKET_EVENT_IN	EPOLLIN
#define ESOCKET_EVENT_OUT	EPOLLOUT
#define ESOCKET_EVENT_ERR	EPOLLERR
#define ESOCKET_EVENT_HUP	EPOLLHUP

#endif



/* enhanced sockets */
struct esockethandler;

typedef struct esocket {
  rbt_t rbt;
  struct esocket *next, *prev;	/* main socket list */

  int socket;
  unsigned int state;
  unsigned int error;
  unsigned long long context;
  unsigned int type;
  struct addrinfo *addr;
  uint32_t events;

  unsigned int tovalid, resetvalid;
  struct timeval to, reset;
  struct esockethandler *handler;
} esocket_t;


/* define handler functions */
typedef int (input_handler_t) (esocket_t * s);
typedef int (output_handler_t) (esocket_t * s);
typedef int (error_handler_t) (esocket_t * s);
typedef int (timeout_handler_t) (esocket_t * s);


/* socket types */
typedef struct esockettypes {
  unsigned int type;
  input_handler_t *input;
  output_handler_t *output;
  error_handler_t *error;
  timeout_handler_t *timeout;
  uint32_t default_events;
} esocket_type_t;

/* socket handler */
typedef struct esockethandler {
  esocket_type_t *types;
  unsigned int numtypes, curtypes;

  esocket_t *sockets;
  rbt_t *root;
  unsigned long timercnt;

#ifndef USE_EPOLL
#ifndef POLL
  fd_set input;
  fd_set output;
  fd_set error;

  int ni, no, ne;
#endif
#else
  int epfd;
#endif
  int n;

  struct timeval toval;
} esocket_handler_t;

/* function prototypes */
extern esocket_handler_t *esocket_create_handler (unsigned int numtypes);
extern unsigned int esocket_add_type (esocket_handler_t * h, unsigned int events,
				      input_handler_t input, output_handler_t output,
				      error_handler_t error, timeout_handler_t timeout);
extern esocket_t *esocket_new (esocket_handler_t * h, unsigned int etype, int domain, int type,
			       int protocol, unsigned long long context);
extern esocket_t *esocket_add_socket (esocket_handler_t * h, unsigned int type, int s,
				      unsigned int state, unsigned long long context);
extern unsigned int esocket_close (esocket_t * s);
extern unsigned int esocket_remove_socket (esocket_t * s);

extern int esocket_connect (esocket_t * s, char *address, unsigned int port);
extern unsigned int esocket_select (esocket_handler_t * h, struct timeval *to);
extern unsigned int esocket_update (esocket_t * s, int fd, unsigned int state);
extern unsigned int esocket_settimeout (esocket_t * s, unsigned long timeout);
extern unsigned int esocket_deltimeout (esocket_t * s);

extern unsigned int esocket_setevents (esocket_t * s, unsigned int events);
extern unsigned int esocket_addevents (esocket_t * s, unsigned int events);
extern unsigned int esocket_clearevents (esocket_t * s, unsigned int events);

#define esocket_hasevent(s,e)     (s->events & e)


/*
 *   These function allow more control over which actions to receive per socket.
 */

#endif /* _ESOCKET_H_ */
