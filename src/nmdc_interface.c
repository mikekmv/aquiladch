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

#include "user.h"
#include "core_config.h"
#include "plugin_int.h"

#include "hashlist_func.h"

#include "nmdc_token.h"
#include "nmdc_nicklistcache.h"
#include "nmdc_local.h"


/******************************************************************************\
**                                                                            **
**                            GLOBAL VARIABLES                                **
**                                                                            **
\******************************************************************************/

cache_t cache;
ratelimiting_t rates;

user_t *userlist = NULL;

/* special users */
user_t *HubSec;

/* globals */
unsigned int keylen = 0;
unsigned int keyoffset = 0;
char key[16 + sizeof (LOCK) + 4 + LOCKLENGTH + 1];

/* local hub cache */

leaky_bucket_t connects;
nmdc_stats_t nmdc_stats;

banlist_t reconnectbanlist;

unsigned int cloning;

unsigned int chatmaxlength;
unsigned int searchmaxlength;
unsigned int srmaxlength;
unsigned int researchmininterval, researchperiod, researchmaxcount;

unsigned char *defaultbanmessage = NULL;

/* users currently logged in */
static user_t *freelist = NULL;

hashlist_t hashlist;

/******************************************************************************\
**                                                                            **
**                            FUNCTION PROTOTYPES                             **
**                                                                            **
\******************************************************************************/


/* function prototypes */
int proto_nmdc_setup ();
int proto_nmdc_init ();
int proto_nmdc_handle_token (user_t * u, buffer_t * b);
int proto_nmdc_handle_input (user_t * user, buffer_t ** buffers);
void proto_nmdc_flush_cache ();
int proto_nmdc_user_disconnect (user_t * u);
int proto_nmdc_user_forcemove (user_t * u, unsigned char *destination, buffer_t * message);
int proto_nmdc_user_redirect (user_t * u, buffer_t * message);
int proto_nmdc_user_drop (user_t * u, buffer_t * message);
user_t *proto_nmdc_user_find (unsigned char *nick);
user_t *proto_nmdc_user_alloc (void *priv);
int proto_nmdc_user_free (user_t * user);

user_t *proto_nmdc_user_addrobot (unsigned char *nick, unsigned char *description);
int proto_nmdc_user_delrobot (user_t * u);

int proto_nmdc_user_chat_all (user_t * u, buffer_t * message);
int proto_nmdc_user_send (user_t * u, user_t * target, buffer_t * message);
int proto_nmdc_user_send_direct (user_t * u, user_t * target, buffer_t * message);
int proto_nmdc_user_priv (user_t * u, user_t * target, user_t * source, buffer_t * message);
int proto_nmdc_user_priv_direct (user_t * u, user_t * target, user_t * source, buffer_t * message);
int proto_nmdc_user_raw (user_t * target, buffer_t * message);
int proto_nmdc_user_raw_all (buffer_t * message);

/******************************************************************************\
**                                                                            **
**                            PROTOCOL DEFINITION                             **
**                                                                            **
\******************************************************************************/

/* callback structure init */
/* *INDENT-OFF* */
proto_t nmdc_proto = {
	init: 			proto_nmdc_init,
	setup:			proto_nmdc_setup,
	
	handle_token:   	proto_nmdc_handle_token,
	handle_input:		proto_nmdc_handle_input,
	flush_cache:    	proto_nmdc_flush_cache,
	
        user_alloc:		proto_nmdc_user_alloc,
        user_free:		proto_nmdc_user_free,

	user_disconnect:	proto_nmdc_user_disconnect,
	user_forcemove:		proto_nmdc_user_forcemove,
	user_redirect:		proto_nmdc_user_redirect,
	user_drop:		proto_nmdc_user_drop,
	user_find:		proto_nmdc_user_find,
	
	robot_add:		proto_nmdc_user_addrobot,
	robot_del:		proto_nmdc_user_delrobot,
	
	chat_main:		proto_nmdc_user_chat_all,
	chat_send:		proto_nmdc_user_send,
	chat_send_direct:	proto_nmdc_user_send_direct,
	chat_priv:		proto_nmdc_user_priv,
	chat_priv_direct:	proto_nmdc_user_priv_direct,
	raw_send:		proto_nmdc_user_raw,
	raw_send_all:		proto_nmdc_user_raw_all,
	
	name:			"NMDC"
};
/* *INDENT-ON* */


/******************************************************************************\
**                                                                            **
**                            ROBOT HANDLING                                  **
**                                                                            **
\******************************************************************************/

int proto_nmdc_user_delrobot (user_t * u)
{
  buffer_t *buf;

  /* clear the user from all the relevant caches: not chat and not quit */
  string_list_purge (&cache.myinfo.messages, u);
  string_list_purge (&cache.myinfoupdate.messages, u);
  string_list_purge (&cache.myinfoupdateop.messages, u);
  string_list_purge (&cache.asearch.messages, u);
  string_list_purge (&cache.psearch.messages, u);

  buf = bf_alloc (8 + NICKLENGTH);
  bf_strcat (buf, "$Quit ");
  bf_strcat (buf, u->nick);
  bf_strcat (buf, "|");

  cache_queue (cache.myinfo, u, buf);

  bf_free (buf);

  plugin_send_event (u->plugin_priv, PLUGIN_EVENT_LOGOUT, NULL);

  nicklistcache_deluser (u);
  hash_deluser (&hashlist, &u->hash);

  if (u->MyINFO) {
    bf_free (u->MyINFO);
    u->MyINFO = NULL;
  }

  /* remove from the current user list */
  if (u->next)
    u->next->prev = u->prev;

  if (u->prev) {
    u->prev->next = u->next;
  } else {
    userlist = u->next;
  };

  free (u);
  return 0;
}

user_t *proto_nmdc_user_addrobot (unsigned char *nick, unsigned char *description)
{
  user_t *u;
  buffer_t *tmpbuf;

  /* create new context */
  u = malloc (sizeof (user_t));
  memset (u, 0, sizeof (user_t));

  u->state = PROTO_STATE_VIRTUAL;

  /* add user to the list... */
  u->next = userlist;
  if (u->next)
    u->next->prev = u;
  u->prev = NULL;

  userlist = u;

  /* build MyINFO */
  tmpbuf = bf_alloc (1024);
  bf_strcat (tmpbuf, "$MyINFO $ALL ");
  bf_strcat (tmpbuf, nick);
  bf_strcat (tmpbuf, " ");
  bf_strcat (tmpbuf, description);
  bf_printf (tmpbuf, "$ $%c$$0$", 1);

  strncpy (u->nick, nick, NICKLENGTH);
  u->nick[NICKLENGTH - 1] = 0;
  u->MyINFO = bf_copy (tmpbuf, 0);
  bf_free (tmpbuf);

  u->rights = CAP_OP;
  u->op = 1;

  hash_adduser (&hashlist, u);
  nicklistcache_adduser (u);

  /* send it to the users */
  cache_queue (cache.myinfo, u, u->MyINFO);

  return u;
}

/******************************************************************************\
**                                                                            **
**                             CHAT HANDLING                                  **
**                                                                            **
\******************************************************************************/

unsigned int proto_nmdc_user_flush (user_t * u)
{
  buffer_t *b = NULL, *buffer;
  string_list_entry_t *le;

  if (u->state == PROTO_STATE_DISCONNECTED)
    return 0;
  if (u->state == PROTO_STATE_VIRTUAL)
    return 0;

  buffer = bf_alloc (10240);

  for (le = ((nmdc_user_t *) u->pdata)->privatemessages.messages.first; le; le = le->next) {
    /* data and length */
    b = le->data;
    bf_strncat (buffer, b->s, bf_used (b));
    bf_strcat (buffer, "|");

    u->MessageCnt--;
    u->CacheException--;
  }
  cache_clear ((((nmdc_user_t *) u->pdata)->privatemessages));

  server_write (u->parent, buffer);

  bf_free (buffer);

  return 0;
}

__inline__ int proto_nmdc_user_say (user_t * u, buffer_t * b, buffer_t * message)
{
  bf_strcat (b, "<");
  bf_strcat (b, u->nick);
  bf_strcat (b, "> ");
  bf_strncat (b, message->s, bf_used (message));
  if (*(b->e - 1) == '\n')
    b->e--;
  bf_strcat (b, "|");
  return 0;
}

__inline__ int proto_nmdc_user_say_string (user_t * u, buffer_t * b, unsigned char *message)
{
  bf_strcat (b, "<");
  bf_strcat (b, u->nick);
  bf_strcat (b, "> ");
  bf_strcat (b, message);
  if (*(b->e - 1) == '\n')
    b->e--;
  bf_strcat (b, "|");
  return 0;
}

int proto_nmdc_user_chat_all (user_t * u, buffer_t * message)
{
  buffer_t *buf;

  if (u->state == PROTO_STATE_DISCONNECTED)
    return 0;

  buf = bf_alloc (32 + NICKLENGTH + bf_used (message));

  proto_nmdc_user_say (u, buf, message);

  cache_queue (cache.chat, u, buf);

  bf_free (buf);

  return 0;
}

int proto_nmdc_user_send (user_t * u, user_t * target, buffer_t * message)
{
  buffer_t *buf;

  if (target->state == PROTO_STATE_DISCONNECTED)
    return EINVAL;

  buf = bf_alloc (32 + NICKLENGTH + bf_used (message));

  proto_nmdc_user_say (u, buf, message);

  //cache_count (cache.privatemessages, target, buf);
  cache_queue (((nmdc_user_t *) target->pdata)->privatemessages, u, buf);
  cache_count (privatemessages, target);
  target->MessageCnt++;
  target->CacheException++;

  bf_free (buf);

  return 0;
}

int proto_nmdc_user_send_direct (user_t * u, user_t * target, buffer_t * message)
{
  buffer_t *buf;

  if (target->state == PROTO_STATE_DISCONNECTED)
    return EINVAL;

  buf = bf_alloc (32 + NICKLENGTH + bf_used (message));

  proto_nmdc_user_say (u, buf, message);

  server_write (target->parent, buf);

  bf_free (buf);

  return 0;
}

int proto_nmdc_user_priv (user_t * u, user_t * target, user_t * source, buffer_t * message)
{
  buffer_t *buf;

  if (target->state == PROTO_STATE_DISCONNECTED)
    return 0;

  buf = bf_alloc (32 + 3 * NICKLENGTH + bf_used (message));

  bf_printf (buf, "$To: %s From: %s $<%s> ", target->nick, u->nick, source->nick);
  bf_strncat (buf, message->s, bf_used (message));
  if (*(buf->e - 1) == '\n')
    buf->e--;
  if (buf->e[-1] != '|')
    bf_strcat (buf, "|");

  if (target->state == PROTO_STATE_VIRTUAL) {
    plugin_send_event (target->plugin_priv, PLUGIN_EVENT_PM_IN, buf);

    bf_free (buf);
    return 0;
  }
  //cache_count (cache.privatemessages, target, buf);
  cache_queue (((nmdc_user_t *) target->pdata)->privatemessages, u, buf);
  cache_count (privatemessages, target);

  target->MessageCnt++;
  target->CacheException++;

  bf_free (buf);

  return 0;
}

int proto_nmdc_user_priv_direct (user_t * u, user_t * target, user_t * source, buffer_t * message)
{
  buffer_t *buf;

  if (target->state == PROTO_STATE_DISCONNECTED)
    return 0;

  buf = bf_alloc (32 + 3 * NICKLENGTH + bf_used (message));

  bf_printf (buf, "$To: %s From: %s $<%s> ", target->nick, u->nick, source->nick);
  bf_strncat (buf, message->s, bf_used (message));
  if (*(buf->e - 1) == '\n')
    buf->e--;
  bf_strcat (buf, "|");

  if (target->state == PROTO_STATE_VIRTUAL) {
    plugin_send_event (target->plugin_priv, PLUGIN_EVENT_PM_IN, buf);

    bf_free (buf);
    return 0;
  }

  server_write (target->parent, buf);

  bf_free (buf);

  return 0;
}

int proto_nmdc_user_raw (user_t * target, buffer_t * message)
{
  buffer_t *buf;

  if (target->state == PROTO_STATE_DISCONNECTED)
    return EINVAL;

  buf = bf_copy (message, 0);

  //cache_count (cache.privatemessages, target, buf);
  cache_queue (((nmdc_user_t *) target->pdata)->privatemessages, target, buf);
  cache_count (privatemessages, target);

  target->MessageCnt++;
  target->CacheException++;

  bf_free (buf);

  return 0;
}

int proto_nmdc_user_raw_all (buffer_t * message)
{
  buffer_t *buf;

  buf = bf_copy (message, 0);

  cache_queue (cache.chat, HubSec, buf);

  bf_free (buf);

  return 0;
}

/******************************************************************************\
**                                                                            **
**                             USER HANDLING                                  **
**                                                                            **
\******************************************************************************/

user_t *proto_nmdc_user_alloc (void *priv)
{
  struct timeval now;
  user_t *user;

  /* do we have a connect token? */
  gettimeofday (&now, NULL);
  if (!get_token (&rates.connects, &connects, now.tv_sec))
    return NULL;

  /* yes, create and init user */
  user = malloc (sizeof (user_t));
  memset (user, 0, sizeof (user_t));

  user->state = PROTO_STATE_INIT;
  user->parent = priv;

  init_bucket (&user->rate_warnings, now.tv_sec);
  init_bucket (&user->rate_chat, now.tv_sec);
  init_bucket (&user->rate_search, now.tv_sec);
  init_bucket (&user->rate_myinfo, now.tv_sec);
  init_bucket (&user->rate_getnicklist, now.tv_sec);
  init_bucket (&user->rate_getinfo, now.tv_sec);
  init_bucket (&user->rate_downloads, now.tv_sec);

  /* warnings and searches start with a full token load ! */
  user->rate_warnings.tokens = rates.warnings.burst;


  /* protocol private data */
  user->pdata = malloc (sizeof (nmdc_user_t));
  memset (user->pdata, 0, sizeof (nmdc_user_t));

  /* add user to the list... */
  user->next = userlist;
  if (user->next)
    user->next->prev = user;
  user->prev = NULL;

  userlist = user;

  searchlist_init (&user->searchlist);

  nmdc_stats.userjoin++;

  return user;
}

int proto_nmdc_user_free (user_t * user)
{

  /* remove from the current user list */
  if (user->next)
    user->next->prev = user->prev;

  if (user->prev) {
    user->prev->next = user->next;
  } else {
    userlist = user->next;
  };

  user->parent = NULL;
  user->next = freelist;
  user->prev = NULL;
  freelist = user;

  nmdc_stats.userpart++;

  return 0;
}

user_t *proto_nmdc_user_find (unsigned char *nick)
{
  return hash_find_nick (&hashlist, nick, strlen (nick));
}

int proto_nmdc_user_disconnect (user_t * u)
{
  buffer_t *buf;

  if (u->state == PROTO_STATE_DISCONNECTED)
    return 0;

  /* if user was online, clear out all stale data */
  if (u->state == PROTO_STATE_ONLINE) {
    string_list_purge (&cache.myinfo.messages, u);
    string_list_purge (&cache.myinfoupdate.messages, u);
    string_list_purge (&cache.myinfoupdateop.messages, u);
    string_list_purge (&cache.asearch.messages, u);
    string_list_purge (&cache.psearch.messages, u);
    string_list_clear (&((nmdc_user_t *) u->pdata)->results.messages);
    string_list_clear (&((nmdc_user_t *) u->pdata)->privatemessages.messages);

    buf = bf_alloc (8 + NICKLENGTH);

    bf_strcat (buf, "$Quit ");
    bf_strcat (buf, u->nick);

    cache_queue (cache.myinfo, u, buf);

    bf_free (buf);

    plugin_send_event (u->plugin_priv, PLUGIN_EVENT_LOGOUT, NULL);

    nicklistcache_deluser (u);
    hash_deluser (&hashlist, &u->hash);
  } else {
    /* if the returned user has same nick, but differnt user pointer, this is legal */
    ASSERT (u != hash_find_nick (&hashlist, u->nick, strlen (u->nick)));
  }

  /* clean out our MyINFO info */
  if (u->MyINFO) {
    bf_free (u->MyINFO);
    u->MyINFO = NULL;
  }

  searchlist_clear (&u->searchlist);

  if (u->supports & NMDC_SUPPORTS_ZLine)
    cache.ZlineSupporters--;
  if (u->supports & NMDC_SUPPORTS_ZPipe)
    cache.ZpipeSupporters--;

  u->state = PROTO_STATE_DISCONNECTED;

  return 0;
}


int proto_nmdc_user_forcemove (user_t * u, unsigned char *destination, buffer_t * message)
{
  buffer_t *b;

  if (u->state == PROTO_STATE_DISCONNECTED)
    return 0;

  DPRINTF ("Redirecting user %s to %s because %.*s\n", u->nick, destination,
	   (int) bf_used (message), message->s);

  if (u->MessageCnt)
    proto_nmdc_user_flush (u);

  b = bf_alloc (265 + NICKLENGTH + strlen (destination) + bf_used (message));

  if (message)
    proto_nmdc_user_say (HubSec, b, message);

  if (destination && *destination) {
    bf_strcat (b, "$ForceMove ");
    bf_strcat (b, destination);
    bf_strcat (b, "|");
  }

  server_write (u->parent, b);
  bf_free (b);

  if (u->state != PROTO_STATE_DISCONNECTED)
    server_disconnect_user (u->parent);

  nmdc_stats.forcemove++;

  return 0;
}

int proto_nmdc_user_drop (user_t * u, buffer_t * message)
{
  buffer_t *b;

  if (u->state == PROTO_STATE_DISCONNECTED)
    return 0;

  if (u->MessageCnt)
    proto_nmdc_user_flush (u);

  if (message) {
    b = bf_alloc (265 + bf_used (message));

    proto_nmdc_user_say (HubSec, b, message);

    server_write (u->parent, b);
    bf_free (b);
  }

  server_disconnect_user (u->parent);

  nmdc_stats.disconnect++;

  return 0;
}

int proto_nmdc_user_redirect (user_t * u, buffer_t * message)
{
  if (u->state == PROTO_STATE_DISCONNECTED)
    return 0;

  /* call plugin first. it can do custom redirects */
  if (plugin_send_event (u->plugin_priv, PLUGIN_EVENT_REDIRECT, message) != PLUGIN_RETVAL_CONTINUE) {
    return 0;
  }

  nmdc_stats.redirect++;
  nmdc_stats.forcemove--;

  return proto_nmdc_user_forcemove (u, config.Redirect, message);
}

int proto_nmdc_user_warn (user_t * u, struct timeval *now, unsigned char *message, ...)
{
  buffer_t *buf;
  va_list ap;

  if (!get_token (&rates.warnings, &u->rate_warnings, now->tv_sec)) {
    return 0;
  }

  buf = bf_alloc (1024);

  bf_strcat (buf, "<");
  bf_strcat (buf, HubSec->nick);
  bf_strcat (buf, "> WARNING: ");

  va_start (ap, message);
  bf_vprintf (buf, message, ap);
  va_end (ap);

  bf_strcat (buf, "|");

  server_write (u->parent, buf);
  bf_free (buf);

  return 1;
}


int proto_nmdc_handle_input (user_t * user, buffer_t ** buffers)
{
  buffer_t *b;

  if (bf_size (*buffers) > MAX_TOKEN_SIZE) {
    bf_free (*buffers);
    *buffers = NULL;
    return 0;
  }

  for (;;) {
    /* get a new token */
    b = bf_sep_char (buffers, '|');
    if (!b)
      break;

    /* process it and free memory */
    errno = 0;			/* make sure this is reset otherwise errno check will cause crashes */
    if (proto_nmdc_handle_token (user, b) < 0) {
      /* This should never happen! On an EPIPE, server_write should do this.
         if (errno == EPIPE)
         server_disconnect_user (user->parent);
       */
      ASSERT (!((errno == EPIPE) && user->parent));
      bf_free (b);
      break;
    }
    bf_free (b);
    /* if parent is freed "buffers" is not longer valid */
    if (user->state == PROTO_STATE_DISCONNECTED)
      break;
  }

  /* destroy freelist */
  while (freelist) {
    user_t *o;

    o = freelist;
    freelist = freelist->next;
    if (o->plugin_priv)
      plugin_del_user ((plugin_private_t **) & o->plugin_priv);
    /* free protocol private data */
    if (o->pdata)
      free (o->pdata);

    free (o);
  }
  return 0;
}

/******************************************************************************\
**                                                                            **
**                                INIT HANDLING                               **
**                                                                            **
\******************************************************************************/

int proto_nmdc_init ()
{
  int i, l;
  unsigned char *s, *d;
  unsigned char lock[16 + sizeof (LOCK) + 2 + LOCKLENGTH + 2 + 1];
  struct timeval now;

  /* prepare lock calculation shortcut */
  strcpy (lock, "EXTENDEDPROTOCOL");
  strcat (lock, LOCK);
  strcat (lock, "[[");
  l = strlen (lock);
  keyoffset = l;
  for (i = 0; i < LOCKLENGTH; i++, l++)
    lock[l] = '\0';
  lock[l++] = ']';
  lock[l++] = ']';
  lock[l] = '\0';

  memset (key, 0, sizeof (key));
  s = lock;
  d = key;
  for (i = 1; i < l; i++) {
    d[i] = s[i] ^ s[i - 1];
  };

  d[0] = s[0] ^ d[l - 1] ^ 5;

  for (i = 0; i < l; i++)
    d[i] = ((d[i] << 4) & 240) | ((d[i] >> 4) & 15);

  keylen = l;

  /* rate limiting stuff */
  memset ((void *) &rates, 0, sizeof (ratelimiting_t));
  init_bucket_type (&rates.chat, 2, 3, 1);
  init_bucket_type (&rates.asearch, 15, 5, 1);
  init_bucket_type (&rates.psearch, 15, 5, 1);
  init_bucket_type (&rates.myinfo, 1800, 1, 1);
  init_bucket_type (&rates.getnicklist, 1200, 1, 1);
  init_bucket_type (&rates.getinfo, 1, 10, 10);
  init_bucket_type (&rates.downloads, 5, 6, 1);
  init_bucket_type (&rates.connects, 1, 10, 10);
  init_bucket_type (&rates.psresults_in, 15, 100, 25);
  init_bucket_type (&rates.psresults_out, 15, 50, 25);
  init_bucket_type (&rates.warnings, 120, 10, 1);

  config_register ("rate.chat.period", CFG_ELEM_ULONG, &rates.chat.period,
		   "Period of chat messages. This controls how often a user can send a chat message. Keep this low.");
  config_register ("rate.chat.burst", CFG_ELEM_ULONG, &rates.chat.burst,
		   "Burst of chat messages. This controls how many chat messages a user can 'save up'. Keep this low.");
  config_register ("rate.activesearch.period", CFG_ELEM_ULONG, &rates.asearch.period,
		   "Period of searches. This controls how often an active user can search. Keep this reasonable.");
  config_register ("rate.activesearch.burst", CFG_ELEM_ULONG, &rates.asearch.burst,
		   "Burst of searches. This controls how many searches an active user can 'save up'. Keep this low.");
  config_register ("rate.passivesearch.period", CFG_ELEM_ULONG, &rates.psearch.period,
		   "Period of searches. This controls how often a passive user can search. Keep this reasonable.");
  config_register ("rate.passivesearch.burst", CFG_ELEM_ULONG, &rates.psearch.burst,
		   "Burst of searches. This controls how many searches a passive user can 'save up'. Keep this low.");
  config_register ("rate.myinfo.period", CFG_ELEM_ULONG, &rates.myinfo.period,
		   "Period of MyINFO messages. This controls how often a user can send a MyINFO message. Keep this very high.");
  config_register ("rate.myinfo.burst", CFG_ELEM_ULONG, &rates.myinfo.burst,
		   "Burst of MyINFO messages. This controls how many MyINFO messages a user can 'save up'. Keep this at 1.");
  config_register ("rate.getnicklist.period", CFG_ELEM_ULONG, &rates.getnicklist.period,
		   "Period of nicklist requests. This controls how often a user can refresh his userlist. Keep this high.");
  config_register ("rate.getnicklist.burst", CFG_ELEM_ULONG, &rates.getnicklist.burst,
		   "Burst of nicklist requests. This controls how many userlist refreshes a user can 'save up'. Keep this at 1.");
  config_register ("rate.getinfo.period", CFG_ELEM_ULONG, &rates.getinfo.period,
		   "Period of getinfo requests. This controls how often a user can request info on a user. Keep this low.");
  config_register ("rate.getinfo.burst", CFG_ELEM_ULONG, &rates.getinfo.burst,
		   "Burst of getinfo requests. This controls how many getinfo requests a user can 'save up'.");
  config_register ("rate.download.period", CFG_ELEM_ULONG, &rates.downloads.period,
		   "Period of downloads. This controls how often a user can initiate a download. Keep this low.");
  config_register ("rate.download.burst", CFG_ELEM_ULONG, &rates.downloads.burst,
		   "Burst of downloads. This controls how many downloads a user can 'save up'. Keep this reasonable.");
  config_register ("rate.connect.period", CFG_ELEM_ULONG, &rates.connects.period,
		   "Period of connects. This controls how often the connect counter is refreshed. Keep this low.");
  config_register ("rate.connect.burst", CFG_ELEM_ULONG, &rates.connects.burst,
		   "Burst of connects. This controls how many new user connects can be saved up in idle time. Keep this low.");
  config_register ("rate.connect.refill", CFG_ELEM_ULONG, &rates.connects.refill,
		   "Refill of connects. This controls how many new user connects are added each time the counter resets. Keep this low.");

  config_register ("rate.results_in.period", CFG_ELEM_ULONG, &rates.psresults_in.period,
		   "Period of passive search results. This controls how often the incoming passive search results counter is refreshed. Keep this low.");
  config_register ("rate.results_in.burst", CFG_ELEM_ULONG, &rates.psresults_in.burst,
		   "Burst of passive search results. This controls how many incoming passive search results can be saved up in idle time. Keep this equal to the search period.");
  config_register ("rate.results_in.refill", CFG_ELEM_ULONG, &rates.psresults_in.refill,
		   "Refill of passive search results. This controls how many incoming passive search results are added each time the counter resets. Keep this low.");

  config_register ("rate.results_out.period", CFG_ELEM_ULONG, &rates.psresults_out.period,
		   "Period of passive search results. This controls how often the outgoing passive search results counter is refreshed. Keep this low.");
  config_register ("rate.results_out.burst", CFG_ELEM_ULONG, &rates.psresults_out.burst,
		   "Burst of passive search results. This controls how many outgoing passive search results can be saved up in idle time. Keep this reasonably high.");
  config_register ("rate.results_out.refill", CFG_ELEM_ULONG, &rates.psresults_out.refill,
		   "Refill of passive search results. This controls how many outgoing passive search results are added each time the counter resets. Keep this reasonably high.");

  config_register ("rate.warnings.period", CFG_ELEM_ULONG, &rates.warnings.period,
		   "Period of user warnings. This controls how often a warning is send to user that overstep limits.");
  config_register ("rate.warnings.refill", CFG_ELEM_ULONG, &rates.warnings.refill,
		   "Period of user warnings. This controls how many warning a user gets within the period.");
  config_register ("rate.warnings.burst", CFG_ELEM_ULONG, &rates.warnings.burst,
		   "Period of user warnings. This controls how many warnings a user that overstep limits can save up.");

  /* cache stuff */
  memset ((void *) &cache, 0, sizeof (cache_t));
  cache.needrebuild = 1;

  init_bucket_type (&cache.chat.timertype, 1, 1, 1);
  init_bucket_type (&cache.myinfo.timertype, 1, 1, 1);
  init_bucket_type (&cache.myinfoupdate.timertype, 300, 1, 1);	/* every 5 minutes */
  init_bucket_type (&cache.myinfoupdateop.timertype, 1, 1, 1);
  init_bucket_type (&cache.asearch.timertype, 30, 1, 1);
  init_bucket_type (&cache.psearch.timertype, 30, 1, 1);
  init_bucket_type (&cache.aresearch.timertype, 30, 1, 1);
  init_bucket_type (&cache.presearch.timertype, 30, 1, 1);
  init_bucket_type (&cache.results.timertype, 20, 1, 1);
  init_bucket_type (&cache.privatemessages.timertype, 1, 1, 1);

  gettimeofday (&now, NULL);

  init_bucket (&cache.chat.timer, now.tv_sec);
  init_bucket (&cache.myinfo.timer, now.tv_sec);
  init_bucket (&cache.myinfoupdate.timer, now.tv_sec);
  init_bucket (&cache.myinfoupdateop.timer, now.tv_sec);
  init_bucket (&cache.asearch.timer, now.tv_sec);
  init_bucket (&cache.psearch.timer, now.tv_sec);
  init_bucket (&cache.aresearch.timer, now.tv_sec);
  init_bucket (&cache.presearch.timer, now.tv_sec);
  init_bucket (&cache.results.timer, now.tv_sec);
  init_bucket (&cache.privatemessages.timer, now.tv_sec);

  config_register ("cache.chat.period", CFG_ELEM_ULONG, &cache.chat.timertype.period,
		   "Period of chat cache flush. This controls how often chat messages are sent to users. Keep this low.");
  config_register ("cache.join.period", CFG_ELEM_ULONG, &cache.myinfo.timertype.period,
		   "Period of join cache flush. This controls how often users are notified of new joins.");
  config_register ("cache.update.period", CFG_ELEM_ULONG, &cache.myinfoupdate.timertype.period,
		   "Period of update cache flush. This controls how often users are sent MyINFO updates. Keep this high.");
  config_register ("cache.updateop.period", CFG_ELEM_ULONG, &cache.myinfoupdateop.timertype.period,
		   "Period of operator update cache flush. This controls how often operators are sent MyINFO updates. Keep this low.");
  config_register ("cache.activesearch.period", CFG_ELEM_ULONG, &cache.asearch.timertype.period,
		   "Period of active search cache flush. This controls how often search messages are sent to active users.");
  config_register ("cache.passivesearch.period", CFG_ELEM_ULONG, &cache.psearch.timertype.period,
		   "Period of passive search cache flush. This controls how often search messages are sent to passive users.");
  config_register ("cache.results.period", CFG_ELEM_ULONG, &cache.results.timertype.period,
		   "Period of search results cache flush. This controls how often search results are sent to passive users.");
  config_register ("cache.pm.period", CFG_ELEM_ULONG, &cache.privatemessages.timertype.period,
		   "Period of private messages cache flush. This controls how often private messages are sent to users. Keep this low.");

  config_register ("cache.activeresearch.period", CFG_ELEM_ULONG, &cache.aresearch.timertype.period,
		   "Period of active repeated search cache flush. This controls how often search messages are sent to active users.");
  config_register ("cache.passiveresearch.period", CFG_ELEM_ULONG,
		   &cache.presearch.timertype.period,
		   "Period of passive repeated search cache flush. This controls how often search messages are sent to passive users.");

  cloning = DEFAULT_CLONING;
  chatmaxlength = DEFAULT_MAXCHATLENGTH;
  searchmaxlength = DEFAULT_MAXSEARCHLENGTH;
  srmaxlength = DEFAULT_MAXSRLENGTH;
  researchmininterval = 60;
  researchperiod = 1200;
  researchmaxcount = 5;
  defaultbanmessage = strdup ("");

  config_register ("hub.allowcloning", CFG_ELEM_UINT, &cloning,
		   "Allow multiple users from the same IP address.");
  config_register ("nmdc.maxchatlength", CFG_ELEM_UINT, &chatmaxlength,
		   "Maximum length of a chat message.");
  config_register ("nmdc.maxsearchlength", CFG_ELEM_UINT, &searchmaxlength,
		   "Maximum length of a search message.");
  config_register ("nmdc.maxsrlength", CFG_ELEM_UINT, &srmaxlength,
		   "Maximum length of a search result");

  config_register ("nmdc.researchinterval", CFG_ELEM_UINT, &researchmininterval,
		   "Minimum time before a re-search is considered valid.");
  config_register ("nmdc.researchperiod", CFG_ELEM_UINT, &researchperiod,
		   "Period during which a search is considered a re-search.");
  config_register ("nmdc.researchmaxcount", CFG_ELEM_UINT, &researchmaxcount,
		   "Maximum number of searches cached.");

  config_register ("nmdc.defaultbanmessage", CFG_ELEM_STRING, &defaultbanmessage,
		   "This message is send to all banned users when they try to join.");

  /* further inits */
  memset (&hashlist, 0, sizeof (hashlist));
  hash_init (&hashlist);
  token_init ();

  memset (&nmdc_stats, 0, sizeof (nmdc_stats_t));

  banlist_init (&reconnectbanlist);

  return 0;
}

int proto_nmdc_setup ()
{
  HubSec = proto_nmdc_user_addrobot (config.HubSecurityNick, config.HubSecurityDesc);
  HubSec->flags |= PROTO_FLAG_HUBSEC;
  plugin_new_user ((plugin_private_t **) & HubSec->plugin_priv, HubSec, &nmdc_proto);

  return 0;
}