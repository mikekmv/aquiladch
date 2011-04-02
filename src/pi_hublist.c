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

#include <unistd.h>
#include <string.h>

#include "esocket.h"
#include "buffer.h"
#include "config.h"
#include "commands.h"
#include "plugin.h"

#define PI_HUBLIST_BUFFER_SIZE 	4096

//struct pi_hublist_context {
//  struct pi_huglist_context *next, *prev;
//  
//  esocket_t *es;
//} pi_hublist_context_t;

unsigned char *pi_hublist_lists;

struct timeval pi_hublist_savetime;
unsigned long pi_hublist_interval;
unsigned int pi_hublist_es_type;
esocket_handler_t *pi_hublist_handler;

extern long users_total;

static int escape_string (char *output, int j)
{
  int i;
  char *l, *k;
  char tmpbuf[2048];

  if (j > 2048)
    return -1;

  memcpy (tmpbuf, output, j);

  l = tmpbuf;
  k = output;

  /* do not escape first $ character */
  if (*k == '$') {
    k++;
    l++;
    j--;
  };

  for (i = 0; (i < j) && ((k - output) < 2038); i++)
    switch (l[i]) {
      case 0:
      case 5:
      case 36:
      case 96:
      case 124:
      case 126:
	k += sprintf (k, "/%%DCN%03d%%/", l[i]);
	break;
      default:
	*k++ = l[i];
    }
  *k = '\0';

  return k - output;
}


int pi_hublist_update (buffer_t * output)
{
  int result;
  unsigned int port;
  unsigned char *lists, *l, *p, *c;
  esocket_t *s;

  lists = strdup (pi_hublist_lists);

  for (l = strtok (lists, ";, "); l; l = strtok (NULL, ";, ")) {
    if (!strlen (l))
      continue;

    c = strdup (l);

    DPRINTF ("hublist Registering at %s\n", l);
    /* extract port */
    p = strchr (l, ':');
    if (p) {
      port = atoi (p + 1);
      *p = '\0';
    } else
      port = 2501;

    /* create esocket */
    s =
      esocket_new (pi_hublist_handler, pi_hublist_es_type, AF_INET, SOCK_STREAM, 0,
		   (unsigned long long) c);

    /* connect */
    if ((result = esocket_connect (s, l, port)) != 0) {
      if (result > 0) {
	bf_printf (output, "Hublist update ERROR: %s: connect: %s\n", l, gai_strerror (result));
	DPRINTF ("%s", output->s);
      } else {
	bf_printf (output, "Hublist update ERROR: %s: connect: %s\n", l, strerror (result));
	DPRINTF ("%s", output->s);
      }
      esocket_close (s);
      free (c);
      esocket_remove_socket (s);
      continue;
    };
    esocket_settimeout (s, 10);
  };

  free (lists);
  return 0;
};


int pi_hublist_handle_input (esocket_t * s)
{
  int n;
  unsigned int i, j, port;
  buffer_t *buf, *output;
  struct sockaddr_in local;
  char *t, *l, *k;
  config_element_t *hubname, *hostname, *listenport, *hubdesc;

  buf = bf_alloc (PI_HUBLIST_BUFFER_SIZE);

  // read data
  n = read (s->socket, buf->s, PI_HUBLIST_BUFFER_SIZE);
  if (n < 0) {
    bf_printf (buf, "Hublost update ERROR: %s: read: %s\n", (char *) s->context, strerror (errno));
    DPRINTF ("%s", buf->s);
    plugin_report (buf);
    goto leave;
  }
  buf->e = buf->s + n;

  n = sizeof (local);
  if (getsockname (s->socket, (struct sockaddr *) &local, &n)) {
    bf_printf (buf, "Hublost update ERROR: %s: gethostname: %s\n", (char *) s->context,
	       strerror (errno));
    DPRINTF ("%s", buf->s);
    plugin_report (buf);
    goto leave;
  }
  port = ntohs (local.sin_port);

  output = bf_alloc (PI_HUBLIST_BUFFER_SIZE);

  /* extract lock */
  t = buf->s + 6;		/* skip '$Lock ' */
  l = strsep (&t, " ");
  j = t - l - 1;

  /* prepare output buffer */
  bf_strcat (output, "$Key ");
  k = output->e;

  /* calculate key */
  for (i = 1; i < j; i++)
    k[i] = l[i] ^ l[i - 1];

  /* use port as magic byte */
  k[0] = l[0] ^ k[j - 1] ^ ((port + (port >> 8)) & 0xff);

  for (i = 0; i < j; i++)
    k[i] = ((k[i] << 4) & 240) | ((k[i] >> 4) & 15);

  k[j] = '\0';

  /* escape key */
  output->e += escape_string (k, j);	/* length of key plus length of "$Key " */

  bf_strcat (output, "|");

  hubname = config_find ("hubname");
  ASSERT (hubname);
  hostname = config_find ("hubaddress");
  ASSERT (hostname);
  listenport = config_find ("nmdc.listenport");
  ASSERT (listenport);
  hubdesc = config_find ("hubdescription");
  ASSERT (hubdesc);
  bf_printf (output, "%s|%s:%u|%s|%d|%llu|",
	     *hubname->val.v_string ? *hubname->val.v_string : (unsigned char *) "",
	     *hostname->val.v_string ? *hostname->val.v_string : (unsigned char *) "",
	     *listenport->val.v_uint ? *listenport->val.v_uint : 411,
	     *hubdesc->val.v_string ? *hubdesc->val.v_string : (unsigned char *) "",
	     users_total, 0LL);

  n = write (s->socket, output->s, bf_used (output));
  if (n < 0) {
    bf_printf (buf, "Hublist update ERROR: %s: write: %s\n", (char *) s->context, strerror (errno));
    plugin_report (buf);
    DPRINTF ("%s", buf->s);
  }

  bf_free (output);

#ifdef DEBUG
  DPRINTF (" -- Registered at hublist %s\n", (char *) s->context);
#endif

leave:
  bf_free (buf);

  free ((char *) s->context);
  esocket_close (s);
  esocket_remove_socket (s);

  return 0;
};

int pi_hublist_handle_error (esocket_t * s)
{
  buffer_t *buf;

  if (!s->error)
    return 0;

  buf = bf_alloc (10240);

  bf_printf (buf, "Hublist update ERROR: %s: Timed out.\n", (char *) s->context);
  plugin_report (buf);
  DPRINTF ("%s", buf->s);

  free ((char *) s->context);
  esocket_close (s);
  esocket_remove_socket (s);

  bf_free (buf);

  return 0;
};

int pi_hublist_handle_timeout (esocket_t * s)
{
  buffer_t *buf = bf_alloc (10240);

  bf_printf (buf, "Hublist update ERROR: %s: Timed out.\n", (char *) s->context);
  plugin_report (buf);
  DPRINTF ("%s", buf->s);

  free ((char *) s->context);
  esocket_close (s);
  esocket_remove_socket (s);

  bf_free (buf);

  return 0;
};


unsigned long pi_hublist_handle_update (plugin_user_t * user, void *ctxt, unsigned long event,
					void *token)
{
  struct timeval now;
  buffer_t *output;
  unsigned int l;

  if (!pi_hublist_interval)
    return 0;

  gettimeofday (&now, NULL);
  if (now.tv_sec > (pi_hublist_savetime.tv_sec + (time_t) pi_hublist_interval)) {
    pi_hublist_savetime = now;
    output = bf_alloc (1024);
    bf_printf (output, "Errors during hublist update:\n");
    l = bf_used (output);

    pi_hublist_update (output);

    if (bf_used (output) != l) {
      plugin_report (output);
    }
    bf_free (output);
  }

  return 0;
}

unsigned long pi_hublist_handler_hublist (plugin_user_t * user, buffer_t * output, void *dummy,
					  unsigned int argc, unsigned char **argv)
{
  gettimeofday (&pi_hublist_savetime, NULL);
  pi_hublist_update (output);

  return 0;
}

int pi_hublist_setup (esocket_handler_t * h)
{
  pi_hublist_es_type =
    esocket_add_type (h, ESOCKET_EVENT_IN, pi_hublist_handle_input, NULL, pi_hublist_handle_error,
		      pi_hublist_handle_timeout);

  plugin_request (NULL, PLUGIN_EVENT_CACHEFLUSH,
		  (plugin_event_handler_t *) pi_hublist_handle_update);

  pi_hublist_handler = h;

  return 0;
}

int pi_hublist_init ()
{
  pi_hublist_es_type = -1;

  gettimeofday (&pi_hublist_savetime, NULL);

  pi_hublist_interval = 0;
  pi_hublist_lists = strdup ("");

  config_register ("hublist.lists", CFG_ELEM_STRING, &pi_hublist_lists,
		   "list of hublist addresses.");
  config_register ("hublist.interval", CFG_ELEM_ULONG, &pi_hublist_interval,
		   "Interval of hublist updates.");

  command_register ("hublist", &pi_hublist_handler_hublist, 0, "Register at hublists.");

  return 0;
};
