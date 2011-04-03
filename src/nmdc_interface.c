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

#include "aqtime.h"
#include "nmdc_token.h"
#include "nmdc_local.h"

/******************************************************************************\
**                                                                            **
**                            GLOBAL VARIABLES                                **
**                                                                            **
\******************************************************************************/

cache_t cache;
ratelimiting_t rates;

nmdc_user_t *userlist = NULL;

server_t *nmdc_server;

/* special users */
nmdc_user_t *HubSec;

/* globals */
unsigned int keylen = 0;
unsigned int keyoffset = 0;
char key[16 + sizeof (LOCK) + 4 + LOCKLENGTH + 1];

/* local hub cache */

leaky_bucket_t connects;
nmdc_stats_t nmdc_stats;

banlist_t reconnectbanlist;
banlist_t softbanlist;

plugin_manager_t *nmdc_pluginmanager;

unsigned int cloning;

unsigned int chatmaxlength;
unsigned int searchmaxlength;
unsigned int srmaxlength;
unsigned int researchmininterval, researchperiod, researchmaxcount;

unsigned char *defaultbanmessage = NULL;

leaky_bucket_t rate_warnings;

static nmdc_user_t *freelist = NULL;
hashlist_t hashlist;

unsigned long cachelist_count = 0;
nmdc_user_t *cachelist = NULL;
nmdc_user_t *cachelist_last = NULL;
hashlist_t cachehashlist;

/******************************************************************************\
**                                                                            **
**                            FUNCTION PROTOTYPES                             **
**                                                                            **
\******************************************************************************/


/* function prototypes */
proto_t *proto_nmdc_setup ();
proto_t *proto_nmdc_init ();
int proto_nmdc_handle_token (nmdc_user_t * u, buffer_t * b);
int proto_nmdc_handle_input (nmdc_user_t * user, buffer_t ** buffers);
void proto_nmdc_flush_cache ();
int proto_nmdc_user_disconnect (nmdc_user_t * u, char *reason);
int proto_nmdc_user_forcemove (nmdc_user_t * u, unsigned char *destination, buffer_t * message);
int proto_nmdc_user_redirect (nmdc_user_t * u, buffer_t * message);
int proto_nmdc_user_drop (nmdc_user_t * u, buffer_t * message);
nmdc_user_t *proto_nmdc_user_find (unsigned char *nick);
nmdc_user_t *proto_nmdc_user_find_ip (nmdc_user_t * last, unsigned long ip);
nmdc_user_t *proto_nmdc_user_find_net (nmdc_user_t * last, unsigned long ip, unsigned long netmask);
nmdc_user_t *proto_nmdc_user_alloc (void *priv, unsigned long ipaddress);
int proto_nmdc_user_free (nmdc_user_t * user);

nmdc_user_t *proto_nmdc_user_addrobot (unsigned char *nick, unsigned char *description);
int proto_nmdc_user_delrobot (nmdc_user_t * u);

int proto_nmdc_user_chat_all (nmdc_user_t * u, buffer_t * message);
int proto_nmdc_user_send (nmdc_user_t * u, nmdc_user_t * target, buffer_t * message);
int proto_nmdc_user_send_direct (nmdc_user_t * u, nmdc_user_t * target, buffer_t * message);
int proto_nmdc_user_priv (nmdc_user_t * u, nmdc_user_t * target, nmdc_user_t * source,
			  buffer_t * message);
int proto_nmdc_user_priv_direct (nmdc_user_t * u, nmdc_user_t * target, nmdc_user_t * source,
				 buffer_t * message);
int proto_nmdc_user_raw (nmdc_user_t * target, buffer_t * message);
int proto_nmdc_user_raw_all (buffer_t * message);

banlist_t *proto_nmdc_banlist_hard (void);
banlist_t *proto_nmdc_banlist_soft (void);

int proto_nmdc_warn (struct timeval *now, const char *message, ...);

/******************************************************************************\
**                                                                            **
**                            PROTOCOL DEFINITION                             **
**                                                                            **
\******************************************************************************/

/* callback structure init */
/* *INDENT-OFF* */
proto_t nmdc_proto = {
	init: 			(void *) proto_nmdc_init,
	setup:			(void *) proto_nmdc_setup,
	
	handle_token:   	(void *) proto_nmdc_handle_token,
	handle_input:		(void *) proto_nmdc_handle_input,
	flush_cache:    	(void *) proto_nmdc_flush_cache,
	
        user_alloc:		(void *) proto_nmdc_user_alloc,
        user_free:		(void *) proto_nmdc_user_free,

	user_disconnect:	(void *) proto_nmdc_user_disconnect,
	user_forcemove:		(void *) proto_nmdc_user_forcemove,
	user_redirect:		(void *) proto_nmdc_user_redirect,
	user_drop:		(void *) proto_nmdc_user_drop,
	user_find:		(void *) proto_nmdc_user_find,
	user_find_ip:		(void *) proto_nmdc_user_find_ip,
	user_find_net:		(void *) proto_nmdc_user_find_net,
	
	robot_add:		(void *) proto_nmdc_user_addrobot,
	robot_del:		(void *) proto_nmdc_user_delrobot,
	
	chat_main:		(void *) proto_nmdc_user_chat_all,
	chat_send:		(void *) proto_nmdc_user_send,
	chat_send_direct:	(void *) proto_nmdc_user_send_direct,
	chat_priv:		(void *) proto_nmdc_user_priv,
	chat_priv_direct:	(void *) proto_nmdc_user_priv_direct,
	raw_send:		(void *) proto_nmdc_user_raw,
	raw_send_all:		(void *) proto_nmdc_user_raw_all,

        get_banlist_hard:	proto_nmdc_banlist_hard,
        get_banlist_soft:	proto_nmdc_banlist_soft,

	name:			"NMDC"
};
/* *INDENT-ON* */


/******************************************************************************\
**                                                                            **
**                            ROBOT HANDLING                                  **
**                                                                            **
\******************************************************************************/

banlist_t *proto_nmdc_banlist_hard ()
{
  return &nmdc_server->hardbanlist;
}

banlist_t *proto_nmdc_banlist_soft ()
{
  return &softbanlist;
}

/******************************************************************************\
**                                                                            **
**                            ROBOT HANDLING                                  **
**                                                                            **
\******************************************************************************/

int proto_nmdc_user_delrobot (nmdc_user_t * u)
{
  buffer_t *buf;

  if (u->state != PROTO_STATE_VIRTUAL)
    return -1;

  /* clear the user from all the relevant caches: not chat and not quit */
  string_list_purge (&cache.myinfo.messages, &u->user);
  string_list_purge (&cache.myinfoupdate.messages, &u->user);
  string_list_purge (&cache.myinfoupdateop.messages, &u->user);
  string_list_purge (&cache.asearch.messages, &u->user);
  string_list_purge (&cache.psearch.messages, &u->user);

  buf = bf_alloc (8 + NICKLENGTH);
  bf_strcat (buf, "$Quit ");
  bf_strcat (buf, u->user.nick);
  bf_strcat (buf, "|");

  cache_queue (cache.myinfo, NULL, buf);
  cache_queue (cache.myinfoupdateop, NULL, buf);

  bf_free (buf);

  plugin_send_event (nmdc_pluginmanager, u->user.plugin_priv, PLUGIN_EVENT_LOGOUT, NULL);

  nicklistcache_deluser (u);
  hash_deluser (&hashlist, &u->user.hash);

  if (u->MyINFO) {
    bf_free (u->MyINFO);
    u->MyINFO = NULL;
  }

  if (u->user.plugin_priv)
    plugin_del_user ((plugin_private_t **) & u->user.plugin_priv);

  /* remove from the current user list */
  if (u->user.next)
    u->user.next->prev = u->user.prev;

  if (u->user.prev) {
    u->user.prev->next = u->user.next;
  } else {
    userlist = (nmdc_user_t *) u->user.next;
  };

  free (u);

  return 0;
}

nmdc_user_t *proto_nmdc_user_addrobot (unsigned char *nick, unsigned char *description)
{
  nmdc_user_t *u;
  buffer_t *tmpbuf;

  /* create new context */
  u = malloc (sizeof (nmdc_user_t));
  if (!u)
    return NULL;
  memset (u, 0, sizeof (nmdc_user_t));

  u->state = PROTO_STATE_VIRTUAL;

  /* add user to the list... */
  u->user.next = (user_t *) userlist;
  if (u->user.next)
    u->user.next->prev = &u->user;
  u->user.prev = NULL;

  userlist = u;

  /* build MyINFO */
  tmpbuf = bf_alloc (32 + strlen (nick) + strlen (description));
  bf_strcat (tmpbuf, "$MyINFO $ALL ");
  bf_strcat (tmpbuf, nick);
  bf_strcat (tmpbuf, " ");
  bf_strcat (tmpbuf, description);
  bf_printf (tmpbuf, "$ $%c$$0$", 1);

  strncpy (u->user.nick, nick, NICKLENGTH);
  u->user.nick[NICKLENGTH - 1] = 0;
  u->MyINFO = bf_copy (tmpbuf, 0);
  bf_free (tmpbuf);

  u->user.flags |= PROTO_FLAG_VIRTUAL;
  u->user.rights = CAP_OP;
  u->op = 1;

  hash_adduser (&hashlist, &u->user);
  nicklistcache_adduser (u);

  /* send it to the users */
  cache_queue (cache.myinfo, u, u->MyINFO);
  cache_queue (cache.myinfoupdateop, u, u->MyINFO);

  return u;
}

/******************************************************************************\
**                                                                            **
**                          FREELIST HANDLING                                **
**                                                                            **
\******************************************************************************/

void proto_nmdc_user_freelist_add (nmdc_user_t * user)
{
  user->user.next = (user_t *) freelist;
  user->user.prev = NULL;
  freelist = user;
}

void proto_nmdc_user_freelist_clear ()
{
  /* destroy freelist */
  while (freelist) {
    nmdc_user_t *o;

    o = freelist;
    freelist = (nmdc_user_t *) freelist->user.next;

    if (o->tthlist)
      free (o->tthlist);

    if (o->MyINFO) {
      bf_free (o->MyINFO);
      o->MyINFO = NULL;
    }

    if (o->user.plugin_priv)
      plugin_del_user ((plugin_private_t **) & o->user.plugin_priv);

    free (o);
  }
}

/******************************************************************************\
**                                                                            **
**                          CACHELIST HANDLING                                **
**                                                                            **
\******************************************************************************/

void proto_nmdc_user_cachelist_add (nmdc_user_t * user)
{
  user->user.next = (user_t *) cachelist;
  if (user->user.next) {
    user->user.next->prev = (user_t *) user;
  } else {
    cachelist_last = user;
  }
  user->user.prev = NULL;
  cachelist = user;
  cachelist_count++;
}

void proto_nmdc_user_cachelist_invalidate (nmdc_user_t * u)
{
  u->user.joinstamp = 0;
  hash_deluser (&cachehashlist, &u->user.hash);
}

void proto_nmdc_user_cachelist_clear ()
{
  buffer_t *buf;
  nmdc_user_t *u, *p;
  time_t now;

  time (&now);
  now -= config.DelayedLogout;
  for (u = cachelist_last; u; u = p) {
    p = (nmdc_user_t *) u->user.prev;
    if (u->user.joinstamp >= (unsigned) now)
      continue;

    /* a joinstamp of 0 means the user rejoined */
    if (u->user.joinstamp) {
      /* queue the quit message */
      buf = bf_alloc (8 + NICKLENGTH);
      bf_strcat (buf, "$Quit ");
      bf_strcat (buf, u->user.nick);
      cache_queue (cache.myinfo, NULL, buf);
      cache_queue (cache.myinfoupdateop, NULL, buf);
      bf_free (buf);
      nicklistcache_deluser (u);

      /* remove from hashlist */
      hash_deluser (&cachehashlist, &u->user.hash);
    }

    /* remove from list */
    if (u->user.next) {
      u->user.next->prev = u->user.prev;
    } else {
      cachelist_last = (nmdc_user_t *) u->user.prev;
    }
    if (u->user.prev) {
      u->user.prev->next = u->user.next;
    } else {
      cachelist = (nmdc_user_t *) u->user.next;
    };
    cachelist_count--;

    /* put user in freelist */
    proto_nmdc_user_freelist_add (u);
    u = NULL;
  }
}

/******************************************************************************\
**                                                                            **
**                             CHAT HANDLING                                  **
**                                                                            **
\******************************************************************************/

unsigned int proto_nmdc_user_flush (nmdc_user_t * u)
{
  buffer_t *b = NULL, *buffer;
  string_list_entry_t *le;

  if (u->state == PROTO_STATE_DISCONNECTED)
    return 0;
  if (u->state == PROTO_STATE_VIRTUAL)
    return 0;

  buffer = bf_alloc (10240);

  for (le = u->privatemessages.messages.first; le; le = le->next) {
    /* data and length */
    b = le->data;
    bf_strncat (buffer, b->s, bf_used (b));
    bf_strcat (buffer, "|");

    u->MessageCnt--;
    u->CacheException--;
  }
  cache_clear ((u->privatemessages));

  server_write (u->user.parent, buffer);

  bf_free (buffer);

  return 0;
}

__inline__ int proto_nmdc_user_say (nmdc_user_t * u, buffer_t * b, buffer_t * message)
{
  bf_strcat (b, "<");
  bf_strcat (b, u->user.nick);
  bf_strcat (b, "> ");
  for (; message; message = message->next)
    bf_strncat (b, message->s, bf_used (message));
  if (*(b->e - 1) == '\n')
    b->e--;
  bf_strcat (b, "|");
  return 0;
}

__inline__ int proto_nmdc_user_say_string (nmdc_user_t * u, buffer_t * b, unsigned char *message)
{
  bf_strcat (b, "<");
  bf_strcat (b, u->user.nick);
  bf_strcat (b, "> ");
  bf_strcat (b, message);
  if (*(b->e - 1) == '\n')
    b->e--;
  bf_strcat (b, "|");
  return 0;
}

int proto_nmdc_user_chat_all (nmdc_user_t * u, buffer_t * message)
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

int proto_nmdc_user_send (nmdc_user_t * u, nmdc_user_t * target, buffer_t * message)
{
  buffer_t *buf;

  if (target->state == PROTO_STATE_DISCONNECTED)
    return EINVAL;

  buf = bf_alloc (32 + NICKLENGTH + bf_used (message));

  proto_nmdc_user_say (u, buf, message);

  cache_queue (target->privatemessages, u, buf);
  cache_count (privatemessages, target);
  target->MessageCnt++;
  target->CacheException++;

  bf_free (buf);

  return 0;
}

int proto_nmdc_user_send_direct (nmdc_user_t * u, nmdc_user_t * target, buffer_t * message)
{
  buffer_t *buf;

  if (target->state == PROTO_STATE_DISCONNECTED)
    return EINVAL;

  buf = bf_alloc (32 + NICKLENGTH + bf_used (message));

  proto_nmdc_user_say (u, buf, message);

  server_write (target->user.parent, buf);

  bf_free (buf);

  return 0;
}

int proto_nmdc_user_priv (nmdc_user_t * u, nmdc_user_t * target, nmdc_user_t * source,
			  buffer_t * message)
{
  buffer_t *buf;

  if (target->state == PROTO_STATE_DISCONNECTED)
    return 0;

  buf = bf_alloc (32 + 3 * NICKLENGTH + bf_used (message));

  bf_printf (buf, "$To: %s From: %s $<%s> ", target->user.nick, u->user.nick, source->user.nick);
  for (; message; message = message->next)
    bf_strncat (buf, message->s, bf_used (message));
  if (*(buf->e - 1) == '\n')
    buf->e--;
  if (buf->e[-1] != '|')
    bf_strcat (buf, "|");

  if (target->state == PROTO_STATE_VIRTUAL) {
    plugin_send_event (nmdc_pluginmanager, target->user.plugin_priv, PLUGIN_EVENT_PM_IN, buf);

    bf_free (buf);
    return 0;
  }
  cache_queue (target->privatemessages, u, buf);
  cache_count (privatemessages, target);

  target->MessageCnt++;
  target->CacheException++;

  bf_free (buf);

  return 0;
}

int proto_nmdc_user_priv_direct (nmdc_user_t * u, nmdc_user_t * target, nmdc_user_t * source,
				 buffer_t * message)
{
  buffer_t *buf;

  if (target->state == PROTO_STATE_DISCONNECTED)
    return 0;

  buf = bf_alloc (32 + 3 * NICKLENGTH + bf_used (message));

  bf_printf (buf, "$To: %s From: %s $<%s> ", target->user.nick, u->user.nick, source->user.nick);
  for (; message; message = message->next)
    bf_strncat (buf, message->s, bf_used (message));
  if (*(buf->e - 1) == '\n')
    buf->e--;
  bf_strcat (buf, "|");

  if (target->state == PROTO_STATE_VIRTUAL) {
    plugin_send_event (nmdc_pluginmanager, target->user.plugin_priv, PLUGIN_EVENT_PM_IN, buf);

    bf_free (buf);
    return 0;
  }

  server_write (target->user.parent, buf);

  bf_free (buf);

  return 0;
}

int proto_nmdc_user_raw (nmdc_user_t * target, buffer_t * message)
{
  buffer_t *buf;

  if (target->state == PROTO_STATE_DISCONNECTED)
    return EINVAL;

  buf = bf_copy (message, 0);

  cache_queue (target->privatemessages, target, buf);
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

nmdc_user_t *proto_nmdc_user_alloc (void *priv, unsigned long ipaddress)
{
  nmdc_user_t *user;

  /* do we have a connect token? */
  if (!get_token (&rates.connects, &connects, now.tv_sec)) {
    proto_nmdc_warn (&now, "Users refused because of login rate.");
    return NULL;
  }

  /* yes, create and init user */
  user = malloc (sizeof (nmdc_user_t));
  if (!user)
    return NULL;
  memset (user, 0, sizeof (nmdc_user_t));

  user->tthlist = tth_list_alloc (researchmaxcount);

  user->state = PROTO_STATE_INIT;
  user->user.parent = priv;
  user->user.ipaddress = ipaddress;

  init_bucket (&user->rate_warnings, now.tv_sec);
  init_bucket (&user->rate_violations, now.tv_sec);
  init_bucket (&user->rate_chat, now.tv_sec);
  init_bucket (&user->rate_search, now.tv_sec);
  init_bucket (&user->rate_myinfo, now.tv_sec);
  init_bucket (&user->rate_myinfoop, now.tv_sec);
  init_bucket (&user->rate_getnicklist, now.tv_sec);
  init_bucket (&user->rate_getinfo, now.tv_sec);
  init_bucket (&user->rate_downloads, now.tv_sec);

  /* warnings and violationss start with a full token load ! */
  user->rate_warnings.tokens = rates.warnings.burst;
  user->rate_violations.tokens = rates.violations.burst;

  /* add user to the list... */
  user->user.next = (user_t *) userlist;
  if (user->user.next)
    user->user.next->prev = &user->user;
  user->user.prev = NULL;

  userlist = user;

  nmdc_stats.userjoin++;

  return user;
}

int proto_nmdc_user_free (nmdc_user_t * user)
{

  /* remove from the current user list */
  if (user->user.next)
    user->user.next->prev = user->user.prev;

  if (user->user.prev) {
    user->user.prev->next = user->user.next;
  } else {
    userlist = (nmdc_user_t *) user->user.next;
  };

  user->user.parent = NULL;

  /* if the user was online, put him in the cachelist. if he was kicked, don't. */
  if (!(user->user.flags & NMDC_FLAG_WASONLINE) || (user->user.flags & NMDC_FLAG_WASKICKED)) {
    proto_nmdc_user_freelist_add (user);
  } else {
    proto_nmdc_user_cachelist_add (user);
  }

  nmdc_stats.userpart++;

  return 0;
}

nmdc_user_t *proto_nmdc_user_find (unsigned char *nick)
{
  if (!nick) {
    nmdc_user_t *u = userlist;

    while (u && (u->state != PROTO_STATE_ONLINE))
      u = (nmdc_user_t *) u->user.next;

    return u;
  }
  return (nmdc_user_t *) hash_find_nick (&hashlist, nick, strlen (nick));
}

nmdc_user_t *proto_nmdc_user_find_ip (nmdc_user_t * last, unsigned long ip)
{
  return (nmdc_user_t *) hash_find_ip_next (&hashlist, (user_t *) last, ip);
}

nmdc_user_t *proto_nmdc_user_find_net (nmdc_user_t * last, unsigned long ip, unsigned long netmask)
{
  return (nmdc_user_t *) hash_find_net_next (&hashlist, (user_t *) last, ip, netmask);
}

int proto_nmdc_user_disconnect (nmdc_user_t * u, char *reason)
{
  buffer_t *buf;

  if (u->state == PROTO_STATE_DISCONNECTED)
    return 0;

  plugin_send_event (nmdc_pluginmanager, u->user.plugin_priv, PLUGIN_EVENT_DISCONNECT, reason);

  /* if user was online, clear out all stale data */
  if (u->state == PROTO_STATE_ONLINE) {
    u->user.flags &= ~PROTO_FLAG_ONLINE;

    string_list_purge (&cache.myinfo.messages, u);
    string_list_purge (&cache.myinfoupdate.messages, u);
    string_list_purge (&cache.myinfoupdateop.messages, u);
    string_list_purge (&cache.asearch.messages, u);
    string_list_purge (&cache.psearch.messages, u);
    string_list_clear (&u->results.messages);
    string_list_clear (&u->privatemessages.messages);

    plugin_send_event (nmdc_pluginmanager, u->user.plugin_priv, PLUGIN_EVENT_LOGOUT, NULL);

    hash_deluser (&hashlist, &u->user.hash);

    /* kicked users do not go on the cachehashlist */
    if (u->user.flags & NMDC_FLAG_WASKICKED) {
      nicklistcache_deluser (u);
      buf = bf_alloc (8 + NICKLENGTH);
      bf_strcat (buf, "$Quit ");
      bf_strcat (buf, u->user.nick);
      cache_queue (cache.myinfo, NULL, buf);
      cache_queue (cache.myinfoupdateop, NULL, buf);
      bf_free (buf);
    } else {
      hash_adduser (&cachehashlist, (user_t *) u);
      u->user.flags |= NMDC_FLAG_WASONLINE;
    }
  } else {
    /* if the returned user has same nick, but different user pointer, this is legal */
    ASSERT (u != (nmdc_user_t *) hash_find_nick (&hashlist, u->user.nick, strlen (u->user.nick)));
  }

  if (u->supports & NMDC_SUPPORTS_ZLine)
    cache.ZlineSupporters--;
  if (u->supports & NMDC_SUPPORTS_ZPipe)
    cache.ZpipeSupporters--;

  u->state = PROTO_STATE_DISCONNECTED;

  return 0;
}


int proto_nmdc_user_forcemove (nmdc_user_t * u, unsigned char *destination, buffer_t * message)
{
  buffer_t *b;

  if (u->state == PROTO_STATE_DISCONNECTED)
    return 0;

  DPRINTF ("Redirecting user %s to %s because %.*s\n", u->user.nick, destination,
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

  server_write (u->user.parent, b);
  bf_free (b);

  u->user.flags &= NMDC_FLAG_WASKICKED;

  if (u->state != PROTO_STATE_DISCONNECTED)
    server_disconnect_user (u->user.parent, "User forcemoved.");

  nmdc_stats.forcemove++;

  return 0;
}

int proto_nmdc_user_drop (nmdc_user_t * u, buffer_t * message)
{
  buffer_t *b;

  if (u->state == PROTO_STATE_DISCONNECTED)
    return 0;

  if (u->MessageCnt)
    proto_nmdc_user_flush (u);

  if (message) {
    b = bf_alloc (265 + bf_used (message));

    proto_nmdc_user_say (HubSec, b, message);

    server_write (u->user.parent, b);
    bf_free (b);
  }

  u->user.flags &= NMDC_FLAG_WASKICKED;

  server_disconnect_user (u->user.parent, "User dropped");

  nmdc_stats.disconnect++;

  return 0;
}

int proto_nmdc_user_redirect (nmdc_user_t * u, buffer_t * message)
{
  buffer_t *b;

  if (u->state == PROTO_STATE_DISCONNECTED)
    return 0;

  /* call plugin first. it can do custom redirects */
  if (plugin_send_event (nmdc_pluginmanager, u->user.plugin_priv, PLUGIN_EVENT_REDIRECT, message) !=
      PLUGIN_RETVAL_CONTINUE) {
    return 0;
  }

  if (u->MessageCnt)
    proto_nmdc_user_flush (u);

  b = bf_alloc (265 + NICKLENGTH + strlen (config.Redirect) + bf_used (message));

  if (message)
    proto_nmdc_user_say (HubSec, b, message);

  if (config.Redirect && *config.Redirect) {
    bf_strcat (b, "$ForceMove ");
    bf_strcat (b, config.Redirect);
    bf_strcat (b, "|");
  }

  u->user.flags &= NMDC_FLAG_WASKICKED;

  server_write (u->user.parent, b);
  bf_free (b);

  if (u->state != PROTO_STATE_DISCONNECTED)
    server_disconnect_user (u->user.parent, "User redirected");

  nmdc_stats.redirect++;

  return 0;
}

int proto_nmdc_violation (nmdc_user_t * u, struct timeval *now, char *reason)
{
  buffer_t *buf, *report;
  struct in_addr addr;
  nmdc_user_t *t;

  /* if there are still tokens left, just return */
  if (get_token (&rates.violations, &u->rate_violations, now->tv_sec))
    return 0;

  /* never do this for owners! */
  if (u->user.rights & CAP_OWNER)
    return 0;

  /* user is in violation */

  /* if he is only a short time online, this is most likely a spammer and he will be hardbanned */
  buf = bf_alloc (128);
  if ((u->user.joinstamp - now->tv_sec) < config.ProbationPeriod) {
    bf_printf (buf, _("Rate Probation Violation (Last: %s)."), reason);
    banlist_add (&nmdc_server->hardbanlist, HubSec->user.nick, u->user.nick, u->user.ipaddress,
		 0xFFFFFFFF, buf, 0);

  } else {
    bf_printf (buf, _("Rate Violation (Last: %s)."), reason);
    banlist_add (&nmdc_server->hardbanlist, HubSec->user.nick, u->user.nick, u->user.ipaddress,
		 0xFFFFFFFF, buf, now->tv_sec + config.ViolationBantime);
  }

  u->user.flags &= NMDC_FLAG_WASKICKED;

  /* send message */
  server_write (u->user.parent, buf);

  /* disconnect the user */
  if (u->state != PROTO_STATE_DISCONNECTED)
    server_disconnect_user (u->user.parent, buf->s);

  nmdc_stats.userviolate++;

  report = bf_alloc (1024);

  addr.s_addr = u->user.ipaddress;
  bf_printf (report, _("Flood detected: %s (%s) was banned: %.*s (Last violation: %s)\n"),
	     u->user.nick, inet_ntoa (addr), bf_used (buf), buf->s, reason);

  t =
    (nmdc_user_t *) hash_find_nick (&hashlist, config.SysReportTarget,
				    strlen (config.SysReportTarget));
  if (!t)
    return 0;

  proto_nmdc_user_priv_direct (HubSec, t, HubSec, buf);

  bf_free (report);
  bf_free (buf);

  return -1;
}

int proto_nmdc_user_warn (nmdc_user_t * u, struct timeval *now, const char *message, ...)
{
  buffer_t *buf;
  va_list ap;

  if (!get_token (&rates.warnings, &u->rate_warnings, now->tv_sec)) {
    return 0;
  }

  buf = bf_alloc (10240);

  bf_printf (buf, "<%s>", HubSec->user.nick);
  bf_printf (buf, _("WARNING: "));

  va_start (ap, message);
  bf_vprintf (buf, message, ap);
  va_end (ap);

  bf_strcat (buf, "|");

  server_write (u->user.parent, buf);

  bf_free (buf);

  return 1;
}

int proto_nmdc_warn (struct timeval *now, const char *message, ...)
{
  nmdc_user_t *u;
  buffer_t *buf;
  va_list ap;

  u =
    (nmdc_user_t *) hash_find_nick (&hashlist, config.SysReportTarget,
				    strlen (config.SysReportTarget));
  if (!u)
    return 0;

  if (!get_token (&rates.warnings, &rate_warnings, now->tv_sec))
    return 0;

  buf = bf_alloc (10240);

  bf_printf (buf, _("WARNING: "));

  va_start (ap, message);
  bf_vprintf (buf, message, ap);
  va_end (ap);

  proto_nmdc_user_priv_direct (HubSec, u, HubSec, buf);

  bf_free (buf);

  return 1;
}

/******************************************************************************\
**                                                                            **
**                                INPUT HANDLING                              **
**                                                                            **
\******************************************************************************/

int proto_nmdc_handle_input (nmdc_user_t * user, buffer_t ** buffers)
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
         server_disconnect_user (user->user.parent, _("EPIPE"));
       */
      ASSERT (!((errno == EPIPE) && user->user.parent));
      bf_free (b);
      break;
    }
    bf_free (b);
    /* if parent is freed "buffers" is not longer valid */
    if (user->state == PROTO_STATE_DISCONNECTED)
      break;

    gettime ();
  }

  proto_nmdc_user_freelist_clear ();

  return 0;
}

/******************************************************************************\
**                                                                            **
**                                INIT HANDLING                               **
**                                                                            **
\******************************************************************************/

proto_t *proto_nmdc_init ()
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
  init_bucket_type (&rates.myinfoop, 120, 1, 1);
  init_bucket_type (&rates.getnicklist, 1200, 1, 1);
  init_bucket_type (&rates.getinfo, 1, 10, 10);
  init_bucket_type (&rates.downloads, 5, 6, 1);
  init_bucket_type (&rates.connects, 1, 10, 10);
  init_bucket_type (&rates.psresults_in, 15, 100, 25);
  init_bucket_type (&rates.psresults_out, 15, 50, 25);
  init_bucket_type (&rates.warnings, 120, 10, 1);
  init_bucket_type (&rates.violations, 20, 10, 3);

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
		   "Period of MyINFO messages. This controls how often a user can send a MyINFO message that is send to everyone. Keep this very high.");
  config_register ("rate.myinfo.burst", CFG_ELEM_ULONG, &rates.myinfo.burst,
		   "Burst of MyINFO messages. This controls how many MyINFO messages a user can 'save up' (everyone). Keep this at 1.");
  config_register ("rate.myinfoop.period", CFG_ELEM_ULONG, &rates.myinfoop.period,
		   "Period of MyINFO messages. This controls how often a user can send a MyINFO message that is send on to the ops only. Keep this very high.");
  config_register ("rate.myinfoop.burst", CFG_ELEM_ULONG, &rates.myinfoop.burst,
		   "Burst of MyINFO messages. This controls how many MyINFO messages a user can 'save up' (OPs messages only). Keep this at 1.");
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

  config_register ("rate.violations.period", CFG_ELEM_ULONG, &rates.violations.period,
		   "Period of user violations. This controls how often a warning is send to user that overstep limits.");
  config_register ("rate.violations.refill", CFG_ELEM_ULONG, &rates.violations.refill,
		   "Period of user violations. This controls how many warning a user gets within the period.");
  config_register ("rate.violations.burst", CFG_ELEM_ULONG, &rates.violations.burst,
		   "Period of user violations. This controls how many violations a user that overstep limits can save up.");

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
  researchmininterval = DEFAULT_RESEARCH_MININTERVAL;
  researchperiod = DEFAULT_RESEARCH_PERIOD;
  researchmaxcount = DEFAULT_RESEARCH_MAXCOUNT;
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
  hash_init (&cachehashlist);
  token_init ();
  init_bucket (&rate_warnings, now.tv_sec);
  rate_warnings.tokens = rates.warnings.burst;

  memset (&nmdc_stats, 0, sizeof (nmdc_stats_t));

  banlist_init (&reconnectbanlist);
  banlist_init (&softbanlist);

  return &nmdc_proto;
}

proto_t *proto_nmdc_setup ()
{
  HubSec = proto_nmdc_user_addrobot (config.HubSecurityNick, config.HubSecurityDesc);
  plugin_new_user (nmdc_pluginmanager, (plugin_private_t **) & HubSec->user.plugin_priv,
		   &HubSec->user);

  nmdc_proto.HubSec = &HubSec->user;

  return &nmdc_proto;
}
