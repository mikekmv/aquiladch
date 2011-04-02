/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#include "esocket.h"

#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef POLL
#include <sys/poll.h>
#endif

#define ASSERT assert

#ifdef DEBUG
#define DPRINTF printf
#else
#define DPRINTF(...)
#endif

esocket_t *freelist;

/*
 * Handler functions
 */
esocket_handler_t *esocket_create_handler (unsigned int numtypes)
{
  esocket_handler_t *h;

  h = malloc (sizeof (esocket_handler_t));
  if (!h)
    return NULL;
  memset (h, 0, sizeof (esocket_handler_t));

  h->types = malloc (sizeof (esocket_type_t) * numtypes);
  if (!h->types) {
    free (h);
    return NULL;
  }
  memset (h->types, 0, sizeof (esocket_type_t) * numtypes);
  h->numtypes = numtypes;

  /* default timeout 1 hour ie, no timeout */
  h->toval.tv_sec = 3600;

  initRoot (&h->root);

#ifdef USE_EPOLL
  h->epfd = epoll_create (ESOCKET_MAX_FDS);
#endif

  return h;
}

unsigned int esocket_add_type (esocket_handler_t * h, unsigned int events, input_handler_t input,
			       output_handler_t output, error_handler_t error,
			       timeout_handler_t timeout)
{
  if (h->curtypes == h->numtypes)
    return -1;

  h->types[h->curtypes].type = h->curtypes;
  h->types[h->curtypes].input = input;
  h->types[h->curtypes].output = output;
  h->types[h->curtypes].error = error;
  h->types[h->curtypes].timeout = timeout;

  h->types[h->curtypes].default_events = events;

  return h->curtypes++;
}


/*
 * Socket functions
 */

unsigned int esocket_setevents (esocket_t * s, unsigned int events)
{
#ifdef USE_EPOLL
  esocket_handler_t *h = s->handler;

  if (events != s->events) {
    struct epoll_event ee;

    ee.events = events;
    ee.data.ptr = s;
    epoll_ctl (h->epfd, events ? (s->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD) : EPOLL_CTL_DEL,
	       s->socket, &ee);
    s->events = events;
  }
#else
#ifdef POLL
  s->events = events;
#else
  esocket_handler_t *h = s->handler;
  unsigned int e = ~s->events & events;

  if (e & ESOCKET_EVENT_IN) {
    FD_SET (s->socket, &h->input);
    h->ni++;
  };
  if (e & ESOCKET_EVENT_OUT) {
    FD_SET (s->socket, &h->output);
    h->no++;
  };
  if (e & ESOCKET_EVENT_ERR) {
    FD_SET (s->socket, &h->error);
    h->ne++;
  };

  e = s->events & events;
  if (e & ESOCKET_EVENT_IN) {
    FD_CLR (s->socket, &h->input);
    h->ni--;
  };
  if (e & ESOCKET_EVENT_OUT) {
    FD_CLR (s->socket, &h->output);
    h->no--;
  };
  if (e & ESOCKET_EVENT_ERR) {
    FD_CLR (s->socket, &h->error);
    h->ne--;
  };

  s->events = events;
#endif
#endif
  return 0;
}

unsigned int esocket_addevents (esocket_t * s, unsigned int events)
{
  esocket_handler_t *h = s->handler;

#ifdef USE_EPOLL
  if ((events | s->events) != s->events) {
    struct epoll_event ee;

    memset (&ee, 0, sizeof (ee));
    ee.events = s->events | events;
    ee.data.ptr = s;
    epoll_ctl (h->epfd, ee.events ? (s->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD) : EPOLL_CTL_DEL,
	       s->socket, &ee);
    s->events = ee.events;
  }
#else
#ifdef POLL
  s->events |= events;
#else
  unsigned int e = ~s->events & events;

  if (e & ESOCKET_EVENT_IN) {
    FD_SET (s->socket, &h->input);
    h->ni++;
  };
  if (e & ESOCKET_EVENT_OUT) {
    FD_SET (s->socket, &h->output);
    h->no++;
  };
  if (e & ESOCKET_EVENT_ERR) {
    FD_SET (s->socket, &h->error);
    h->ne++;
  };
  s->events |= events;
#endif
#endif
  return 0;
}

unsigned int esocket_clearevents (esocket_t * s, unsigned int events)
{
  esocket_handler_t *h = s->handler;

#ifdef USE_EPOLL
  if (events & s->events) {
    struct epoll_event ee;

    memset (&ee, 0, sizeof (ee));
    ee.events = s->events & ~events;
    ee.data.ptr = s;
    epoll_ctl (h->epfd, ee.events ? (s->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD) : EPOLL_CTL_DEL,
	       s->socket, &ee);
    s->events = ee.events;
  }
#else
#ifdef POLL
  s->events = s->events & ~events;
#else
  unsigned int e = s->events & events;

  if (e & ESOCKET_EVENT_IN) {
    FD_CLR (s->socket, &h->input);
    h->ni--;
  };
  if (e & ESOCKET_EVENT_OUT) {
    FD_CLR (s->socket, &h->output);
    h->no--;
  };
  if (e & ESOCKET_EVENT_ERR) {
    FD_CLR (s->socket, &h->error);
    h->ne--;
  };
  s->events &= ~events;
#endif
#endif

  return 0;
}

unsigned int esocket_settimeout (esocket_t * s, unsigned long timeout)
{
  unsigned long long key;
  esocket_handler_t *h = s->handler;

  if (s->tovalid)
    esocket_deltimeout (s);

  if (!timeout)
    return 0;

  gettimeofday (&s->to, NULL);

  /* determine key */
  key = (s->to.tv_sec * 1000) + (s->to.tv_usec / 1000) + timeout;

  /* create timeout */
  s->to.tv_sec += timeout / 1000;
  s->to.tv_usec += ((timeout * 1000) % 1000000);
  if (s->to.tv_usec > 1000000) {
    s->to.tv_sec++;
    s->to.tv_usec -= 1000000;
  }
  s->tovalid = 1;

  /* insert into rbt */
  s->rbt.data = key;
  insertNode (&h->root, &s->rbt);

  h->timercnt++;

  return 0;
}

unsigned int esocket_deltimeout (esocket_t * s)
{
  if (!s->tovalid)
    return 0;

  deleteNode (&s->handler->root, &s->rbt);

  s->tovalid = 0;
  s->handler->timercnt--;

  return 0;
}


unsigned int esocket_update_state (esocket_t * s, unsigned int newstate)
{
#ifdef USE_EPOLL
  uint32_t events = 0;
#endif
#ifdef POLL
  uint32_t events = 0;
#endif

  esocket_handler_t *h = s->handler;

  if (s->state == newstate)
    return 0;

  /* first, remove old state */
  switch (s->state) {
    case SOCKSTATE_INIT:
      /* nothing to remove */
      break;
    case SOCKSTATE_CONNECTING:
      /* remove the wait for connect" */
#ifndef USE_EPOLL
#ifndef POLL
      FD_CLR (s->socket, &h->output);
      h->no--;
#else
      events &= ~POLLOUT;
#endif
#else
      events &= ~EPOLLOUT;
#endif
      break;
    case SOCKSTATE_CONNECTED:
      /* remove according to requested callbacks */
#ifndef USE_EPOLL
#ifndef POLL
      if (esocket_hasevent (s, ESOCKET_EVENT_IN)) {
	FD_CLR (s->socket, &h->input);
	h->ni--;
      };
      if (esocket_hasevent (s, ESOCKET_EVENT_OUT)) {
	FD_CLR (s->socket, &h->output);
	h->no--;
      };
      if (esocket_hasevent (s, ESOCKET_EVENT_ERR)) {
	FD_CLR (s->socket, &h->error);
	h->ne--;
      };
#else
      events &= ~s->events;
#endif
#else
      events &= ~s->events;
#endif
      break;
    case SOCKSTATE_CLOSED:
    case SOCKSTATE_ERROR:
      /* nothing to remove */
      break;
  }

  s->state = newstate;

  /* add new state */
  switch (s->state) {
    case SOCKSTATE_INIT:
      /* nothing to add */
      break;
    case SOCKSTATE_CONNECTING:
      /* add to wait for connect */
#ifndef USE_EPOLL
#ifdef POLL
      events |= POLLOUT;
#else
      FD_SET (s->socket, &h->output);
      h->no++;
#endif
#else
      events |= EPOLLOUT;
#endif
      break;
    case SOCKSTATE_CONNECTED:
      /* add according to requested callbacks */
#ifndef USE_EPOLL
#ifndef POLL
      s->events = h->types[s->type].default_events;
      if (esocket_hasevent (s, ESOCKET_EVENT_IN)) {
	FD_SET (s->socket, &h->input);
	h->ni++;
      };
      if (esocket_hasevent (s, ESOCKET_EVENT_OUT)) {
	FD_SET (s->socket, &h->output);
	h->no++;
      };
      if (esocket_hasevent (s, ESOCKET_EVENT_ERR)) {
	FD_SET (s->socket, &h->error);
	h->ne++;
      };
#else
      events |= h->types[s->type].default_events;
#endif
#else
      events |= h->types[s->type].default_events;
#endif
      break;
    case SOCKSTATE_CLOSED:
    case SOCKSTATE_ERROR:
      /* nothing to add */
      break;
  }

#ifdef USE_EPOLL
  if (events != s->events) {
    struct epoll_event ee;

    ee.events = events;
    ee.data.ptr = s;
    epoll_ctl (h->epfd, ee.events ? (s->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD) : EPOLL_CTL_DEL,
	       s->socket, &ee);
    s->events = events;
  }
#endif
  return 0;
}

esocket_t *esocket_add_socket (esocket_handler_t * h, unsigned int type, int s, unsigned int state,
			       unsigned long long context)
{
  esocket_t *socket;

  if (type >= h->curtypes)
    return NULL;

#ifndef USE_EPOLL
#ifndef USE_POLL
  if (s > 1024)
    return NULL;
#endif
#endif

  socket = malloc (sizeof (esocket_t));
  if (!socket)
    return NULL;

  memset (socket, 0, sizeof (esocket_t));
  socket->type = type;
  socket->socket = s;
  socket->context = context;
  socket->handler = h;
  socket->state = SOCKSTATE_INIT;
  socket->addr = NULL;
  socket->events = 0;
  socket->tovalid = 0;

  socket->prev = NULL;
  socket->next = h->sockets;
  if (socket->next)
    socket->next->prev = socket;
  h->sockets = socket;

  if (s == -1)
    return socket;

  esocket_update_state (socket, state);

#ifndef USE_EPOLL
#ifndef POLL
  if (h->n <= s)
    h->n = s + 1;
#else
  ++(h->n);
#endif
#endif

  return socket;
}

esocket_t *esocket_new (esocket_handler_t * h, unsigned int etype, int domain, int type,
			int protocol, unsigned long long context)
{
  int fd;
  esocket_t *s;

  if (etype >= h->curtypes)
    return NULL;

  fd = socket (domain, type, protocol);
  if (fd < 0)
    return NULL;

  if (fcntl (fd, F_SETFL, O_NONBLOCK)) {
    perror ("ioctl()");
    close (fd);
    return NULL;
  };

  s = malloc (sizeof (esocket_t));
  if (!s)
    return NULL;

  s->type = etype;
  s->socket = fd;
  s->context = context;
  s->handler = h;
  s->state = SOCKSTATE_INIT;
  s->tovalid = 0;
  s->events = 0;

  s->prev = NULL;
  s->next = h->sockets;
  if (s->next)
    s->next->prev = s;
  h->sockets = s;

  if (fd == -1)
    return s;

#ifndef USE_EPOLL
#ifndef POLL
  if (h->n <= fd)
    h->n = fd + 1;
#else
  ++(h->n);
#endif
#endif

  return s;
}

unsigned int esocket_close (esocket_t * s)
{
  esocket_update_state (s, SOCKSTATE_INIT);
  close (s->socket);
  s->socket = -1;
  s->handler->n--;

  if (s->tovalid)
    esocket_deltimeout (s);

  return 0;
}

unsigned int esocket_connect (esocket_t * s, char *address, unsigned int port)
{
  int err;

  if (s->addr)
    freeaddrinfo (s->addr);

  if ((err = getaddrinfo (address, NULL, NULL, &s->addr))) {
    return err;
  }

  ((struct sockaddr_in *) s->addr->ai_addr)->sin_port = htons (port);

  err = connect (s->socket, s->addr->ai_addr, sizeof (struct sockaddr));
  if (err && (errno != EINPROGRESS)) {
    /* perror ("connect()"); */
    return -errno;
  }

  esocket_update_state (s, SOCKSTATE_CONNECTING);

  return 0;
}

unsigned int esocket_remove_socket (esocket_t * s)
{
#ifndef USE_EPOLL
  int max;
#endif
  esocket_handler_t *h;

  if (!s)
    return 0;

  assert (s->state != SOCKSTATE_FREED);

  h = s->handler;

  esocket_update_state (s, SOCKSTATE_CLOSED);

  if (s->tovalid)
    esocket_deltimeout (s);

  /* remove from list */
  if (s->next)
    s->next->prev = s->prev;
  if (s->prev) {
    s->prev->next = s->next;
  } else {
    h->sockets = s->next;
  };

  /* put in freelist */
  s->next = freelist;
  freelist = s;
  s->prev = NULL;
  s->state = SOCKSTATE_FREED;

#ifndef USE_EPOLL
#ifndef POLL
  /* recalculate fd upperlimit for select */
  max = 0;
  for (s = h->sockets; s; s = s->next)
    if (s->socket > max)
      max = s->socket;

  h->n = max + 1;
#else
  --(h->n);
#endif
#endif

  /* free memory 
     if (s->addr) {
     freeaddrinfo (s->addr);
     s = NULL;
     }
     free (s);
   */

  return 1;
}

unsigned int esocket_update (esocket_t * s, int fd, unsigned int sockstate)
{
#ifndef USE_EPOLL
  esocket_handler_t *h = s->handler;
#endif

  esocket_update_state (s, SOCKSTATE_CLOSED);
  s->socket = fd;
  esocket_update_state (s, sockstate);

  /* reset timeout */
  s->tovalid = 0;

#ifndef USE_EPOLL
  /* recalculate fd upperlimit for select */
  /* FIXME could be optimized */
  h->n = 0;
  for (s = h->sockets; s; s = s->next)
    if (s->socket > h->n)
      h->n = s->socket;
  h->n += 1;
#endif

  return 1;
}

unsigned int esocket_checktimers (esocket_handler_t * h)
{
  esocket_t *s;
  struct timeval now;
  rbt_t *rbt;

  gettimeofday (&now, NULL);

  /* handle timers */
  while ((rbt = smallestNode (&h->root))) {
    s = (esocket_t *) rbt;

    ASSERT (s->tovalid);

    if (!timercmp ((&s->to), (&now), <))
      return 0;

    s->tovalid = 0;
    deleteNode (&h->root, rbt);
    h->timercnt--;

    h->types[s->type].timeout (s);
  }
  return 0;
}

#ifndef USE_EPOLL
#ifndef POLL
/************************************************************************
**
**                             EPOLL
**
************************************************************************/
unsigned int esocket_select (esocket_handler_t * h, struct timeval *to)
{
  int num;
  esocket_t *s;
  struct timeval now;

  fd_set input, output, error;
  fd_set *i, *o, *e;

  /* prepare fdsets */
  if (h->ni) {
    memcpy (&input, &h->input, sizeof (fd_set));
    i = &input;
  } else
    i = NULL;

  if (h->no) {
    memcpy (&output, &h->output, sizeof (fd_set));
    o = &output;
  } else
    o = NULL;

  if (h->ne) {
    memcpy (&error, &h->error, sizeof (fd_set));
    e = &error;
  } else
    e = NULL;

  /* do select */
  num = select (h->n, i, o, e, to);

  /* handle sockets */
  /* FIXME could be optimized. s should not be reset to h->sockets after each try */
  /* best to keep resetting to h->sockets this allows any socket to be deleted from the callbacks */
  s = h->sockets;
  while (num > 0) {
    if (s->socket >= 0) {
      if (i && FD_ISSET (s->socket, i)) {
	FD_CLR (s->socket, i);
	s->to = now;
	if (esocket_hasevent (s, ESOCKET_EVENT_IN))
	  h->types[s->type].input (s);
	num--;
	s = h->sockets;
	continue;
      }
      if (o && FD_ISSET (s->socket, o)) {
	DPRINTF ("output event! ");
	FD_CLR (s->socket, o);
	s->to = now;
	switch (s->state) {
	  case SOCKSTATE_CONNECTED:
	    DPRINTF ("Connected and writable\n");
	    if (esocket_hasevent (s, ESOCKET_EVENT_OUT))
	      h->types[s->type].output (s);
	    break;
	  case SOCKSTATE_CONNECTING:
	    {
	      int err, len;

	      DPRINTF ("Connecting and %s!", s->error ? "error" : "connected");

	      len = sizeof (s->error);
	      err = getsockopt (s->socket, SOL_SOCKET, SO_ERROR, &s->error, &len);
	      assert (!err);
	      esocket_update_state (s, !s->error ? SOCKSTATE_CONNECTED : SOCKSTATE_ERROR);
	      if ((h->types[s->type].error) && esocket_hasevent (s, ESOCKET_EVENT_OUT))
		h->types[s->type].error (s);

	    }
	    break;
	  default:
	    assert (0);
	}
	num--;
	s = h->sockets;
	continue;
      }
      if (e && FD_ISSET (s->socket, e)) {
	FD_CLR (s->socket, e);
	s->to = now;
	if ((h->types[s->type].error) && esocket_hasevent (s, ESOCKET_EVENT_ERR))
	  h->types[s->type].error (s);
	num--;
	s = h->sockets;
	continue;
      }
    }
    s = s->next;
    if (!s) {
      DPRINTF (" All sockets tried, num still %d\n", num);
    };
  }

  /* timer stuff */
  if (h->timercnt) {
    esocket_checktimers (h);
  }

  return 0;
}
#else
/************************************************************************
**
**                             POLL()
**
************************************************************************/

unsigned int esocket_select (esocket_handler_t * h, struct timeval *to)
{
  int i, n;
  struct pollfd *pfd, *pfdi;
  esocket_t *s, **l, **li;
  struct timeval now;

  pfd = malloc (sizeof (struct pollfd) * h->n);
  l = malloc (sizeof (esocket_t *) * h->n);

  s = h->sockets;
  for (i = 0, pfdi = pfd, li = l; i < h->n; ++i, ++pfdi, ++li) {
    pfdi->fd = s->socket;
    pfdi->events = s->events;
    *li = s;
    s = s->next;
  };

  n = poll (pfd, h->n, to->tv_sec * 1000 + to->tv_usec / 1000);

  for (i = 0, pfdi = pfd, li = i; i < h->n; ++i, ++pfdi, ++li) {
    if (!pfdi->revents)
      continue;

    s = *l;
    if (pfdi->revents & (POLLERR | POLLHUP | POLLNVAL)) {
      s->to = now;
      if (h->types[s->type].error)
	h->types[s->type].error (s);
      if (s->state == SOCKSTATE_FREED)
	continue;
      if (s->socket < 0)
	continue;
    } else if (pfdi->revents & POLLIN) {
      s->to = now;
      if (h->types[s->type].input)
	h->types[s->type].input (s);
      if (s->state == SOCKSTATE_FREED)
	continue;
      if (s->socket < 0)
	continue;
    } else if (pfdi->revents & POLLOUT) {
      s->to = now;
      switch (s->state) {
	case SOCKSTATE_CONNECTED:
	  if (h->types[s->type].output)
	    h->types[s->type].output (s);
	  break;
	case SOCKSTATE_CONNECTING:
	  {
	    int err;
	    unsigned int len;

	    len = sizeof (s->error);
	    err = getsockopt (s->socket, SOL_SOCKET, SO_ERROR, &s->error, &len);
	    assert (!err);
	    esocket_update_state (s, !s->error ? SOCKSTATE_CONNECTED : SOCKSTATE_ERROR);
	    if (h->types[s->type].error)
	      h->types[s->type].error (s);
	  }
	  break;
	case SOCKSTATE_FREED:
	default:
	  assert (0);
      }
      if (s->state == SOCKSTATE_FREED)
	continue;
      if (s->socket < 0)
	continue;
    }
  }

  free (pfd);
  free (l);

  /* timer stuff */
  if (h->timercnt)
    esocket_checktimers (h);

  /* clear freelist */
  while (freelist) {
    s = freelist;
    freelist = s->next;
    if (s->addr) {
      freeaddrinfo (s->addr);
      s = NULL;
    }
    free (s);
  }
  return 0;
}
#endif
#else
/************************************************************************
**
**                             select
**
************************************************************************/

unsigned int esocket_select (esocket_handler_t * h, struct timeval *to)
{
  int num, i;
  uint32_t activity;
  esocket_t *s;
  struct timeval now;
  struct epoll_event events[ESOCKET_MAX_FDS];

  num = epoll_wait (h->epfd, events, ESOCKET_MAX_FDS, to->tv_sec * 1000 + to->tv_usec / 1000);
  if (num < 0) {
    perror ("ESocket: epoll_wait: ");
    return -1;
  }
  gettimeofday (&now, NULL);
  now.tv_sec += h->toval.tv_sec;
  now.tv_usec += h->toval.tv_usec;
  if (now.tv_usec > 1000000) {
    now.tv_sec += now.tv_sec / 1000000;
    now.tv_usec = now.tv_usec % 1000000;
  }
  for (i = 0; i < num; i++) {
    activity = events[i].events;
    s = events[i].data.ptr;
    if (s->state == SOCKSTATE_FREED)
      continue;

    if (activity & EPOLLERR) {
      s->to = now;
      if (h->types[s->type].error)
	h->types[s->type].error (s);
      if (s->state == SOCKSTATE_FREED)
	continue;
      if (s->socket < 0)
	continue;
    }
    if (activity & EPOLLIN) {
      s->to = now;
      if (h->types[s->type].input)
	h->types[s->type].input (s);
      if (s->state == SOCKSTATE_FREED)
	continue;
      if (s->socket < 0)
	continue;
    }
    if (activity & EPOLLOUT) {
      s->to = now;
      switch (s->state) {
	case SOCKSTATE_CONNECTED:
	  if (h->types[s->type].output)
	    h->types[s->type].output (s);
	  break;
	case SOCKSTATE_CONNECTING:
	  {
	    int err;
	    unsigned int len;

	    len = sizeof (s->error);
	    err = getsockopt (s->socket, SOL_SOCKET, SO_ERROR, &s->error, &len);
	    assert (!err);
	    esocket_update_state (s, !s->error ? SOCKSTATE_CONNECTED : SOCKSTATE_ERROR);
	    if (h->types[s->type].error)
	      h->types[s->type].error (s);
	  }
	  break;
	case SOCKSTATE_FREED:
	default:
	  assert (0);
      }
      if (s->state == SOCKSTATE_FREED)
	continue;
      if (s->socket < 0)
	continue;
    }
  }

  /* timer stuff */
  if (h->timercnt)
    esocket_checktimers (h);

  /* clear freelist */
  while (freelist) {
    s = freelist;
    freelist = s->next;
    if (s->addr) {
      freeaddrinfo (s->addr);
      s = NULL;
    }
    free (s);
  }

  return 0;
}
#endif
