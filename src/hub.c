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

#include "hub.h"

#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <assert.h>
#include <string.h>

#include "../config.h"
#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif
#ifdef HAVE_NETINET_TCP_H
#  include <netinet/tcp.h>
#endif

#include "core_config.h"
#include "iplist.h"

#define HUB_INPUTBUFFER_SIZE	4096

int server_disconnect_user (client_t *, char *);

/*****************************************************************************
**                         INTERNAL HANDLERS                                **
*****************************************************************************/

/****************************** DATA SOCKETS *********************************/

int server_handle_output (esocket_t * es)
{
  client_t *cl = (client_t *) es->context;
  buffer_t *b, *n;
  long w, l;
  unsigned long t;
  string_list_entry_t *e;

  /* duplicate event */
  if (!cl->outgoing.count)
    return 0;

  DPRINTF (" %p Writing output (%u, %lu)...", cl->user, cl->outgoing.count, cl->offset);
  w = 0;
  t = 0;
  b = NULL;

  ASSERT (!(cl->outgoing.count && !cl->server->buffering));

  cl->server->buf_mem -= cl->outgoing.size;

  /* write out as much data as possible */
  for (e = cl->outgoing.first; e; e = cl->outgoing.first) {
    for (b = e->data; b; b = n) {
      /* write data */
      l = bf_used (b) - cl->offset;
      w = send (es->socket, b->s + cl->offset, l, 0);
      if (w < 0) {
	switch (errno) {
	  case EAGAIN:
	    break;
	  case EPIPE:
	    e->data = b;
	    cl->server->buf_mem += cl->outgoing.size;
	    server_disconnect_user (cl, "Connection closed.");
	    return -1;
	  default:
	    e->data = b;
	    cl->server->buf_mem += cl->outgoing.size;
	    return -1;
	}
	w = 0;
	break;
      }
      cl->server->hubstats.TotalBytesSend += w;
      t += w;
      if (w != l)
	break;
      cl->offset = 0;

      /* store "next" pointer and free buffer memory */
      n = b->next;
      cl->outgoing.size -= bf_used (b);
      bf_free_single (b);
    }
    e->data = b;
    if (b)
      break;

    string_list_del (&cl->outgoing, e);
  }
  if (cl->credit) {
    if (cl->credit > t) {
      cl->credit -= t;
    } else {
      cl->credit = 0;
    };
  }

  /* still not all data written */
  if (b) {
    if (w > 0)
      cl->offset += w;

    if ((cl->state == HUB_STATE_OVERFLOW)
	&& ((cl->outgoing.size - cl->offset) < (config.BufferSoftLimit + cl->credit))) {
      esocket_settimeout (cl->es, config.TimeoutBuffering);
      cl->state = HUB_STATE_BUFFERING;
    }

    cl->server->buf_mem += cl->outgoing.size;

    DPRINTF (" wrote %lu, still %u buffers, size %lu (offset %lu, credit %lu)\n", t,
	     cl->outgoing.count, cl->outgoing.size, cl->offset, cl->credit);
    return 0;
  }

  DPRINTF (" wrote %lu, ALL CLEAR (%u, %lu)!!\n", t, cl->outgoing.count, cl->offset);
  /* all data was written, we don't need the output event anymore */
  if (!cl->outgoing.count) {
    esocket_clearevents (cl->es, ESOCKET_EVENT_OUT);
    esocket_settimeout (cl->es, HUB_TIMEOUT_ONLINE);
    ASSERT (cl->state != HUB_STATE_NORMAL);
    cl->server->buffering--;
    cl->state = HUB_STATE_NORMAL;
  }
  ASSERT (!(cl->outgoing.count && !cl->server->buffering));
  return 0;
}

int server_handle_input (esocket_t * s)
{
  client_t *cl = (client_t *) s->context;
  buffer_t *b;
  int n, first;

  ASSERT (!(cl->outgoing.count && !cl->server->buffering));
  /* read available data */
  first = 1;
  for (;;) {
    /* alloc new buffer and read data in it. break loop if no data available */
    b = bf_alloc (HUB_INPUTBUFFER_SIZE);
    n = recv (cl->es->socket, b->s, HUB_INPUTBUFFER_SIZE, 0);
    if (n <= 0)
      break;

    cl->server->hubstats.TotalBytesReceived += n;
    first = 0;
    /* init buffer and store */
    b->e = b->s + n;
    bf_append (&cl->buffers, b);

    if (n < HUB_INPUTBUFFER_SIZE)
      break;
  };
  if (n <= 0) {
    bf_free (b);
    b = NULL;
  }

  if ((n <= 0) && first) {
    server_disconnect_user (cl, "Error on read.");
    return -1;
  }

  if (cl->buffers)
    cl->proto->handle_input (cl->user, &cl->buffers);

  return 0;
}

int server_timeout (esocket_t * s)
{
  client_t *cl = (client_t *) ((unsigned long long) s->context);

  ASSERT (!(cl->outgoing.count && !cl->server->buffering));

  /* FIXME
     /* if the client is online and has no data buffered, ignore the timeout -- this avoids disconnection in an empty hub 
     if ((cl->user->state == PROTO_STATE_ONLINE) && !cl->outgoing.size) {
     esocket_settimeout (cl->es, PROTO_TIMEOUT_ONLINE);
     return 0;
     }
   */
  DPRINTF ("Timeout user %p\n", cl->user);
  return server_disconnect_user (cl, "Timeout");
}

int server_error (esocket_t * s)
{
  client_t *cl = (client_t *) ((unsigned long long) s->context);

  ASSERT (!(cl->outgoing.count && !cl->server->buffering));

  DPRINTF ("Error on user %p\n", cl->user);
  return server_disconnect_user (cl, "Socket error");
}

/**************************** LISTENING SOCKETS ******************************/

int accept_new_user (esocket_t * s)
{
  int r, l, yes = 1;
  struct sockaddr_in client_address;
  client_t *cl;
  listener_t *listener = (listener_t *) s->context;
  server_t *server = listener->server;

  gettime ();
  for (;;) {
    memset (&client_address, 0, l = sizeof (client_address));
    r = accept (s->socket, (struct sockaddr *) &client_address, &l);

    if (r == -1) {
      if (errno == EAGAIN)
	return 0;
      perror ("accept:");
      return -1;
    }

    /* FIXME: test. we disable naggle: we do our own queueing! */
    if (setsockopt (r, IPPROTO_TCP, TCP_NODELAY, (char *) &yes, sizeof (yes)) < 0) {
      perror ("setsockopt:");
      close (r);
      return -1;
    }

    /* before all else, test hardban */
    if (banlist_find_byip (&server->hardbanlist, client_address.sin_addr.s_addr)) {
      shutdown (r, SHUT_RDWR);
      close (r);
      return -1;
    }

    /* make socket non-blocking */
    if (fcntl (r, F_SETFL, O_NONBLOCK)) {
      perror ("fcntl()");
      shutdown (r, SHUT_RDWR);
      close (r);
      return -1;
    }

    /* check last connection list */
    if (iplist_interval) {
      if (iplist_find (&server->lastlist, client_address.sin_addr.s_addr)) {
	int l;
	char buffer[256];

	l = snprintf (buffer, 256, __ ("<%s> Don't reconnect so fast.|"), HUBSOFT_NAME);
	send (r, buffer, l, 0);
	shutdown (r, SHUT_RDWR);
	close (r);
	return -1;
      }
      iplist_add (&server->lastlist, client_address.sin_addr.s_addr);
    }

    /* check available memory */
    if (server->buf_mem > config.BufferTotalLimit) {
      int l;
      char buffer[256];

      l =
	snprintf (buffer, 256, __ ("<%s> This hub is too busy, please try again later.|"),
		  HUBSOFT_NAME);
      send (r, buffer, l, 0);
      shutdown (r, SHUT_RDWR);
      close (r);

      return -1;
    }

    /* client */
    cl = malloc (sizeof (client_t));
    memset (cl, 0, sizeof (client_t));

    /* create new context */
    cl->proto = listener->proto;
    cl->server = server;
    cl->user = cl->proto->user_alloc (cl, client_address.sin_addr.s_addr);
    cl->state = HUB_STATE_NORMAL;

    /* user connection refused. */
    if (!cl->user) {
      int l;
      char buffer[256];

      l =
	snprintf (buffer, 256, __ ("<%s> This hub is too busy, please try again later.|"),
		  HUBSOFT_NAME);

      send (r, buffer, l, 0);
      shutdown (r, SHUT_RDWR);
      close (r);

      free (cl);
      return -1;
    }

    /* init some fields */
    cl->es =
      esocket_add_socket (s->handler, cl->server->es_type_server, r, SOCKSTATE_CONNECTED,
			  (unsigned long long) cl);

    if (!cl->es) {
      int l;
      char buffer[256];

      l =
	snprintf (buffer, 256, __ ("<%s> This hub is too busy, please try again later.|"),
		  HUBSOFT_NAME);

      send (r, buffer, l, 0);
      shutdown (r, SHUT_RDWR);
      close (r);

      cl->proto->user_free (cl->user);
      free (cl);
      return -1;
    }
    esocket_settimeout (cl->es, HUB_TIMEOUT_INIT);

    DPRINTF (" Accepted %s user from %s\n", cl->proto->name, inet_ntoa (client_address.sin_addr));

    /* initiate connection */
    cl->proto->handle_token (cl->user, NULL);
  }

  return 0;
}

/*****************************************************************************
**                         INTERNAL UTILITIES                               **
*****************************************************************************/
/*
 * GENERIC NETWORK FUNCTIONS
 */
int setup_incoming_socket (unsigned long address, int port)
{
  struct sockaddr_in a;
  int s, yes = 1;

  /* we create a socket */
  if ((s = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
    perror ("socket:");
    return -1;
  }
  /* we make the socket reusable, so many ppl can connect here */
  if (setsockopt (s, SOL_SOCKET, SO_REUSEADDR, (char *) &yes, sizeof (yes)) < 0) {
    perror ("setsockopt:");
    close (s);
    return -1;
  }
  /* init the socket address structure */
  memset (&a, 0, sizeof (a));
  a.sin_addr.s_addr = address;
  a.sin_port = htons (port);
  a.sin_family = AF_INET;

  /* bind the socket to the local port */
  if (bind (s, (struct sockaddr *) &a, sizeof (a)) < 0) {
    perror ("bind:");
    close (s);
    return -1;
  }

  /* make socket non-blocking */
  if (fcntl (s, F_SETFL, O_NONBLOCK)) {
    perror ("ioctl()");
    shutdown (s, SHUT_RDWR);
    close (s);
    return -1;
  }

  /* start listening on the port */
  listen (s, 10);

  DPRINTF ("Listening for clients on port %d\n", port);

  return s;
}

/*****************************************************************************
**                         EXPORTED FUNCTIONS                               **
*****************************************************************************/

/* only reset timeouts if we aren't already in a buffering state */
int server_settimeout (client_t * cl, unsigned long timeout)
{
  if (!cl)
    return 0;

  if (!cl->outgoing.count)
    return esocket_settimeout (cl->es, timeout);
  return 0;
}


int server_write_credit (client_t * cl, buffer_t * b)
{
  if (!cl)
    return 0;
  ASSERT (!(cl->outgoing.count && !cl->server->buffering));
  cl->credit += bf_size (b);
  return server_write (cl, b);
}

int server_write (client_t * cl, buffer_t * b)
{
  esocket_t *s;
  long l, w;
  unsigned long t;
  buffer_t *e;
  server_t *server = cl->server;

  if (!cl)
    return 0;

  ASSERT (!(cl->outgoing.count && !cl->server->buffering));
  ASSERT (cl->es);
  if (!(s = cl->es))
    return 0;

  /* if data is queued, queue this after it */
  if (cl->outgoing.count) {
    /* if we are still below max buffer per user, queue buffer */
    if (((cl->outgoing.size - cl->offset) < (config.BufferHardLimit + cl->credit))
	&& (server->buf_mem < config.BufferTotalLimit)) {
      server->buf_mem -= cl->outgoing.size;
      string_list_add (&cl->outgoing, cl->user, b);
      server->buf_mem += cl->outgoing.size;
      DPRINTF (" %p Buffering %d buffers, %lu.\n", cl->user, cl->outgoing.count, cl->outgoing.size);
      /* if we are over the softlimit, we go into quick timeout mode */
      if ((cl->state == HUB_STATE_BUFFERING)
	  && ((cl->outgoing.size - cl->offset) >= (config.BufferSoftLimit + cl->credit))) {
	cl->state = HUB_STATE_OVERFLOW;
	esocket_settimeout (s, config.TimeoutBuffering);
      }
    }

    /* try sending some of that data */
    server_handle_output (s);
    return 0;
  }

  /* write as much as we are able */
  w = l = t = 0;
  for (e = b; e; e = e->next) {
    l = bf_used (e);
    t += l;
    w = send (s->socket, e->s, l, 0);
    if (w < 0) {
      DPRINTF ("server_write: write: %s\n", strerror (errno));
      switch (errno) {
	case EAGAIN:
	case ENOMEM:
	  break;
	case EPIPE:
	case ECONNRESET:
	  server_disconnect_user (cl, __ ("Connection closed."));
	default:
	  return -1;
      }
      break;
    }
    cl->server->hubstats.TotalBytesSend += w;
    if (w != l)
      break;
  }
  if (w > 0)
    t += w - l;

  if (cl->credit) {
    if (cl->credit > t) {
      cl->credit -= t;
    } else {
      cl->credit = 0;
    };
  }

  /* not everything could be written to socket. store the buffer 
   * and ask esocket to notify us as the socket becomes writable 
   */
  if (e) {
    if (w < 0)
      w = 0;

    if (!cl->outgoing.count) {
      ASSERT (cl->state == HUB_STATE_NORMAL);
      server->buffering++;
    }

    string_list_add (&cl->outgoing, cl->user, e);
    server->buf_mem += cl->outgoing.size;
    cl->offset = w;
    esocket_addevents (s, ESOCKET_EVENT_OUT);

    if (cl->state == HUB_STATE_NORMAL) {
      if ((cl->outgoing.size - cl->offset) < (config.BufferSoftLimit + cl->credit)) {
	cl->state = HUB_STATE_BUFFERING;
	esocket_settimeout (s, config.TimeoutBuffering);
      } else {
	cl->state = HUB_STATE_OVERFLOW;
	esocket_settimeout (s, config.TimeoutOverflow);
      }
    }
    DPRINTF (" %p Starting to buffer... %lu (credit %lu)\n", cl->user, cl->outgoing.size,
	     cl->credit);
  };

  ASSERT (!(cl->outgoing.count && !cl->server->buffering));
  return t;
}

int server_disconnect_user (client_t * cl, char *reason)
{
  int s;
  server_t *server;

  if (!cl)
    return 0;

  server = cl->server;

  ASSERT (!(cl->outgoing.count && !cl->server->buffering));

  /* first disconnect on protocol level: parting messages can be written */
  cl->proto->user_disconnect (cl->user, reason);

  /* close the real socket */
  if (cl->es) {
    s = cl->es->socket;
    esocket_remove_socket (cl->es);
    cl->es = NULL;
    close (s);
  }

  if (cl->outgoing.count) {
    ASSERT (cl->outgoing.first);
    server->buf_mem -= cl->outgoing.size;
    ASSERT (cl->state != HUB_STATE_NORMAL);
    server->buffering--;
  }

  /* clear buffered output buffers */
  string_list_clear (&cl->outgoing);

  /* free incoming buffers */
  if (cl->buffers) {
    bf_free (cl->buffers);
    cl->buffers = NULL;
  };

  /* free user */
  cl->proto->user_free (cl->user);

  free (cl);

  return 0;
}

/*
 * Exported functions
 */

int server_add_port (server_t * server, proto_t * proto, unsigned long address, int port)
{
  int s;
  listener_t *l;

  l = malloc (sizeof (listener_t));
  memset (l, 0, sizeof (listener_t));
  l->proto = proto;
  l->server = server;

  s = setup_incoming_socket (address, port);
  if (s >= 0)
    esocket_add_socket (server->h, server->es_type_listen, s, SOCKSTATE_CONNECTED,
			(unsigned long long) l);

  return 0;
}

int server_setup (server_t * s)
{
  s->es_type_listen = esocket_add_type (s->h, ESOCKET_EVENT_IN, accept_new_user, NULL, NULL, NULL);
  s->es_type_server =
    esocket_add_type (s->h, ESOCKET_EVENT_IN, server_handle_input, server_handle_output,
		      server_error, server_timeout);

  /* set default timeout */
  s->h->toval.tv_sec = 1200;

  iplist_init (&s->lastlist);

  return 0;
}

server_t *server_init (unsigned int hint)
{
  server_t *s;

  s = malloc (sizeof (server_t));
  memset (s, 0, sizeof (server_t));

  banlist_init (&s->hardbanlist);

  core_config_init ();

  /* setup socket handler */
  s->h = esocket_create_handler (hint);
  s->h->toval.tv_sec = 60;
  s->h->toval.tv_usec = 0;

  return s;
}
