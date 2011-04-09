/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (jove@users.berlios.de)                                                                      
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

#include "esocket.h"

#ifndef USE_IOCP
#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif
#endif

#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

#ifdef USE_POLL
#include <sys/poll.h>
#endif

#ifdef USE_IOCP
#include <io.h>
#include <ws2tcpip.h>
#include <mswsock.h>

#if (_WIN32_WINNT <= 0x0501)
void WSAAPI freeaddrinfo (struct addrinfo *);
int WSAAPI getaddrinfo (const char *, const char *, const struct addrinfo *, struct addrinfo **);
int WSAAPI getnameinfo (const struct sockaddr *, socklen_t, char *, DWORD, char *, DWORD, int);
#endif

#endif

#ifndef ASSERT
#  ifdef DEBUG
#    define ASSERT assert
#  else
#    define ASSERT(...)
#  endif
#endif

#ifndef DPRINTF
#  ifdef DEBUG
#     define DPRINTF printf
#  else
#    define DPRINTF(...)
#  endif
#endif

esocket_t *freelist = NULL;

#ifdef USE_IOCP

//extern BOOL WSAAPI ConnectEx(SOCKET, const struct sockaddr*, int, PVOID, DWORD, LPDWORD, LPOVERLAPPED);


/******************************** IOCP specific stuff *******************************************/

/*
 *  These settings control the maximum writes outstanding per connection.
 *  the minimum write is 1/2 IOCP_COMPLETION_SIZE_MIN and max is 1/2 IOCP_COMPLETION_SIZE_MAX
 *  the MAX will use all outstanding locked memory for 1000 users and 1Gb memory
 *  the MIN was chosen at 2 tcp packets per fragment for throughput reasons. should support more than 10k users at 1Gb
 */

#define IOCP_COMPLETION_FRAGMENTS 2
#define IOCP_COMPLETION_SIZE_MAX      131071
#define IOCP_COMPLETION_SIZE_MIN      2*1460*IOCP_COMPLETION_FRAGMENTS

#define close(x) closesocket(x)

#define IOCTX_FREE(ctxt) { ioctx_free (ctxt); free(ctxt); }
#define SYNC_ERRORS(socket) { errno = socket->error = translate_error(WSAGetLastError()); }

/* 
 *   connect polling interval
 */
#define IOCP_CONNECT_INTERVAL  250

/*
 * memory autotuning 
 */

/* here we keep track of the max outstanding request we have been able to do.
 * this is meant to represent the non-paged pool usage.
 *  If this becomes a problem we can adjust the pool with SetProcessWorkingSetSize
 */
#define OUTSTANDING_CHECK (outstanding < outstanding_max)
#define OUTSTANDING_INC	{ outstanding++; if (outstanding > outstanding_peak) outstanding_peak = outstanding; }
#define OUTSTANDING_DEC  { outstanding--; };
#define OUTSTANDING_FAILED { outstanding_max = outstanding_peak = outstanding; }

unsigned long outstanding = 0;
unsigned long outstanding_peak = 0;
unsigned long outstanding_max = ULONG_MAX;

/* here we keep track of the max outstanding write bytes we have been able to do.
 * this is meant to give us a good idea of the amount of memory we can lock.
 * we will use this to tune the outstanding bytes/connection
 */
#define OUTSTANDINGBYTES_CHECK(size) ( (outstandingbytes+size) < outstandingbytes_max)
#define OUTSTANDINGBYTES_INC(size)	{ outstandingbytes+=size; if (outstandingbytes > outstandingbytes_peak) { outstandingbytes_peak = outstandingbytes; USERS_UPDATE; }; }
#define OUTSTANDINGBYTES_DEC(size)  { outstandingbytes-=size; };
#define OUTSTANDINGBYTES_FAILED { outstandingbytes_max = outstandingbytes_peak = outstandingbytes; USERS_UPDATE; }

unsigned long outstandingbytes = 0;
unsigned long outstandingbytes_peak = 0;
unsigned long outstandingbytes_max = ULONG_MAX;

#define USERS_UPDATE	{ if (iocp_users) outstandingbytes_peruser = outstandingbytes_max / iocp_users; if (outstandingbytes_peruser < IOCP_COMPLETION_SIZE_MIN) outstandingbytes_peruser = IOCP_COMPLETION_SIZE_MIN; if (outstandingbytes_peruser> IOCP_COMPLETION_SIZE_MAX) outstandingbytes_peruser = IOCP_COMPLETION_SIZE_MAX; }
#define USERS_INC	{ iocp_users++; USERS_UPDATE; }
#define USERS_DEC	{ iocp_users--; USERS_UPDATE; }

unsigned long iocp_users = 0;
unsigned long outstandingbytes_peruser = IOCP_COMPLETION_SIZE_MAX;

unsigned char fake_buf;

unsigned int translate_error (unsigned int winerror)
{
  switch (winerror) {
    case WSANOTINITIALISED:
      return ENOENT;

    case WSAENETDOWN:
      /* return ENETDOWN; */
      return ENOENT;

    case WSAEAFNOSUPPORT:
      /* return EAFNOSUPPORT; */
      return EINVAL;

    case WSAEPROTONOSUPPORT:
      /* return EPROTONOSUPPORT; */
      return EINVAL;

    case WSAEPROTOTYPE:
      /* return EPROTOTYPE; */
      return EINVAL;

    case WSAEWOULDBLOCK:
      return EAGAIN;

    case WSAEINPROGRESS:
      return EAGAIN;

    case WSAEMFILE:
      return EMFILE;

    case WSAENOBUFS:
      return ENOMEM;

    case WSAESOCKTNOSUPPORT:
      /* return ESOCKTNOSUPPORT; */
      return EINVAL;

    case WSAEINVAL:
      return EINVAL;

    case WSAEFAULT:
      return EFAULT;

    case WSAECONNABORTED:
      /* return ECONNABORTED; */
      return EPIPE;

    case WSAECONNRESET:
      /* return ECONNRESET; */
      return EPIPE;

    case WSAEDISCON:
      /* return ENOTCONN; */
      return EPIPE;

    case WSAEINTR:
      return EINTR;

    case WSAENETRESET:
      /* return ETIMEDOUT; */
      return EPIPE;

    case WSAENOTSOCK:
      /* return ENOTSOCK; */
      return EBADF;

    case WSA_IO_PENDING:
      /* return EINPROGRESS; */
      return EAGAIN;

    case WSA_OPERATION_ABORTED:
      return EINTR;

      /* this error is sometimes returned and means the socket is dead. */
    case ERROR_NETNAME_DELETED:
      return EPIPE;

  }
  /* unknown error... safest to assume the socket is toast. */
  return EPIPE;
}


esocket_ioctx_t *ioctx_alloc (esocket_t * s, int extralen)
{
  esocket_ioctx_t *ctxt = malloc (sizeof (esocket_ioctx_t) + extralen);

  if (!ctxt)
    return NULL;

  /* init to 0 */
  memset (ctxt, 0, sizeof (esocket_ioctx_t) + extralen);
  ctxt->es = s;

  /* link in list */
  ctxt->next = s->ioclist;
  if (ctxt->next)
    ctxt->next->prev = ctxt;
  ctxt->prev = NULL;
  s->ioclist = ctxt;

  return ctxt;
}


int ioctx_free (esocket_ioctx_t * ctxt)
{

  ASSERT (ctxt->es);

  /* remove from list */
  if (ctxt->next)
    ctxt->next->prev = ctxt->prev;
  if (ctxt->prev) {
    ctxt->prev->next = ctxt->next;
  } else {
    ctxt->es->ioclist = ctxt->next;
  }

  ctxt->es = NULL;
  ctxt->next = NULL;
  ctxt->prev = NULL;

  return 0;
}

int ioctx_flush (esocket_t * s)
{
  while (s->ioclist)
    ioctx_free (s->ioclist);

  return 0;
}

/* send error up */
int es_iocp_error (esocket_t * s)
{
  esocket_handler_t *h = s->handler;

  s->error = translate_error (WSAGetLastError ());

  if (h->types[s->type].error)
    h->types[s->type].error (s);

  return s->error;
}


int es_iocp_trigger (esocket_t * s, int type)
{
  esocket_ioctx_t *ctxt;

  if (s->state == SOCKSTATE_CLOSED)
    return -1;

  ctxt = ioctx_alloc (s, 0);
  if (!ctxt)
    return -1;

  ctxt->iotype = type;

  if (!PostQueuedCompletionStatus (s->handler->iocp, 0, (ULONG_PTR) s, &ctxt->Overlapped)) {
    perror ("PostQueuedCompletionStatus (send trigger):");
    IOCTX_FREE (ctxt);
    SYNC_ERRORS (s);
    return -1;
  }

  return 0;
}


/*    Queues a new 0 byte recv
 * This has a serious limitation! when you get an input event you MUST read
 * all the queued data.
 */

int es_iocp_recv (esocket_t * s)
{
  int ret;
  WSABUF buf;
  DWORD nrRecv = 0;
  DWORD dwFlags = 0;

  esocket_ioctx_t *ctxt = ioctx_alloc (s, 0);

  if (!ctxt)
    return -1;

  /* init context */
  ctxt->iotype = ESIO_READ;

  /* init buf to zero byte length */
  buf.buf = &fake_buf;
  buf.len = 0;
  ret = WSARecv (s->socket, &buf, 1, &nrRecv, &dwFlags, &ctxt->Overlapped, NULL);
  if (ret == SOCKET_ERROR) {
    int err = WSAGetLastError ();

    /* WSA_IO_PENDING means succesfull queueing */
    if (err != WSA_IO_PENDING) {
      IOCTX_FREE (ctxt);

      if (err == WSAENOBUFS)
	OUTSTANDING_FAILED;

      es_iocp_error (s);
      return -1;
    }
  }

  OUTSTANDING_INC;

  return 0;
}

int es_iocp_accept (esocket_t * s, int family, int type, int protocol)
{
  int ret, err;
  DWORD bytesReceived = 0;

  esocket_ioctx_t *ctxt = ioctx_alloc (s, (2 * (sizeof (SOCKADDR_STORAGE) + 16)));

  if (!ctxt)
    return -1;

  ctxt->ioAccept = WSASocket (family, type, protocol, s->protinfo, 0, WSA_FLAG_OVERLAPPED);
  if (ctxt->ioAccept == INVALID_SOCKET) {
    err = WSAGetLastError ();
    if (err == WSAENOBUFS)
      OUTSTANDING_FAILED;

    perror ("WSASocket (accept):");
    IOCTX_FREE (ctxt);
    SYNC_ERRORS (s);

    es_iocp_trigger (s, ESIO_ACCEPT_TRIGGER);

    return -1;
  }

  ctxt->iotype = ESIO_ACCEPT;
  ctxt->family = family;
  ctxt->type = type;
  ctxt->protocol = protocol;

  ret = /*s->fnAcceptEx */ AcceptEx (s->socket, ctxt->ioAccept, (ctxt + 1), 0,
				     (sizeof (SOCKADDR_STORAGE) + 16),
				     (sizeof (SOCKADDR_STORAGE) + 16), &bytesReceived,
				     &ctxt->Overlapped);
  err = WSAGetLastError ();
  if ((ret == SOCKET_ERROR) && (err != ERROR_IO_PENDING)) {
    if (err == WSAENOBUFS)
      OUTSTANDING_FAILED;
    perror ("fnAcceptEx:");
    IOCTX_FREE (ctxt);
    SYNC_ERRORS (s);

    es_iocp_trigger (s, ESIO_ACCEPT_TRIGGER);

    return -1;
  }

  /* what if it returns 0? AFAIK, this is also normal. Doc says ERROR_IO_PENDING is what it should return. */
  OUTSTANDING_INC;

  return 0;
}


#endif


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

#ifdef USE_EPOLL
  h->epfd = epoll_create (ESOCKET_MAX_FDS);
#endif

#ifdef USE_IOCP
  h->iocp = CreateIoCompletionPort (INVALID_HANDLE_VALUE, NULL, (ULONG_PTR) 0, 1);
  if (h->iocp == NULL) {
    perror ("CreateIoCompletionPort (Create)");
    free (h);
    return NULL;
  }
#endif

  initRoot (&h->root);

#ifdef USE_PTHREADDNS
  h->dns = dns_init ();
#endif

  return h;
}

int esocket_add_type (esocket_handler_t * h, unsigned int events,
		      input_handler_t input, output_handler_t output,
		      error_handler_t error, timeout_handler_t timeout)
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

int esocket_setevents (esocket_t * s, unsigned int events)
{
#ifdef USE_EPOLL
  esocket_handler_t *h = s->handler;

  if (events != s->events) {
    struct epoll_event ee;

    ee.events = events;
    ee.data.ptr = s;
    epoll_ctl (h->epfd,
	       events ? (s->
			 events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD) : EPOLL_CTL_DEL, s->socket, &ee);
    s->events = events;
  }
#endif

#ifdef USE_POLL
  s->events = events;
#endif

#ifdef USE_SELECT
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

#ifdef USE_IOCP
  if ((!(s->events & ESOCKET_EVENT_IN)) && (events & ESOCKET_EVENT_IN) && (!s->protinfo))
    if (es_iocp_recv (s) < 0)
      return -1;
  s->events = events;
#endif

  return 0;
}

int esocket_addevents (esocket_t * s, unsigned int events)
{
#ifdef USE_EPOLL
  esocket_handler_t *h = s->handler;

  if ((events | s->events) != s->events) {
    struct epoll_event ee;

    memset (&ee, 0, sizeof (ee));
    ee.events = s->events | events;
    ee.data.ptr = s;
    epoll_ctl (h->epfd,
	       ee.events ? (s->
			    events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD) :
	       EPOLL_CTL_DEL, s->socket, &ee);
    s->events = ee.events;
  }
#endif

#ifdef USE_POLL
  s->events |= events;
#endif

#ifdef USE_SELECT
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
  s->events |= events;
#endif

#ifdef USE_IOCP
  if ((!(s->events & ESOCKET_EVENT_IN)) && (events & ESOCKET_EVENT_IN) && (!s->protinfo))
    if (es_iocp_recv (s) < 0)
      return -1;
  s->events |= events;
#endif

  return 0;
}

int esocket_clearevents (esocket_t * s, unsigned int events)
{
#ifdef USE_EPOLL
  esocket_handler_t *h = s->handler;

  if (events & s->events) {
    struct epoll_event ee;

    memset (&ee, 0, sizeof (ee));
    ee.events = s->events & ~events;
    ee.data.ptr = s;
    epoll_ctl (h->epfd,
	       ee.events ? (s->
			    events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD) :
	       EPOLL_CTL_DEL, s->socket, &ee);
    s->events = ee.events;
  }
#endif

#ifdef USE_POLL
  s->events = s->events & ~events;
#endif

#ifdef USE_SELECT
  esocket_handler_t *h = s->handler;
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

#ifdef USE_IOCP
  s->events &= ~events;
#endif

  return 0;
}


int esocket_update_state (esocket_t * s, unsigned int newstate)
{
#ifdef USE_EPOLL
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
#ifdef USE_SELECT
      FD_CLR (s->socket, &h->output);
      h->no--;
#endif

#ifdef USE_POLL
      s->events &= ~POLLOUT;
#endif

#ifdef USE_EPOLL
      events &= ~EPOLLOUT;
#endif

#ifdef USE_IOCP
      s->events &= ~ESOCKET_EVENT_OUT;
#endif

      break;
    case SOCKSTATE_CONNECTED:
      /* remove according to requested callbacks */
#ifdef USE_SELECT
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
#endif
#ifdef USE_POLL
      s->events = 0;
#endif

#ifdef USE_EPOLL
      events &= ~s->events;
#endif

#ifdef USE_IOCP
      s->events = 0;
#endif

      break;
    case SOCKSTATE_CLOSED:
    case SOCKSTATE_CLOSING:
    case SOCKSTATE_ERROR:
    default:
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
#ifdef USE_POLL
      s->events |= POLLOUT;
#endif

#ifdef USE_SELECT
      FD_SET (s->socket, &h->output);
      h->no++;
#endif

#ifdef USE_EPOLL
      events |= EPOLLOUT;
#endif

#ifdef USE_IOCP
      s->events |= ESOCKET_EVENT_OUT;
#endif

      break;
    case SOCKSTATE_CONNECTED:
      /* add according to requested callbacks */
#ifdef USE_SELECT
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
#endif
#ifdef USE_POLL
      s->events |= h->types[s->type].default_events;
#endif

#ifdef USE_EPOLL
      events |= h->types[s->type].default_events;
#endif

#ifdef USE_IOCP
      if ((!(s->events & ESOCKET_EVENT_IN))
	  && (h->types[s->type].default_events & ESOCKET_EVENT_IN) && (!s->protinfo))
	if (es_iocp_recv (s) < 0)
	  return -1;

      s->events |= h->types[s->type].default_events;
#endif

      break;
    case SOCKSTATE_CLOSING:
      break;

    case SOCKSTATE_CLOSED:
#ifdef USE_IOCP
      ioctx_flush (s);
      break;
#endif
    case SOCKSTATE_ERROR:
    default:
      /* nothing to add */
      break;
  }

#ifdef USE_EPOLL
  if (events != s->events) {
    struct epoll_event ee;

    ee.events = events;
    ee.data.ptr = s;
    epoll_ctl (h->epfd,
	       ee.events ? (s->
			    events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD) :
	       EPOLL_CTL_DEL, s->socket, &ee);
    s->events = events;
  }
#endif

  return 0;
}

esocket_t *esocket_add_socket (esocket_handler_t * h, unsigned int type, int s,
			       unsigned int state, uintptr_t context)
{
  esocket_t *socket;

  if (type >= h->curtypes)
    return NULL;

#ifdef USE_SELECT
  if (s >= FD_SETSIZE)
    return NULL;
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

#ifdef USE_IOCP
  {
    int len = 0;

    /* there is no sensible error handling we can do here. so we don't do any. */
    if (setsockopt (s, SOL_SOCKET, SO_SNDBUF, (char *) &len, sizeof (len)) != 0)
      goto remove;
  }

  if (CreateIoCompletionPort ((HANDLE) socket->socket, h->iocp, (ULONG_PTR) socket, 0) == NULL) {
    errno = translate_error (WSAGetLastError ());
    goto remove;
  }

  USERS_INC;
#endif

  if (esocket_update_state (socket, state) < 0)
    goto remove;

#ifdef USE_SELECT
  if (h->n <= s)
    h->n = s + 1;
#endif

#ifdef USE_POLL
  ++(h->n);
#endif

  return socket;

remove:
  h->sockets = socket->next;
  if (h->sockets)
    h->sockets->prev = NULL;
  free (socket);
  return NULL;
}

esocket_t *esocket_new (esocket_handler_t * h, unsigned int etype, int domain, int type,
			int protocol, uintptr_t context)
{
  int fd;
  esocket_t *s;

  if (etype >= h->curtypes)
    return NULL;

#ifndef USE_IOCP
  fd = socket (domain, type, protocol);
  if (fd < 0)
    return NULL;
#else
  fd = WSASocket (domain, type, protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
  if ((SOCKET) fd == INVALID_SOCKET) {
    errno = translate_error (WSAGetLastError ());
    return NULL;
  }
#endif

#ifndef USE_IOCP
  if (fcntl (fd, F_SETFL, O_NONBLOCK)) {
    perror ("ioctl()");
    close (fd);
    return NULL;
  };
#else
  {
    DWORD yes = 1;
    DWORD bytes = 0;

    if (WSAIoctl (fd, FIONBIO, &yes, sizeof (yes), NULL, 0, &bytes, NULL, NULL)) {
      errno = translate_error (WSAGetLastError ());
      perror ("WSAIoctl:");
      close (fd);
      return NULL;
    }
  }
#endif
  s = malloc (sizeof (esocket_t));
  if (!s)
    return NULL;

  memset (s, 0, sizeof (esocket_t));
  s->type = etype;
  s->socket = fd;
  s->context = context;
  s->handler = h;
  s->state = SOCKSTATE_INIT;
  s->tovalid = 0;
  s->events = 0;
  s->addr = NULL;

  s->prev = NULL;
  s->next = h->sockets;
  if (s->next)
    s->next->prev = s;
  h->sockets = s;


  if (fd == -1)
    return s;

#ifdef USE_IOCP
  {
    int len = 0;

    if (setsockopt (fd, SOL_SOCKET, SO_SNDBUF, (char *) &len, sizeof (len)))
      goto remove;
  }

  if (CreateIoCompletionPort ((HANDLE) s->socket, h->iocp, (ULONG_PTR) s, 0) == NULL) {
    errno = translate_error (WSAGetLastError ());
    goto remove;
  }

  USERS_INC;
#endif

#ifdef USE_SELECT
  if (h->n <= s)
    h->n = s + 1;
#endif

#ifdef USE_POLL
  ++(h->n);
#endif

  return s;

#ifdef USE_IOCP
remove:
  h->sockets = s->next;
  if (h->sockets)
    h->sockets->prev = NULL;
  free (s);
  close (fd);
  return NULL;
#endif
}

int esocket_close (esocket_t * s)
{
#ifdef USE_IOCP
  if (s->state == SOCKSTATE_CLOSING)
    return 0;

  if (s->fragments) {
    esocket_update_state (s, SOCKSTATE_CLOSING);
    return 0;
  }
#endif
  if (s->state == SOCKSTATE_CLOSED)
    return 0;

  esocket_update_state (s, SOCKSTATE_CLOSED);
  //FIXME shutdown (s->socket, SHUT_RDWR);
  close (s->socket);
  s->socket = INVALID_SOCKET;

  if (s->tovalid)
    esocket_deltimeout (s);

#ifdef USE_IOCP
  if (s->protinfo) {
    free (s->protinfo);
    s->protinfo = NULL;
  }
#endif

  return 0;
}

int esocket_bind (esocket_t * s, unsigned long address, unsigned int port)
{
  struct sockaddr_in a;

  /* init the socket address structure */
  memset (&a, 0, sizeof (a));
  a.sin_addr.s_addr = address;
  a.sin_port = htons (port);
  a.sin_family = AF_INET;

  /* bind the socket to the local port */
  if (bind (s->socket, (struct sockaddr *) &a, sizeof (a))) {
    perror ("bind:");
#ifdef USE_IOCP
    errno = translate_error (WSAGetLastError ());
#endif
    return -1;
  }

  return 0;
}

#ifdef USE_PTHREADDNS
int esocket_connect (esocket_t * s, char *address, unsigned int port)
{
  dns_resolve (s->handler->dns, s, address);

  /* abusing the error member to store the port. */
  s->error = port;

  esocket_update_state (s, SOCKSTATE_RESOLVING);

  return 0;
}

int esocket_connect_ai (esocket_t * s, struct addrinfo *address, unsigned int port)
{
  int err;
  struct sockaddr_in ai;

  if (s->state != SOCKSTATE_RESOLVING)
    return -1;

  if (!address) {
    s->error = ENXIO;
    if (s->handler->types[s->type].error)
      s->handler->types[s->type].error (s);
    return -1;
  }

  s->addr = address;

  ai = *((struct sockaddr_in *) address->ai_addr);
  ai.sin_port = htons (port);

  err = connect (s->socket, (struct sockaddr *) &ai, sizeof (struct sockaddr));
  esocket_update_state (s, SOCKSTATE_CONNECTING);

  return 0;
}

#else
int esocket_connect (esocket_t * s, char *address, unsigned int port)
{
  int err;

  //esocket_ioctx_t *ctxt;

  if (s->addr)
    freeaddrinfo (s->addr);

  if ((err = getaddrinfo (address, NULL, NULL, &s->addr))) {
    errno = translate_error (WSAGetLastError ());
    return -1;
  }

  ((struct sockaddr_in *) s->addr->ai_addr)->sin_port = htons (port);

/*  ctxt = ioctx_alloc (s, 0);
  if (!ctxt)
    return -1;

  ctxt->iotype = ESIO_CONNECT;

  if ((err = ConnectEx (s->socket, s->addr->ai_addr, sizeof (struct sockaddr), NULL, 0, NULL, &ctxt->Overlapped))) {
    if ((err == SOCKET_ERROR) && (ERROR_IO_PENDING != WSAGetLastError ())) {
      perror ("ConnectEx:");
      IOCTX_FREE (ctxt);
      es_iocp_error (s);
      return -1;
    }
  }
*/

  err = connect (s->socket, s->addr->ai_addr, sizeof (struct sockaddr));
  if (err == SOCKET_ERROR) {
    err = WSAGetLastError ();
    if (err && (err != WSAEWOULDBLOCK)) {
      errno = translate_error (err);
      return -1;
    }
  }

  esocket_update_state (s, SOCKSTATE_CONNECTING);

  return 0;
}


int esocket_check_connect (esocket_t * s)
{
  esocket_handler_t *h = s->handler;
  int err, len;
  struct sockaddr sa;

  s->error = 0;
  len = sizeof (sa);
  err = getpeername (s->socket, &sa, &len);
  if (err == SOCKET_ERROR) {
    err = WSAGetLastError ();
    if (err && (err != WSAEWOULDBLOCK)) {
      s->error = translate_error (err);
      goto error;
    }
    return -1;
  }


  len = sizeof (s->error);
  err = getsockopt (s->socket, SOL_SOCKET, SO_ERROR, (void *) &s->error, &len);
  if (err) {
    err = WSAGetLastError ();
    s->error = translate_error (err);
    goto error;
  }

  if (s->error != 0) {
    s->error = translate_error (s->error);
    goto error;
  }

  DPRINTF ("Connecting and %s!\n", s->error ? "error" : "connected");

  err = s->error;
  esocket_update_state (s, !s->error ? SOCKSTATE_CONNECTED : SOCKSTATE_ERROR);

  if (!s->error) {
    if (h->types[s->type].output)
      h->types[s->type].output (s);
    return 0;
  }

error:
  if (h->types[s->type].error)
    h->types[s->type].error (s);

  return -1;
}

#endif

/*
#else

int es_iocp_resolv (esocket_ioctx_t *ctxt) {
  esocket_t *s = ctxt->es;
  int err;
  esocket_ioctx_t *ctxt_connect;

  ctxt_connect = ioctx_alloc (s, 0);
  if (!ctxt_connect)
    return -1;

  ctxt->iotype = ESIO_CONNECT;

  ((struct sockaddr_in *) s->addr->ai_addr)->sin_port = htons (ctxt->port);

  if ((err = ConnectEx (s->socket, s->addr->ai_addr, sizeof (struct sockaddr), NULL, 0, NULL, &ctxt->Overlapped)) {
    if ((err == SOCKET_ERROR) && (ERROR_IO_PENDING != WSAGetLastError ())) {
      perror ("ConnectEx:");
      es_iocp_error (s);
      return -1;
    }
  }
  esocket_update_state (s, SOCKSTATE_CONNECTING);

  return 0;
}

int esocket_connect (esocket_t *s, char *address, unsigned int port) {
  struct esocket_ioctx_t *ctxt;
  
  int err;
  
  if (s->addr) {
    FreeAddrInfoEx (s->addr);
    s->addr = NULL;
  }
  
  ctxt = ioctx_alloc (s, 0);
  if (!ctxt)
    return -1;
  
  ctxt->iotype = ESIO_RESOLVE;
  ctxt->port   = port;
  
  if ((err = GetAddrInfoEx (address, NULL, NS_DNS, NULL, NULL, &s->addr, NULL, &ctx->Overlapped, NULL, NULL)) {
    if ((err == SOCKET_ERROR) && (ERROR_IO_PENDING != WSAGetLastError ())) {
      perror ("GetAddrInfoEx:");
      return -1;
    }
  };
  esocket_update_state (s, SOCKSTATE_RESOLVING);
  
  return 0;
}

#endif
*/
int esocket_remove_socket (esocket_t * s)
{
#ifdef USE_SELECT
  int max;
#endif
  esocket_handler_t *h;

  if (!s)
    return 0;

  ASSERT (s->state != SOCKSTATE_FREED);

  h = s->handler;

#ifdef USE_IOCP
  if (s->state == SOCKSTATE_CLOSING)
    return 0;

  if (s->fragments) {
    esocket_update_state (s, SOCKSTATE_CLOSING);
    return 0;
  }
#endif

  if (s->state != SOCKSTATE_CLOSED)
    esocket_update_state (s, SOCKSTATE_CLOSED);

  if (s->socket != INVALID_SOCKET) {
    close (s->socket);
    s->socket = INVALID_SOCKET;
  }

  if (s->tovalid)
    esocket_deltimeout (s);

#ifdef USE_IOCP
  if (s->protinfo) {
    free (s->protinfo);
    s->protinfo = NULL;
  }
  USERS_DEC;
#endif

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

#ifdef USE_SELECT
  /* recalculate fd upperlimit for select */
  max = 0;
  for (s = h->sockets; s; s = s->next)
    if (s->socket > max)
      max = s->socket;

  h->n = max + 1;
#endif

#ifdef USE_POLL
  --(h->n);
#endif

  return 1;
}

int esocket_update (esocket_t * s, int fd, unsigned int sockstate)
{
#ifdef USE_SELECT
  esocket_handler_t *h = s->handler;
#endif

  esocket_update_state (s, SOCKSTATE_CLOSED);
  s->socket = fd;
  esocket_update_state (s, sockstate);

  /* reset timeout */
  s->tovalid = 0;

#ifdef USE_SELECT
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

/************************************************************************
**
**                             TIMERS
**
************************************************************************/
int esocket_settimeout (esocket_t * s, unsigned long timeout)
{
  unsigned long long key;
  esocket_handler_t *h = s->handler;

  if (!timeout) {
    if (s->tovalid)
      esocket_deltimeout (s);
    return 0;
  }
#ifdef USE_IOCP
  if (s->state == SOCKSTATE_CLOSING)
    return 0;

  if (s->state == SOCKSTATE_CONNECTING) {
    s->count = timeout / IOCP_CONNECT_INTERVAL;
    timeout = IOCP_CONNECT_INTERVAL;
  }
#endif

  /* if timer is valid already and the new time is later than the old, just set the reset time... 
   * it will be handled when the timer expires 
   */
  if (s->tovalid) {
    gettimeofday (&s->reset, NULL);
    s->reset.tv_sec += timeout / 1000;
    s->reset.tv_usec += ((timeout * 1000) % 1000000);
    if (s->reset.tv_usec > 1000000) {
      s->reset.tv_sec++;
      s->reset.tv_usec -= 1000000;
    }

    /* new time is later than the old */
    if (timercmp (&s->reset, &s->to, >)) {
      s->resetvalid = 1;

      return 0;
    } else {
      esocket_deltimeout (s);
    }
    s->to = s->reset;
  } else
    gettimeofday (&s->to, NULL);

  /* determine key */
  key = (s->to.tv_sec * 1000LL) + (s->to.tv_usec / 1000LL) + timeout;

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

int esocket_deltimeout (esocket_t * s)
{
  if (!s->tovalid)
    return 0;

  deleteNode (&s->handler->root, &s->rbt);

  s->resetvalid = 0;
  s->tovalid = 0;
  s->handler->timercnt--;

  return 0;
}

int esocket_checktimers (esocket_handler_t * h)
{
  esocket_t *s;
  rbt_t *rbt;
  struct timeval now;

  gettimeofday (&now, NULL);

  /* handle timers */
  while ((rbt = smallestNode (&h->root))) {
    s = (esocket_t *) rbt;

    ASSERT (s->tovalid);

    if (!timercmp ((&s->to), (&now), <))
      return 0;

    if (s->resetvalid) {
      unsigned long long key = (s->reset.tv_sec * 1000LL) + (s->reset.tv_usec / 1000LL);

      s->to = s->reset;
      s->resetvalid = 0;

      deleteNode (&h->root, rbt);

      s->rbt.data = key;
      insertNode (&h->root, rbt);
      continue;
    }

    s->tovalid = 0;
    deleteNode (&h->root, rbt);
    h->timercnt--;

#ifdef USE_IOCP
    /* connect polling */
    if (s->count) {
      if (esocket_check_connect (s) < 0) {
	unsigned long long key;

	if (s->state != SOCKSTATE_CONNECTING)
	  continue;

	key = (s->to.tv_sec * 1000LL) + (s->to.tv_usec / 1000LL) + IOCP_CONNECT_INTERVAL;

	/* create timeout */
	s->to = now;
	s->to.tv_sec += IOCP_CONNECT_INTERVAL / 1000;
	s->to.tv_usec += ((IOCP_CONNECT_INTERVAL * 1000) % 1000000);
	if (s->to.tv_usec > 1000000) {
	  s->to.tv_sec++;
	  s->to.tv_usec -= 1000000;
	}
	s->tovalid = 1;

	/* insert into rbt */
	s->rbt.data = key;
	insertNode (&h->root, &s->rbt);

	h->timercnt++;
	s->count--;
	continue;
      }
      s->count = 0;
      continue;
    }
    /* never give timeouts to closing sockets. */
    if (s->state == SOCKSTATE_CLOSING) {
      esocket_update_state (s, SOCKSTATE_CLOSED);
      esocket_remove_socket (s);
      continue;
    }
#endif

    errno = 0;
    h->types[s->type].timeout (s);
  }
  return 0;
}

/************************************************************************
**
**                             IO Functions
**
************************************************************************/

int esocket_recv (esocket_t * s, buffer_t * buf)
{
  int ret;

  if (s->state != SOCKSTATE_CONNECTED) {
    errno = ENOENT;
    return -1;
  }

  ret = recv (s->socket, buf->e, bf_unused (buf), 0);
  if (ret < 0) {
#ifdef USE_WINDOWS
    SYNC_ERRORS (s);
#endif
    return ret;
  }

  buf->e += ret;

  return ret;
}


int esocket_send (esocket_t * s, buffer_t * buf, unsigned long offset)
{
#ifndef USE_IOCP
  return send (s->socket, buf->s + offset, bf_used (buf) - offset, 0);
#else
  WSABUF wsabuf;
  int ret;
  unsigned int len, p, l, written;
  esocket_ioctx_t *ctxt;

  if (s->state != SOCKSTATE_CONNECTED) {
    errno = ENOENT;
    return -1;
  }

  if (s->fragments >= IOCP_COMPLETION_FRAGMENTS)
    goto leave;

  written = 0;
  p = outstandingbytes_peruser / IOCP_COMPLETION_FRAGMENTS;
  len = bf_used (buf) - offset;

  if (!len)
    return 0;

  while (len && (s->outstanding < IOCP_COMPLETION_SIZE_MAX)
	 && (s->fragments < IOCP_COMPLETION_FRAGMENTS)) {
    l = (len > p) ? p : len;
    wsabuf.buf = buf->s + offset;
    wsabuf.len = l;

    if (!OUTSTANDINGBYTES_CHECK (l)) {
      if (!s->fragments)
	es_iocp_trigger (s, ESIO_WRITE_TRIGGER);
      break;
    }

    /* alloc and init ioctx */
    ctxt = ioctx_alloc (s, 0);
    if (!ctxt)
      break;
    ctxt->buffer = buf;
    bf_claim (buf);
    ctxt->length = l;
    ctxt->iotype = ESIO_WRITE;

    /* send data */
    ret = WSASend (s->socket, &wsabuf, 1, NULL, 0, &ctxt->Overlapped, NULL);
    if (ret == SOCKET_ERROR) {
      int err = WSAGetLastError ();

      switch (err) {
	case WSA_IO_PENDING:
	  break;
	case WSAENOBUFS:
	  bf_free (buf);
	  IOCTX_FREE (ctxt);

	  OUTSTANDINGBYTES_FAILED;

	  if (written)
	    return written;
	  errno = translate_error (err);
	  s->error = err;

	  if (!s->fragments)
	    es_iocp_trigger (s, ESIO_WRITE_TRIGGER);

	  return -1;
	default:
	  bf_free (buf);
	  IOCTX_FREE (ctxt);
	  errno = translate_error (err);
	  s->error = err;
	  return -1;
      }
    }

    OUTSTANDING_INC;
    OUTSTANDINGBYTES_INC (l);
    written += l;
    s->outstanding += l;
    offset += l;
    len -= l;
    s->fragments++;
  }

  if (written)
    return written;

leave:
  errno = EAGAIN;
  return -1;
#endif
}


#ifndef USE_IOCP
int esocket_accept (esocket_t * s, struct sockaddr *addr, int *addrlen)
{
  return accept (s->socket, addr, addrlen);
}
#else

SOCKET esocket_accept (esocket_t * s, struct sockaddr * addr, int *addrlen)
{
  SOCKET socket;
  struct sockaddr *addr1, *addr2;
  int len1 = 0, len2 = 0;
  esocket_ioctx_t *ctxt;
  void *p;

  if (!s->ctxt)
    return INVALID_SOCKET;

  ctxt = s->ctxt;

  p = (void *) (s->ctxt + 1);
  GetAcceptExSockaddrs (p, 0, (sizeof (SOCKADDR_STORAGE) + 16),
			(sizeof (SOCKADDR_STORAGE) + 16), &addr1, &len1, &addr2, &len2);

  socket = ctxt->ioAccept;

  if (addr && *addrlen)
    memcpy (addr, addr2, *addrlen);

  // FIXME error handling?
  setsockopt (socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char *) &s->socket,
	      sizeof (s->socket));

  s->ctxt = NULL;

  es_iocp_accept (s, ctxt->family, ctxt->type, ctxt->protocol);

  return socket;
}
#endif

int esocket_listen (esocket_t * s, int num, int family, int type, int protocol)
{
#ifndef USE_IOCP
  return listen (s->socket, num);
#else
  int ret, i;

  /*
     DWORD bytes = 0;
     GUID acceptex_guid = / * WSAID_ACCEPTEX * / { 0xb5367df1, 0xcbac, 0x11cf, {0x95, 0xca, 0x00, 0x80, 0x5f, 0x48, 0xa1, 0x92}};
   */
  ret = listen (s->socket, 10);
  if (ret) {
    printf ("listen error %d\n", (int) WSAGetLastError ());
    perror ("listen");
    return ret;
  }

  i = sizeof (WSAPROTOCOL_INFO);
  s->protinfo = malloc (i);
  if (!s->protinfo) {
    errno = ENOMEM;
    return -1;
  }

  ret = getsockopt (s->socket, SOL_SOCKET, SO_PROTOCOL_INFO, (char *) s->protinfo, &i);
  if (ret == SOCKET_ERROR) {
    perror ("getsockopt (listen):");
    return -1;
  }

  /* retrieve fnAccept function *
     bytes = 0;
     ret = WSAIoctl (s->socket, SIO_GET_EXTENSION_FUNCTION_POINTER,
     &acceptex_guid,
     sizeof (acceptex_guid),
     &s->fnAcceptEx, sizeof (s->fnAcceptEx), &bytes, NULL, NULL);
     if (ret == SOCKET_ERROR) {
     printf ("failed to load AcceptEx: %d\n", WSAGetLastError ());
     return -1;
     }
   */
  for (i = 0; i < num; i++)
    es_iocp_accept (s, family, type, protocol);

  return 0;
#endif
}


#ifdef USE_SELECT
/************************************************************************
**
**                             SELECT
**
************************************************************************/
int esocket_select (esocket_handler_t * h, struct timeval *to)
{
  int num;
  esocket_t *s;

#ifdef USE_PTHREADDNS
  struct addrinfo *res;
#endif

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
  /* could be optimized. s should not be reset to h->sockets after each try BUT */
  /* best to keep resetting to h->sockets this allows any socket to be deleted from the callbacks */
  s = h->sockets;
  while (num > 0) {
    if (s->socket >= 0) {
      if (i && FD_ISSET (s->socket, i)) {
	FD_CLR (s->socket, i);
	if (esocket_hasevent (s, ESOCKET_EVENT_IN))
	  h->types[s->type].input (s);
	num--;
	s = h->sockets;
	continue;
      }
      if (o && FD_ISSET (s->socket, o)) {
	DPRINTF ("output event! ");
	FD_CLR (s->socket, o);
	switch (s->state) {
	  case SOCKSTATE_CONNECTED:
	    DPRINTF ("Connected and writable\n");
	    if (esocket_hasevent (s, ESOCKET_EVENT_OUT))
	      h->types[s->type].output (s);
	    break;
	  case SOCKSTATE_CONNECTING:
	    {
	      int err, len;

	      len = sizeof (s->error);
	      err = getsockopt (s->socket, SOL_SOCKET, SO_ERROR, &s->error, &len);
	      ASSERT (!err);

	      DPRINTF ("Connecting and %s!\n", s->error ? "error" : "connected");

	      esocket_update_state (s, !s->error ? SOCKSTATE_CONNECTED : SOCKSTATE_ERROR);
	      if (s->error) {
		if (h->types[s->type].error)
		  h->types[s->type].error (s);
	      } else {
		if (h->types[s->type].output)
		  h->types[s->type].output (s);
	      }
	    }
	    break;
	  default:
	    ASSERT (0);
	}
	num--;
	s = h->sockets;
	continue;
      }
      if (e && FD_ISSET (s->socket, e)) {
	int err;
	unsigned int len;

	len = sizeof (s->error);
	err = getsockopt (s->socket, SOL_SOCKET, SO_ERROR, &s->error, &len);
	ASSERT (!err);

	FD_CLR (s->socket, e);
	if ((h->types[s->type].error)
	    && esocket_hasevent (s, ESOCKET_EVENT_ERR))
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

#ifdef USE_PTHREADDNS
  /* dns stuff */
  while ((s = dns_retrieve (h->dns, &res))) {
    if (esocket_connect_ai (s, res, s->error) < 0)
      freeaddrinfo (res);
  }
#endif

  /* timer stuff */
  if (h->timercnt) {
    esocket_checktimers (h);
  }

  /* clear freelist */
  while (freelist) {
    s = freelist;
    freelist = s->next;
    if (s->addr) {
      freeaddrinfo (s->addr);
      s->addr = NULL;
    }
    free (s);
  }
  return 0;
}
#endif

#ifdef USE_POLL
/************************************************************************
**
**                             POLL()
**
************************************************************************/

int esocket_select (esocket_handler_t * h, struct timeval *to)
{
  int i, n;
  struct pollfd *pfd, *pfdi;
  esocket_t *s, **l, **li;

#ifdef USE_PTHREADDNS
  struct addrinfo *res;
#endif

  pfd = malloc ((sizeof (struct pollfd) * h->n) + (sizeof (esocket_t *) * h->n));
  if (!pfd)
    return -1;
  l = (esocket_t **) (((char *) pfd) + (sizeof (struct pollfd) * h->n));

  s = h->sockets;
  for (i = 0, pfdi = pfd, li = l; i < h->n; ++i, ++pfdi, ++li) {
    pfdi->fd = s->socket;
    pfdi->events = s->events;
    pfdi->revents = 0;
    *li = s;
    s = s->next;
  };

  n = poll (pfd, h->n, to->tv_sec * 1000 + to->tv_usec / 1000);
  if (n < 0) {
    perror ("esocket_select: poll:");
    goto leave;
  }
  if (!n)
    goto leave;

  /* h->n will grow when we accept users... */
  n = h->n;
  for (i = 0, pfdi = pfd, li = l; i < n; ++i, ++pfdi, ++li) {
    if (!pfdi->revents)
      continue;

    s = *li;

    if (s->state == SOCKSTATE_FREED)
      continue;

    if (pfdi->revents & (POLLERR | POLLHUP | POLLNVAL)) {
      int err;
      unsigned int len;

      len = sizeof (s->error);
      err = getsockopt (s->socket, SOL_SOCKET, SO_ERROR, &s->error, &len);
      ASSERT (!err);

      if (h->types[s->type].error)
	h->types[s->type].error (s);
      if (s->state == SOCKSTATE_FREED)
	continue;
      if (s->socket < 0)
	continue;
    }
    if (pfdi->revents & POLLIN) {
      if (h->types[s->type].input)
	h->types[s->type].input (s);
      if (s->state == SOCKSTATE_FREED)
	continue;
      if (s->socket < 0)
	continue;
    }
    if (pfdi->revents & POLLOUT) {
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
	    ASSERT (!err);
	    esocket_update_state (s, !s->error ? SOCKSTATE_CONNECTED : SOCKSTATE_ERROR);
	    if (s->error) {
	      if (h->types[s->type].error)
		h->types[s->type].error (s);
	    } else {
	      if (h->types[s->type].output)
		h->types[s->type].output (s);
	    }
	  }
	  break;
	case SOCKSTATE_FREED:
	default:
	  DPRINTF ("POLLOUT while socket in state %d\n", s->state);
	  ASSERT (0);
      }
      if (s->state == SOCKSTATE_FREED)
	continue;
      if (s->socket < 0)
	continue;
    }
  }

leave:
  free (pfd);

#ifdef USE_PTHREADDNS
  /* dns stuff */
  while ((s = dns_retrieve (h->dns, &res))) {
    if (esocket_connect_ai (s, res, s->error) < 0)
      freeaddrinfo (res);
  }
#endif

  /* timer stuff */
  if (h->timercnt)
    esocket_checktimers (h);

  /* clear freelist */
  while (freelist) {
    s = freelist;
    freelist = s->next;
    if (s->addr) {
      freeaddrinfo (s->addr);
      s->addr = NULL;
    }
    free (s);
  }
  return 0;
}
#endif

#ifdef USE_EPOLL
/************************************************************************
**
**                             EPOLL
**
************************************************************************/

int esocket_select (esocket_handler_t * h, struct timeval *to)
{
  int num, i;
  uint32_t activity;
  esocket_t *s;
  struct epoll_event events[ESOCKET_ASK_FDS];

#ifdef USE_PTHREADDNS
  struct addrinfo *res;
#endif

  num = epoll_wait (h->epfd, events, ESOCKET_ASK_FDS, to->tv_sec * 1000 + to->tv_usec / 1000);
  if (num < 0) {
    perror ("ESocket: epoll_wait: ");
    return -1;
  }

  for (i = 0; i < num; i++) {
    activity = events[i].events;
    s = events[i].data.ptr;
    if (s->state == SOCKSTATE_FREED)
      continue;

    if (activity & EPOLLHUP) {
      int err;
      unsigned int len;

      len = sizeof (s->error);
      err = getsockopt (s->socket, SOL_SOCKET, SO_ERROR, &s->error, &len);
      ASSERT (!err);

      if (h->types[s->type].error)
	h->types[s->type].error (s);

      ASSERT (s->state == SOCKSTATE_FREED);
      continue;
    }
    if (activity & EPOLLERR) {
      int err;
      unsigned int len;

      len = sizeof (s->error);
      err = getsockopt (s->socket, SOL_SOCKET, SO_ERROR, &s->error, &len);
      ASSERT (!err);

      if (h->types[s->type].error)
	h->types[s->type].error (s);
      if (s->state == SOCKSTATE_FREED)
	continue;
      if (s->socket < 0)
	continue;
    }
    if (activity & EPOLLIN) {
      if (h->types[s->type].input)
	h->types[s->type].input (s);
      if (s->state == SOCKSTATE_FREED)
	continue;
      if (s->socket < 0)
	continue;
    }
    if (activity & EPOLLOUT) {
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
	    ASSERT (!err);
	    esocket_update_state (s, !s->error ? SOCKSTATE_CONNECTED : SOCKSTATE_ERROR);
	    if (s->error) {
	      if (h->types[s->type].error)
		h->types[s->type].error (s);
	    } else {
	      if (h->types[s->type].output)
		h->types[s->type].output (s);
	    }
	  }
	  break;
	case SOCKSTATE_FREED:
	default:
	  ASSERT (0);
      }
      if (s->state == SOCKSTATE_FREED)
	continue;
      if (s->socket < 0)
	continue;
    }
  }

#ifdef USE_PTHREADDNS
  /* dns stuff */
  while ((s = dns_retrieve (h->dns, &res))) {
    if (esocket_connect_ai (s, res, s->error) < 0)
      freeaddrinfo (res);
  }
#endif

  /* timer stuff */
  if (h->timercnt)
    esocket_checktimers (h);

  /* clear freelist */
  while (freelist) {
    s = freelist;
    freelist = s->next;
    if (s->addr) {
      freeaddrinfo (s->addr);
      s->addr = NULL;
    }
    free (s);
  }

  return 0;
}
#endif

#ifdef USE_IOCP
/************************************************************************
**
**                             IOCP
**
************************************************************************/


/*
 * process a write completion.
 */
unsigned int es_iocp_send (esocket_t * s, esocket_ioctx_t * ctxt)
{
  s->outstanding -= ctxt->length;
  OUTSTANDINGBYTES_DEC (ctxt->length);
  OUTSTANDING_DEC;
  s->fragments--;
  bf_free (ctxt->buffer);
  return 0;
}


int esocket_select (esocket_handler_t * h, struct timeval *to)
{
  esocket_t *s;
  esocket_ioctx_t *ctxt;

  BOOL ret;
  DWORD bytes = 0, count = 1000;
  OVERLAPPED *ol = NULL;
  DWORD ms = (to->tv_sec * 1000) + (to->tv_usec / 1000);

  for (; count; count--) {
    s = NULL;
    ol = NULL;
    ret = GetQueuedCompletionStatus (h->iocp, &bytes, (void *) &s, &ol, ms);
    if (!ret && !ol) {
      int err = GetLastError ();

      if (err != WAIT_TIMEOUT)
	printf ("GetQueuedCompletionStatus: %d\n", err);
      goto leave;
    }
    ms = 0;
    ctxt = (esocket_ioctx_t *) ol;
    /* this is a completion for a closed socket */
    ASSERT (ctxt);

    if (!ctxt->es) {
      if (ctxt->iotype == ESIO_WRITE) {
	bf_free (ctxt->buffer);
	OUTSTANDINGBYTES_DEC (ctxt->length);
      };
      OUTSTANDING_DEC;
      free (ctxt);
      continue;
    }
    ASSERT (s == ctxt->es);

    /* this is a completion for a socket with outstanding writes */
    if (s->state == SOCKSTATE_CLOSING) {
      if (ctxt->iotype == ESIO_WRITE) {
	bf_free (ctxt->buffer);
	OUTSTANDINGBYTES_DEC (ctxt->length);
	s->fragments--;
      };
      OUTSTANDING_DEC;
      ioctx_free (ctxt);
      free (ctxt);

      if (!s->fragments) {
	esocket_update_state (s, SOCKSTATE_CLOSED);
	esocket_remove_socket (s);
      }
      continue;
    }

    /* normal socket operation */
    if (s->state == SOCKSTATE_FREED)
      goto cont;
    if (s->socket == INVALID_SOCKET)
      goto cont;

    ioctx_free (ctxt);

    if (!ret) {
      SYNC_ERRORS (s);
      es_iocp_error (s);
      /*
       * we had an error and a socket dying on us.
       * according to the docs this happens when the client disconnects
       * before we had the chance of accepting his connection.
       *   close the socket and queue a new accept.
       */
      if (ctxt->iotype == ESIO_ACCEPT) {
	close (ctxt->ioAccept);
	es_iocp_accept (s, ctxt->family, ctxt->type, ctxt->protocol);
      }
      goto cont;
    }

    switch (ctxt->iotype) {
      case ESIO_READ:
	/* we had a read conpletion 
	 */
	OUTSTANDING_DEC;

	if (s->events & ESOCKET_EVENT_IN) {
	  if (h->types[s->type].input)
	    h->types[s->type].input (s);

	  if ((s->state == SOCKSTATE_FREED) || (s->state == SOCKSTATE_CLOSING))
	    goto cont;
	  if (s->socket == INVALID_SOCKET)
	    goto cont;

	  /* queue a new recv notification */
	  es_iocp_recv (s);
	}

	break;

      case ESIO_WRITE:
	/* first, process completion */
	ret = es_iocp_send (s, ctxt);

      case ESIO_WRITE_TRIGGER:
	/* call output function if requested */
	if ((s->events & ESOCKET_EVENT_OUT) && (h->types[s->type].output))
	  h->types[s->type].output (s);

	break;

      case ESIO_ACCEPT:
	s->ctxt = ctxt;

	if (h->types[s->type].input)
	  h->types[s->type].input (s);

	s->ctxt = NULL;

	OUTSTANDING_DEC;

	break;

      case ESIO_RESOLVE:
	//es_iocp_resolve(ctxt);
	break;

      case ESIO_CONNECT:

	//setsockopt(s->socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);

	//if (h->types[s->type].input)
	//  h->types[s->type].input (s);

	break;

      case ESIO_ACCEPT_TRIGGER:
	/* try queuing another accept request */
	if (s->protinfo)
	  es_iocp_accept (s, s->protinfo->iAddressFamily, s->protinfo->iSocketType,
			  s->protinfo->iProtocol);

	break;

      default:
	ASSERT (0);
    }

    fflush (stdout);
    fflush (stderr);
  cont:
    free (ctxt);
    ctxt = NULL;
  };

leave:
  /* timer stuff */
  if (h->timercnt)
    esocket_checktimers (h);

  /* clear freelist */
  while (freelist) {
    s = freelist;
    freelist = s->next;
    if (s->addr) {
      freeaddrinfo (s->addr);
      s->addr = NULL;
    }
    free (s);
  }

  return 0;
}

#endif
