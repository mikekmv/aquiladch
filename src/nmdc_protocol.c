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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <time.h>

#include "../config.h"
#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif

#include "hash.h"
#include "user.h"
#include "config.h"
#include "core_config.h"
#include "plugin_int.h"
#include "utils.h"

#include "hashlist_func.h"

#include "nmdc_utils.h"
#include "nmdc_token.h"
#include "nmdc_protocol.h"


/* for format size */
#ifdef DEBUG
#include "utils.h"
#endif

/******************************************************************************\
**                                                                            **
**                                  DEFINES                                   **
**                                                                            **
\******************************************************************************/

#define SKIPTOCHAR(var, end, ch)	for (; *var && (*var != ch) && (var < end); *var++)

/******************************************************************************\
**                                                                            **
**                            GLOBAL VARIABLES                                **
**                                                                            **
\******************************************************************************/

/* globals */
unsigned int keylen = 0;
unsigned int keyoffset = 0;
char key[16 + sizeof (LOCK) + 4 + LOCKLENGTH + 1];

/* local hub cache */
cache_t cache;
ratelimiting_t rates;
leaky_bucket_t connects;
nmdc_stats_t nmdc_stats;

unsigned int cloning;

unsigned int chatmaxlength;
unsigned int searchmaxlength;
unsigned int srmaxlength;
unsigned int researchmininterval, researchperiod, researchmaxcount;

unsigned char *defaultbanmessage = NULL;

/* special users */
user_t *HubSec;

/* users currently logged in */
user_t *userlist = NULL;
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
**                            NICKLIST CACHING                                **
**                                                                            **
\******************************************************************************/

int nicklistcache_adduser (user_t * u)
{
  unsigned long l;

  cache.length_estimate += strlen (u->nick) + 2;
  cache.length_estimate_info += bf_used (u->MyINFO) + 1;	/* add one for the | */
  u->flags |= NMDC_FLAG_CACHED;

  cache.usercount++;

  if (u->op) {
    cache.length_estimate_op += strlen (u->nick) + 2;
    cache.needrebuild = 1;
  }

  if (cache.needrebuild)
    return 0;

  /* append the myinfo message */
  l = bf_used (u->MyINFO);
  if (l >= bf_unused (cache.infolistupdate)) {
    cache.needrebuild = 1;
    return 0;
  }

  bf_strncat (cache.infolistupdate, u->MyINFO->s, l);
  bf_strcat (cache.infolistupdate, "|");
  bf_printf (cache.hellolist, "$Hello %s|", u->nick);

  BF_VERIFY (cache.infolistupdate);
  BF_VERIFY (cache.hellolist);

  return 0;
}

int nicklistcache_updateuser (user_t * u, buffer_t * new)
{
  unsigned long l;

  cache.length_estimate_info -= bf_used (u->MyINFO);
  cache.length_estimate_info += bf_used (new);

  if (cache.needrebuild)
    return 0;

  l = bf_used (new);
  if (l > bf_unused (cache.infolistupdate)) {
    cache.needrebuild = 1;
    return 0;
  }

  bf_strncat (cache.infolistupdate, new->s, l);
  bf_strcat (cache.infolistupdate, "|");

  BF_VERIFY (cache.infolistupdate);

  return 0;
}

int nicklistcache_deluser (user_t * u)
{
  if (!(u->flags & NMDC_FLAG_CACHED))
    return 0;

  cache.length_estimate -= (strlen (u->nick) + 2);
  cache.length_estimate_info -= (bf_used (u->MyINFO) + 1);

  cache.usercount--;

  if (u->op) {
    cache.needrebuild = 1;
    cache.length_estimate_op -= (strlen (u->nick) + 2);
  }

  if (cache.needrebuild)
    return 0;

  if (bf_unused (cache.infolistupdate) < (strlen (u->nick) + 8)) {
    cache.needrebuild = 1;
    return 0;
  }

  bf_printf (cache.infolistupdate, "$Quit %s|", u->nick);
  bf_printf (cache.hellolist, "$Quit %s|", u->nick);

  BF_VERIFY (cache.infolistupdate);
  BF_VERIFY (cache.hellolist);

  return 0;
}

int nicklistcache_rebuild (struct timeval now)
{
  unsigned char *s, *o;
  user_t *t;

  nmdc_stats.cacherebuild++;

  DPRINTF (" Rebuilding cache\n");
#ifdef ZLINES
  if (cache.infolistzpipe != cache.infolist)
    bf_free (cache.infolistzpipe);
  cache.infolistzpipe = NULL;
  if (cache.infolistzline != cache.infolist)
    bf_free (cache.infolistzline);
  cache.infolistzline = NULL;

  if (cache.nicklistzpipe != cache.nicklist)
    bf_free (cache.nicklistzpipe);
  cache.nicklistzpipe = NULL;
  if (cache.nicklistzline != cache.nicklist)
    bf_free (cache.nicklistzline);
  cache.nicklistzline = NULL;
#endif
  bf_free (cache.nicklist);
  bf_free (cache.oplist);
  bf_free (cache.infolist);
  bf_free (cache.infolistupdate);
  bf_free (cache.hellolist);

  cache.nicklist = bf_alloc (cache.length_estimate + 32);
  s = cache.nicklist->buffer;
  cache.oplist = bf_alloc (cache.length_estimate_op + 32);
  o = cache.oplist->buffer;
  cache.infolist = bf_alloc (cache.length_estimate_info + 32);
  cache.infolistupdate = bf_alloc ((cache.length_estimate_info >> NICKLISTCACHE_SPARE) + 32);
  cache.hellolist = bf_alloc (cache.length_estimate_info + 32);

  s += sprintf (s, "$NickList ");
  o += sprintf (o, "$OpList ");
  for (t = userlist; t; t = t->next) {
    if ((t->state != PROTO_STATE_ONLINE) && (t->state != PROTO_STATE_VIRTUAL))
      continue;
    bf_strncat (cache.infolist, t->MyINFO->s, bf_used (t->MyINFO));
    bf_strcat (cache.infolist, "|");
    s += sprintf (s, "%s$$", t->nick);
    if (t->op)
      o += sprintf (o, "%s$$", t->nick);
  }
  strcat (s++, "|");
  cache.nicklist->e = s;
  strcat (o++, "|");
  cache.oplist->e = o;

  cache.needrebuild = 0;
  cache.lastrebuild = now.tv_sec;

#ifdef ZLINES
  zline (cache.infolist, cache.ZpipeSupporters ? &cache.infolistzpipe : NULL,
	 cache.ZlineSupporters ? &cache.infolistzline : NULL);
  zline (cache.nicklist, cache.ZpipeSupporters ? &cache.nicklistzpipe : NULL,
	 cache.ZlineSupporters ? &cache.nicklistzline : NULL);
#endif

  BF_VERIFY (cache.infolist);
  BF_VERIFY (cache.nicklist);
  BF_VERIFY (cache.oplist);
#ifdef ZLINES
  BF_VERIFY (cache.infolistzline);
  BF_VERIFY (cache.infolistzpipe);
  BF_VERIFY (cache.nicklistzline);
  BF_VERIFY (cache.nicklistzpipe);
#endif

  return 0;
}

int nicklistcache_sendnicklist (user_t * target)
{
  struct timeval now;

  gettimeofday (&now, NULL);
  if ((now.tv_sec - cache.lastrebuild) > PROTOCOL_REBUILD_PERIOD)
    cache.needrebuild = 1;

  if (cache.needrebuild)
    nicklistcache_rebuild (now);

  /* do not send out a nicklist to nohello clients: they have enough with the infolist 
   * unless they do not support NoGetINFO. Very nice. NOT.
   */
  if (!(target->supports & NMDC_SUPPORTS_NoHello)) {
#ifdef ZLINES
    if (target->supports & NMDC_SUPPORTS_ZPipe) {
      server_write_credit (target->parent, cache.nicklistzpipe);
    } else if (target->supports & NMDC_SUPPORTS_ZLine) {
      server_write_credit (target->parent, cache.nicklistzline);
    } else {
      server_write_credit (target->parent, cache.nicklist);
    }
#else
    server_write_credit (target->parent, cache.nicklist);
#endif
  } else {
    if (!(target->supports & NMDC_SUPPORTS_NoGetINFO)) {
      server_write_credit (target->parent, cache.nicklist);
    }
  }
  /* always send the oplist */
  server_write_credit (target->parent, cache.oplist);

  /* clients that support NoGetINFO get a infolist and a infolistupdate, other get a  hello list update */
  if (target->supports & NMDC_SUPPORTS_NoGetINFO) {
#ifdef ZLINES
    if (target->supports & NMDC_SUPPORTS_ZPipe) {
      server_write_credit (target->parent, cache.infolistzpipe);
    } else if (target->supports & NMDC_SUPPORTS_ZLine) {
      server_write_credit (target->parent, cache.infolistzline);
    } else {
      server_write_credit (target->parent, cache.infolist);
    }
#else
    server_write_credit (target->parent, cache.infolist);
#endif
    server_write_credit (target->parent, cache.infolistupdate);
  } else {
    server_write_credit (target->parent, cache.hellolist);
  }

  return 0;
}

int nicklistcache_sendoplist (user_t * target)
{
  struct timeval now;

  gettimeofday (&now, NULL);
  nicklistcache_rebuild (now);

  /* write out to all users except target. */
  target->SearchCnt++;
  target->CacheException++;

  /* we set the user to NULL so we don't get deleted by a search from this user */
  cache_queue (cache.asearch, NULL, cache.oplist);
  cache_queue (cache.psearch, NULL, cache.oplist);

  return 0;
}

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

  b = bf_alloc (265 + bf_used (message));

  proto_nmdc_user_say (HubSec, b, message);

  server_write (u->parent, b);
  bf_free (b);

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

/******************************************************************************\
**                                                                            **
**                    PROTOCOL HANDLING PER STATE                             **
**                                                                            **
\******************************************************************************/

/********************
 *  State INIT
 */

/* define in main.c :S */
extern struct timeval boottime;

int proto_nmdc_state_init (user_t * u, token_t * tkn)
{
  unsigned int i;
  int retval = 0;
  buffer_t *output;
  struct timeval now;

  gettimeofday (&now, NULL);
  timersub (&now, &boottime, &now);

  output = bf_alloc (2048);
  output->s[0] = '\0';

  bf_strcat (output, "$Lock EXTENDEDPROTOCOL" LOCK "[[");
  /* generate LOCKLENGTH characters between 32(' ') and 122('z') */
  for (i = 0; i < LOCKLENGTH; i++) {
    u->lock[i] = (random () % 90) + 33;
  }

  bf_strncat (output, u->lock, LOCKLENGTH);
  bf_strncat (output, "]] Pk=Aquila|", 13);
  bf_strcat (output, "<");
  bf_strcat (output, HubSec->nick);
  bf_strcat (output, "> This hub is running Aquila Version " AQUILA_VERSION " (Uptime ");
  time_print (output, now.tv_sec);
  bf_printf (output, ".%.3lu).|", now.tv_usec / 1000);
  retval = server_write (u->parent, output);

  if (u->state == PROTO_STATE_DISCONNECTED)
    return retval;

  u->state = PROTO_STATE_SENDLOCK;
  server_settimeout (u->parent, PROTO_TIMEOUT_SENDLOCK);

  bf_free (output);

  return retval;
}

/********************
 *  State SENDLOCK
 */

int proto_nmdc_state_sendlock (user_t * u, token_t * tkn)
{
  int retval = 0;
  unsigned char *k, *l;
  buffer_t *output;

  output = bf_alloc (2048);
  output->s[0] = '\0';


  switch (tkn->type) {
    case TOKEN_SUPPORTS:
      k = tkn->argument;

      for (k = tkn->argument; *k; k = l) {
	while (*k && (*k == ' '))
	  k++;
	if (!*k)
	  break;
	l = k + 1;
	while (*l && (*l != ' '))
	  l++;

	/* check length too? */
	switch (*k) {
	  case 'N':
	    if (!strncmp (k, "NoGetINFO", 9)) {
	      u->supports |= NMDC_SUPPORTS_NoGetINFO;
	      continue;
	    }
	    if (!strncmp (k, "NoHello", 7)) {
	      u->supports |= NMDC_SUPPORTS_NoHello;
	      continue;
	    }
	    break;
	  case 'U':
	    if (!strncmp (k, "UserIP2", 7)) {
	      u->supports |= NMDC_SUPPORTS_UserIP2;
	      continue;
	    }
	    if (!strncmp (k, "UserCommand", 11)) {
	      u->supports |= NMDC_SUPPORTS_UserCommand;
	      continue;
	    }
	    break;
	  case 'Q':
	    if (!strncmp (k, "QuickList", 9)) {
	      u->supports |= NMDC_SUPPORTS_QuickList;
	      continue;
	    }
	    break;
	  case 'T':
	    if (!strncmp (k, "TTHSearch", 9)) {
	      u->supports |= NMDC_SUPPORTS_TTHSearch;
	      continue;
	    }
	    break;
	  case 'Z':
	    if (!strncmp (k, "ZPipe", 5)) {
	      u->supports |= NMDC_SUPPORTS_ZPipe;
	      /* if thisis the first zpipe user, we need to do a cache rebuild to build the zpipe buffer */
	      if (!cache.ZpipeSupporters++)
		cache.needrebuild = 1;
	      continue;
	    }
	    if (!strncmp (k, "ZLine", 5)) {
	      u->supports |= NMDC_SUPPORTS_ZLine;
	      /* if thisis the first zline user, we need to do a cache rebuild to build the zline buffer */
	      if (!cache.ZlineSupporters++)
		cache.needrebuild = 1;
	      continue;
	    }
	    break;
	  default:
	    DPRINTF ("Unknown Support flag %.*s\n", (int) (l - k), k);
	}
      }
      bf_strcat (output, "$Supports NoGetINFO NoHello BotINFO"
#ifdef ZLINES
		 " ZLine"
#endif
		 "|");

      server_settimeout (u->parent, PROTO_TIMEOUT_SENDLOCK);
      retval = server_write (u->parent, output);
      break;
    case TOKEN_KEY:
      {
	unsigned int i, j;
	unsigned char c;

	j = unescape_string (tkn->argument, strlen (tkn->argument));
	/* validate length */
	if (j != keylen)
	  goto broken_key;

	/* xor the lock from the key */
	for (i = 0; i < LOCKLENGTH; i++) {
	  c = ((u->lock[i] << 4) & 240) | ((u->lock[i] >> 4) & 15);
	  tkn->argument[keyoffset + i] ^= c;
	  tkn->argument[keyoffset + i + 1] ^= c;
	}

	/* compare extracted key with stored key */
	if (memcmp (tkn->argument, key, keylen))
	  goto broken_key;

	u->state = PROTO_STATE_WAITNICK;
	server_settimeout (u->parent, PROTO_TIMEOUT_WAITNICK);

	break;
      broken_key:
	DPRINTF ("ILLEGAL KEY\n");
	nmdc_stats.brokenkey++;
	server_disconnect_user (u->parent);
	retval = -1;
	break;
      }
  }

  bf_free (output);

  return retval;
}

/********************
 *  State WAITNICK
 */

int proto_nmdc_state_waitnick (user_t * u, token_t * tkn)
{
  int retval = 0;
  buffer_t *output;
  banlist_entry_t *ban;
  user_t *existing_user;
  account_t *a;
  struct timeval now;

  if (tkn->type != TOKEN_VALIDATENICK)
    return 0;

  output = bf_alloc (2048);
  output->s[0] = '\0';

  /* nick already used ? */
  do {
    strncpy (u->nick, tkn->argument, NICKLENGTH);
    u->nick[NICKLENGTH - 1] = 0;

    existing_user = hash_find_nick (&hashlist, u->nick, strlen (u->nick));

    /* the existing user is a bot or chatroom? deny user. */
    if (existing_user && (existing_user->state == PROTO_STATE_VIRTUAL)) {
      bf_strcat (output, "$ValidateDenide ");
      bf_strcat (output, u->nick);
      bf_strcat (output, "|");
      retval = server_write (u->parent, output);
      proto_nmdc_user_redirect (u, bf_buffer ("Your nickname is already in use."));
      nmdc_stats.usednick++;
      retval = -1;
      break;
    }

    bf_strcat (output, "$HubName ");
    bf_strcat (output, config.HubName);
    bf_strcat (output, "|");

    /* does the user have an account ? */
    if ((a = account_find (u->nick))) {
      if (a->passwd[0]) {
	/* ask for users password */
	u->flags |= PROTO_FLAG_REGISTERED;

	bf_strcat (output, "$GetPass|");
	u->state = PROTO_STATE_WAITPASS;
	server_settimeout (u->parent, PROTO_TIMEOUT_WAITPASS);
	retval = server_write (u->parent, output);
	break;
      } else {
	/* assign CAP_SHARE and CAP_TAG anyway */
	u->rights =
	  config.DefaultRights | ((a->rights | a->classp->rights) & (CAP_SHARE | CAP_TAG));
	proto_nmdc_user_say_string (HubSec, output,
				    "Your account priviliges will not be awarded until you set a password.");
      }
    }

    /* if user exists */
    if (existing_user) {
      if (existing_user->ipaddress != u->ipaddress) {
	bf_strcat (output, "$ValidateDenide ");
	bf_strcat (output, u->nick);
	bf_strcat (output, "|");
	retval = server_write (u->parent, output);
	proto_nmdc_user_redirect (u, bf_buffer ("Your nickname is already in use."));
	nmdc_stats.usednick++;
	retval = -1;
	break;
      } else {
	proto_nmdc_user_say_string (HubSec, output, "Another instance of you is connecting, bye!");
	server_write (existing_user->parent, output);
	server_disconnect_user (existing_user->parent);
	*output->s = '\0';
	output->e = output->s;
      }
    }

    /* check for cloning */
    if ((!cloning) && (existing_user = hash_find_ip (&hashlist, u->ipaddress))) {
      proto_nmdc_user_redirect (u,
				bf_buffer
				("Cloning is not allowed. Another user is already logged in from this IP address."));
      retval = -1;
      break;
    }

    /* prepare buffer */
    bf_strcat (output, "$Hello ");
    bf_strcat (output, u->nick);
    bf_strcat (output, "|");

    /* soft ban ? */
    ban = banlist_find_byip (&softbanlist, u->ipaddress);
    if (ban) {
      DPRINTF ("** Refused user %s. IP Banned because %.*s\n", u->nick,
	       (unsigned int) bf_used (ban->message), ban->message->s);
      bf_strcat (output, "<");
      bf_strcat (output, HubSec->nick);
      bf_strcat (output, "> You have been banned by ");
      bf_strcat (output, ban->op);
      bf_strcat (output, " because: ");
      bf_strncat (output, ban->message->s, bf_used (ban->message));
      bf_strcat (output, "|");
      if (ban->expire) {
	gettimeofday (&now, NULL);
	bf_printf (output, "<%s> Time remaining ", HubSec->nick);
	time_print (output, ban->expire - now.tv_sec);
	bf_strcat (output, "|");
      }
      if (defaultbanmessage && strlen (defaultbanmessage)) {
	bf_printf (output, "<%s> %s|", HubSec->nick, defaultbanmessage);
      }
      retval = server_write (u->parent, output);
      proto_nmdc_user_forcemove (u, config.KickBanRedirect, bf_buffer ("Banned."));
      nmdc_stats.forcemove--;
      nmdc_stats.banned++;
      retval = -1;
      nmdc_stats.softban++;
      break;
    }

    /* nickban ? */
    ban = banlist_find_bynick (&softbanlist, u->nick);
    if (ban) {
      DPRINTF ("** Refused user %s. Nick Banned because %.*s\n", u->nick,
	       (unsigned int) bf_used (ban->message), ban->message->s);
      bf_strcat (output, "<");
      bf_strcat (output, HubSec->nick);
      bf_strcat (output, "> You have been banned by ");
      bf_strcat (output, ban->op);
      bf_strcat (output, " because: ");
      bf_strncat (output, ban->message->s, bf_used (ban->message));
      bf_strcat (output, "|");
      if (ban->expire) {
	gettimeofday (&now, NULL);
	bf_printf (output, "<%s> Time remaining ", HubSec->nick);
	time_print (output, ban->expire - now.tv_sec);
	bf_strcat (output, "|");
      }
      if (defaultbanmessage && strlen (defaultbanmessage)) {
	bf_printf (output, "<%s> %s|", HubSec->nick, defaultbanmessage);
      }
      retval = server_write (u->parent, output);
      proto_nmdc_user_forcemove (u, config.KickBanRedirect, bf_buffer ("Banned."));
      nmdc_stats.forcemove--;
      nmdc_stats.banned++;
      retval = -1;
      nmdc_stats.nickban++;
      break;
    }


    if (!u->rights)
      u->rights = config.DefaultRights;

    /* success! */
    BF_VERIFY (output);
    retval = server_write (u->parent, output);
    u->state = PROTO_STATE_HELLO;
    server_settimeout (u->parent, PROTO_TIMEOUT_HELLO);

    /* DPRINTF (" - User %s greeted.\n", u->nick); */
  }
  while (0);

  bf_free (output);

  return retval;
}

/********************
 *  State WAITPASS
 */


int proto_nmdc_state_waitpass (user_t * u, token_t * tkn)
{
  int retval = 0;
  account_t *a;
  account_type_t *t;
  buffer_t *output;
  user_t *existing_user;
  banlist_entry_t *ban;
  struct timeval now;

  if (tkn->type != TOKEN_MYPASS)
    return 0;


  output = bf_alloc (2048);
  output->s[0] = '\0';
  do {
    /* check password */
    a = account_find (u->nick);
    if (!account_pwd_check (a, tkn->argument)) {
      if ((ban = banlist_find_bynick (&softbanlist, u->nick)))
	goto banned;

      proto_nmdc_user_say (HubSec, output, bf_buffer ("Bad password."));
      bf_strcat (output, "$BadPass|");
      retval = server_write (u->parent, output);
      server_disconnect_user (u->parent);
      retval = -1;
      nmdc_stats.badpasswd++;
      /* check password guessing attempts */
      if (++a->badpw >= config.PasswdRetry) {
	gettimeofday (&now, NULL);
	banlist_add (&softbanlist, HubSec->nick, u->nick, u->ipaddress, 0xFFFFFFFF,
		     bf_buffer ("Password retry overflow."), now.tv_sec + config.PasswdBantime);
	a->badpw = 0;
      }

      break;
    }
    /* reset password failure counter */
    a->badpw = 0;

    t = a->classp;

    /* check if users exists, if so, redirect old */
    if ((existing_user = hash_find_nick (&hashlist, u->nick, strlen (u->nick)))) {
      proto_nmdc_user_say_string (HubSec, output, "Another instance of you is connecting, bye!");
      server_write (existing_user->parent, output);
      server_disconnect_user (existing_user->parent);
      *output->s = '\0';
      output->e = output->s;
    }

    /* assign rights */
    if (a->passwd[0]) {
      u->rights = a->rights | t->rights;
      u->op = ((u->rights & CAP_KEY) != 0);
    };

    /* nickban ? not for owner offcourse */
    ban = banlist_find_bynick (&softbanlist, u->nick);
    if (ban && (!(u->rights & CAP_OWNER))) {
      goto banned;
      break;
    }

    time (&a->lastlogin);
    /* welcome user */
    if (u->op)
      bf_printf (output, "$LogedIn %s|", u->nick);
    bf_strcat (output, "$Hello ");
    bf_strcat (output, u->nick);
    bf_strcat (output, "|");

    if (!a->passwd[0])
      proto_nmdc_user_say (HubSec, output,
			   bf_buffer
			   ("Your account priviliges will not be awarded until you set a password. Use !passwd or !pwgen."));

    u->state = PROTO_STATE_HELLO;
    server_settimeout (u->parent, PROTO_TIMEOUT_HELLO);

    retval = server_write (u->parent, output);

    /* DPRINTF (" - User %s greeted.\n", u->nick); */
  } while (0);

  bf_free (output);

  return retval;

banned:
  DPRINTF ("** Refused user %s. Nick Banned because %.*s\n", u->nick,
	   (unsigned int) bf_used (ban->message), ban->message->s);
  bf_strcat (output, "<");
  bf_strcat (output, HubSec->nick);
  bf_strcat (output, "> You have been banned by ");
  bf_strcat (output, ban->op);
  bf_strcat (output, " because: ");
  bf_strncat (output, ban->message->s, bf_used (ban->message));
  bf_strcat (output, "|");
  if (ban->expire) {
    gettimeofday (&now, NULL);
    bf_printf (output, "<%s> Time remaining ", HubSec->nick);
    time_print (output, ban->expire - now.tv_sec);
    bf_strcat (output, "|");
  }
  if (defaultbanmessage && strlen (defaultbanmessage)) {
    bf_printf (output, "<%s> %s|", HubSec->nick, defaultbanmessage);
  }
  retval = server_write (u->parent, output);
  proto_nmdc_user_forcemove (u, config.KickBanRedirect, bf_buffer ("Banned."));
  nmdc_stats.forcemove--;
  nmdc_stats.banned++;
  nmdc_stats.nickban++;

  bf_free (output);

  return -1;
}

/********************
 *  State HELLO
 */


int proto_nmdc_state_hello (user_t * u, token_t * tkn, buffer_t * b)
{
  int retval = 0;
  buffer_t *output;
  user_t *existing_user;

  if (tkn->type == TOKEN_GETNICKLIST) {
    u->flags |= NMDC_FLAG_DELAYEDNICKLIST;
    server_settimeout (u->parent, PROTO_TIMEOUT_HELLO);
    return 0;
  }

  if (tkn->type != TOKEN_MYINFO) {
    return 0;
  }

  output = bf_alloc (2048);
  output->s[0] = '\0';

  do {
    /* $MyINFO $ALL Jove yes... i cannot type. I can Dream though...<DCGUI V:0.3.3,M:A,H:1,S:5>$ $DSL.$email$0$ */

    /* check again if user exists */
    if ((existing_user = hash_find_nick (&hashlist, u->nick, strlen (u->nick)))) {
      if (existing_user->ipaddress != u->ipaddress) {
	proto_nmdc_user_redirect (u, bf_buffer ("Your nickname is already in use."));
	nmdc_stats.usednick++;
	retval = -1;
	break;
      } else {
	proto_nmdc_user_say_string (HubSec, output, "Another instance of you is connecting, bye!");
	server_write (existing_user->parent, output);
	server_disconnect_user (existing_user->parent);
      }
    }

    /* should not happen */
    if (u->MyINFO)
      bf_free (u->MyINFO);

    /* create backup */
    u->MyINFO = rebuild_myinfo (u, b);
    if (!u->MyINFO || (u->active < 0)) {
      /* we cannot pass a user without a valid u->MyINFO field. */
      if (u->MyINFO && (u->active < 0)) {
	if (u->rights & CAP_TAG) {
	  DPRINTF ("  Warning: CAP_TAG overrides bad myinfo");
	  proto_nmdc_user_say (HubSec, output,
			       bf_buffer ("WARNING: You should use a client that uses tags!"));
	  u->active = 0;
	} else {
	  proto_nmdc_user_redirect (u,
				    bf_buffer
				    ("This hub requires tags, please upgrade to a client that supports them."));
	  retval = -1;
	  nmdc_stats.notags++;
	  break;
	}
      } else {
	DPRINTF ("  User %s refused due to bad MyINFO.\n", u->nick);
	proto_nmdc_user_redirect (u,
				  bf_buffer ("Your login was refused, your MyINFO seems corrupt."));
	retval = -1;
	nmdc_stats.badmyinfo++;
	break;
      }
    }
    ASSERT (u->MyINFO);

    /* searches start with a full load too to prevent users from getting the warning at login. */
    u->rate_search.tokens = u->active ? rates.asearch.refill : rates.psearch.refill;


    /* allocate plugin private stuff */
    plugin_new_user ((plugin_private_t **) & u->plugin_priv, u, &nmdc_proto);

    /* send the login event before we announce the new user to the hub so plugins can redirect those users */
    if (plugin_send_event (u->plugin_priv, PLUGIN_EVENT_PRELOGIN, u->MyINFO) !=
	PLUGIN_RETVAL_CONTINUE) {
      proto_nmdc_user_redirect (u, bf_buffer ("Your login was refused."));
      retval = -1;
      nmdc_stats.preloginevent++;
      break;
    }

    DPRINTF (" - User %s has %s shared and is %s\n", u->nick, format_size (u->share),
	     u->active ? "active" : "passive");

    u->state = PROTO_STATE_ONLINE;
    server_settimeout (u->parent, PROTO_TIMEOUT_ONLINE);
    time (&u->joinstamp);

    /* add user to nicklist cache */
    nicklistcache_adduser (u);

    /* from now on, user is reachable */
    hash_adduser (&hashlist, u);

    /* send it to the users */
    cache_queue (cache.myinfo, u, u->MyINFO);

    /* ops get the full tag immediately -- it means the ops get all MYINFOs double though */
    cache_queue (cache.myinfoupdateop, u, b);

    /* if this new user is an OP send an updated OpList */
    if (u->op)
      nicklistcache_sendoplist (u);

    /* send the nicklist if it was requested before the MyINFO arrived 
     * if not, credit user with 1 getinfo token.
     */
    if (u->flags & NMDC_FLAG_DELAYEDNICKLIST) {
      u->flags &= ~NMDC_FLAG_DELAYEDNICKLIST;
      nicklistcache_sendnicklist (u);
    } else {
      u->rate_getnicklist.tokens = 1;
    }

    if (u->state != PROTO_STATE_ONLINE)
      break;

    /* send the login event */
    if (plugin_send_event (u->plugin_priv, PLUGIN_EVENT_LOGIN, u->MyINFO) != PLUGIN_RETVAL_CONTINUE) {
      proto_nmdc_user_redirect (u, bf_buffer ("Your login was refused."));
      retval = -1;
      nmdc_stats.loginevent++;
      break;
    }
  }
  while (0);

  bf_free (output);

  return retval;
}

/********************
 *  State ONLINE
 */

int proto_nmdc_state_online (user_t * u, token_t * tkn, buffer_t * b)
{
  int retval = 0;

  /* user protocol handling */
  buffer_t *output;

  //unsigned char *k, *l;
  struct timeval now;

  gettimeofday (&now, NULL);

  output = bf_alloc (2048);
  output->s[0] = '\0';
  switch (tkn->type) {
    case TOKEN_CHAT:
      {
	int i;
	string_list_entry_t *le;

	if (!(u->rights & CAP_CHAT))
	  break;

	/* check quota */
	if ((!(u->rights & CAP_SPAM)) && (!get_token (&rates.chat, &u->rate_chat, now.tv_sec))) {
	  proto_nmdc_user_warn (u, &now, "Think before you talk and don't spam.");
	  nmdc_stats.chatoverflow++;
	  break;
	}

	/* drop all chat message that are too long */
	if ((bf_size (b) > chatmaxlength) && (!(u->rights & CAP_SPAM))) {
	  nmdc_stats.chattoolong++;
	  break;
	}

	/* verify the nick */
	if (strncasecmp (u->nick, b->s + 1, strlen (u->nick))) {
	  nmdc_stats.chatfakenick++;
	  break;
	}

	/* verify the closing > */
	if (b->s[strlen (u->nick) + 1] != '>') {
	  nmdc_stats.chatfakenick++;
	  break;
	}

	/* drop any trailing spaces */
	while (b->e[-1] == ' ')
	  b->e--;
	*b->e = '\0';

	/* drop empty strings */
	if ((b->s + strlen (u->nick) + 2) == b->e)
	  break;

	/* call plugin first. it can force us to drop the message */
	if (plugin_send_event (u->plugin_priv, PLUGIN_EVENT_CHAT, b) != PLUGIN_RETVAL_CONTINUE) {
	  nmdc_stats.chatevent++;
	  break;
	}

	/* allocate buffer of max size with some extras. */
	buffer_t *buf = bf_alloc (cache.chat.length + cache.chat.messages.count + b->size);

	/* send back to user : keep in order */
	le = cache.chat.messages.first;

	/* skip all previous send messages */
	for (i = u->ChatCnt; i && le; le = le->next)
	  if (le->user == u)
	    i--;

	/* rest of add cached data, skip markers */
	for (; le; le = le->next) {
	  if (!bf_used (le->data))
	    continue;
	  bf_strncat (buf, le->data->s, bf_used (le->data));
	  bf_strcat (buf, "|");
	}
	/* add string to send */
	bf_strncat (buf, b->s, bf_used (b));
	bf_strcat (buf, "|");
	retval = server_write (u->parent, buf);

	bf_free (buf);

	/* mark user as "special" */
	u->ChatCnt++;
	u->CacheException++;

	if (!(u->flags & PROTO_FLAG_ZOMBIE)) {
	  cache_queue (cache.chat, u, b);
	} else {
	  /* add empty chat buffer as marker */
	  buffer_t *mark = bf_alloc (1);

	  cache_queue (cache.chat, u, mark);
	  bf_free (mark);
	}

	break;
      }
    case TOKEN_MYINFO:
      {
	buffer_t *new;

	/* check quota */
	if (!get_token (&rates.myinfo, &u->rate_myinfo, now.tv_sec)) {
	  nmdc_stats.myinfooverflow++;
	  break;
	}

	/* build and generate the tag */
	new = rebuild_myinfo (u, b);
	if (!new || (u->active < 0)) {
	  if (new && (u->active < 0)) {
	    if (u->rights & CAP_TAG) {
	      DPRINTF ("  Warning: CAP_TAG overrides bad myinfo");
	      proto_nmdc_user_say (HubSec, output,
				   bf_buffer ("WARNING: You should use a client that uses tags!"));
	      retval = server_write (u->parent, output);
	      u->active = 0;
	      goto accept_anyway;
	    } else {
	      proto_nmdc_user_redirect (u,
					bf_buffer
					("This hub requires tags, please upgrade to a client that supports them."));
	      retval = -1;
	      nmdc_stats.notags++;
	    }
	    if (new)
	      bf_free (new);
	    break;
	  }

	  proto_nmdc_user_redirect (u, bf_buffer ("Broken MyINFO, get lost."));
	  retval = -1;
	  nmdc_stats.badmyinfo++;
	  break;
	}
      accept_anyway:

	/* update plugin info */
	plugin_update_user (u);

	/* send new info event */
	if (plugin_send_event (u->plugin_priv, PLUGIN_EVENT_INFOUPDATE, b) !=
	    PLUGIN_RETVAL_CONTINUE) {
	  nmdc_stats.myinfoevent++;
	  proto_nmdc_user_redirect (u,
				    bf_buffer
				    ("Sorry, you no longer satisfy the necessary requirements for this hub."));
	  retval = -1;
	  break;
	}

	/* update the tag */
	nicklistcache_updateuser (u, new);

	if (u->MyINFO)
	  bf_free (u->MyINFO);

	/* build and generate the tag for al users */
	u->MyINFO = new;

	/* ops get the full tag immediately */
	cache_purge (cache.myinfoupdateop, u);
	cache_queue (cache.myinfoupdateop, u, b);

	/* check quota */
	if (!get_token (&rates.myinfo, &u->rate_myinfo, now.tv_sec)) {
	  string_list_entry_t *entry;

	  /* if no entry in the stringlist yet, exit */
	  if (!(entry = string_list_find (&cache.myinfoupdate.messages, u)))
	    break;
	  /* if there is an entry, replace it with the more recent one. */
	  cache.myinfoupdate.length -= bf_used (entry->data);
	  string_list_del (&cache.myinfoupdate.messages, entry);

	  cache_queue (cache.myinfoupdate, u, u->MyINFO);

	  break;
	}

	/* queue the tag for al users */
	cache_purge (cache.myinfoupdate, u);
	cache_queue (cache.myinfoupdate, u, u->MyINFO);

	break;
      }
    case TOKEN_SEARCH:
      {
	searchlist_entry_t *e = NULL;
	buffer_t *ss = NULL;
	char *c = NULL;
	unsigned long deadline = now.tv_sec - researchperiod;
	unsigned int drop = 0;

	if (!(u->rights & CAP_SEARCH))
	  break;

	/* check quota */
	if (!get_token (u->active ? &rates.asearch : &rates.psearch, &u->rate_search, now.tv_sec)
	    && (!(u->rights & CAP_NOSRCHLIMIT))) {
	  if (u->active) {
	    proto_nmdc_user_warn (u, &now, "Active earches are limited to %u every %u seconds.",
				  rates.asearch.refill, rates.asearch.period);
	  } else {
	    proto_nmdc_user_warn (u, &now, "Passive searches are limited to %u every %u seconds.",
				  rates.psearch.refill, rates.psearch.period);
	  }
	  nmdc_stats.searchoverflow++;
	  break;
	}

	/* drop all seach message that are too long */
	if ((bf_size (b) > searchmaxlength) && (!(u->rights & CAP_SPAM))) {
	  nmdc_stats.searchtoolong++;
	  break;
	}

	/* Do research handling */
	for (c = tkn->argument; *c != ' '; c++);
	c++;

	/* CAP_NOSRCHLIMIT avoids research option */
	if (!(u->rights & CAP_NOSRCHLIMIT)) {
	  for (e = u->searchlist.last; e; e = e->prev) {
	    /* see if search is cached */
	    if (!strcmp (e->data->s, c)) {
	      /* was the search outside the minimum interval ? */
	      if ((now.tv_sec - e->timestamp) < researchmininterval) {
		proto_nmdc_user_warn (u, &now, "Do not repeat searches within %d seconds.",
				      researchmininterval);
		/* no. restore search token and drop search */
		u->rate_search.tokens++;
		drop = 1;
		//DPRINTF ("Dropped research\n");
		break;
	      }
	      /* was it done within the re-search period ? */
	      if (deadline < e->timestamp) {
		//proto_nmdc_user_warn (u, &now, "Searches repeated within %u seconds are only send to users joined within that period.", researchperiod);
		/* if so, only search recent users */
		/* queue in re-searchqueue */
		cache_queue (cache.aresearch, u, b);
		if (u->active) {
		  cache_queue (cache.presearch, u, b);
		}
		drop = 1;
		//DPRINTF ("Doing RESEARCH\n");
	      }
	      searchlist_update (&u->searchlist, e);
	      break;
	    }
	  }
	  if (drop)
	    break;
	}
	/* if search wasn't cached, cache it */
	if (!e) {
	  //DPRINTF ("New Search\n");
	  ss = bf_alloc (strlen (c) + 1);
	  bf_printf (ss, "%s", c);

	  /* if necessary, prune searchlist */
	  while (u->searchlist.count >= researchmaxcount)
	    searchlist_del (&u->searchlist, u->searchlist.first);

	  searchlist_add (&u->searchlist, u, ss);

	  bf_free (ss);
	}

	/* TODO verify nick, mode and IP */
	if (plugin_send_event (u->plugin_priv, PLUGIN_EVENT_SEARCH, b) != PLUGIN_RETVAL_CONTINUE) {
	  nmdc_stats.searchevent++;
	  break;
	}

	/* if there is still a search cached from this user, delete it. */
	cache_purge (cache.asearch, u);
	if (u->active)
	  cache_purge (cache.psearch, u);

	/* mark user as "special" */
	u->SearchCnt++;
	u->CacheException++;

	cache_queue (cache.asearch, u, b);
	if (u->active) {
	  cache_queue (cache.psearch, u, b);
	}
      }
      break;
    case TOKEN_SR:
      {
	int l;
	unsigned char *c, *n;
	user_t *t;

	/* check quota */
	if (!get_token (&rates.psresults_out, &u->rate_psresults_out, now.tv_sec)) {
	  proto_nmdc_user_warn (u, &now, "Do not repeat searches within %d seconds.",
				researchmininterval);
	  nmdc_stats.sroverflow++;
	  break;
	}

	/* drop all seach message that are too long */
	if ((bf_size (b) > srmaxlength) && (!(u->rights & CAP_SPAM))) {
	  nmdc_stats.srtoolong++;
	  break;
	}

	if (plugin_send_event (u->plugin_priv, PLUGIN_EVENT_SR, b) != PLUGIN_RETVAL_CONTINUE) {
	  nmdc_stats.srevent++;
	  break;
	}

	c = tkn->argument;
	n = tkn->argument;

	/* find end of nick */
	//for (; *c && (*c != ' ') && (c < b->e); ++c);
	SKIPTOCHAR (c, b->e, ' ');
	l = c - n;

	if ((!*c) || strncmp (n, u->nick, l)) {
	  nmdc_stats.srfakesource++;
	  break;
	}

	c = b->e;
	c--;			/* point to last valid character */
	while ((*c != 5) && (c > b->s))
	  --c;
	/* no \5 found */
	if (*c != 5) {
	  ++nmdc_stats.srnodest;
	  break;
	}
	*c++ = '\0';
	l = b->e - c;
	b->e = c - 1;

	if (l > NICKLENGTH) {
	  ++nmdc_stats.srnodest;
	  break;
	}

	/* find target */
	t = hash_find_nick (&hashlist, c, l);
	if (!t) {
	  ++nmdc_stats.srnodest;
	  break;
	}

	if (!(t->rights & CAP_SEARCH))
	  break;

	/* check quota */
	if (!get_token (&rates.psresults_in, &t->rate_psresults_in, now.tv_sec)) {
	  nmdc_stats.sroverflow++;
	  break;
	}

	/* search result for a robot?? */
	if (t->state == PROTO_STATE_VIRTUAL) {
	  ++nmdc_stats.srrobot;
	  break;
	}

	if (t->state != PROTO_STATE_ONLINE) {
	  ++nmdc_stats.srnodest;
	  break;
	}

	/* queue search result with the correct user */
	//cache_count (cache.results, t, b);
	cache_queue (((nmdc_user_t *) t->pdata)->results, u, b);
	cache_count (results, t);
	t->ResultCnt++;
	t->CacheException++;

	break;
      }
    case TOKEN_GETNICKLIST:
      /* check quota */
      if (!get_token (&rates.getnicklist, &u->rate_getnicklist, now.tv_sec)) {
	proto_nmdc_user_warn (u, &now, "Userlist request denied. Maximum 1 reload per %ds.",
			      rates.getnicklist.period);
	break;
      }

      nicklistcache_sendnicklist (u);
      break;
    case TOKEN_GETINFO:
      {
	int l;
	unsigned char *c, *n;
	user_t *t;

	/* check quota */
	if ((u->supports & NMDC_SUPPORTS_NoGetINFO)
	    && (!get_token (&rates.getinfo, &u->rate_getinfo, now.tv_sec)))
	  break;

	c = tkn->argument;
	n = tkn->argument;

	/* find end of nick */
	//for (; *c && (*c != ' '); c++);
	SKIPTOCHAR (c, b->e, ' ');
	l = c - n;

	/* find target */
	t = hash_find_nick (&hashlist, n, l);
	if (!t)
	  break;

	cache_queue (((nmdc_user_t *) u->pdata)->privatemessages, u, t->MyINFO);
	//cache_count (cache.privatemessages, u, t->MyINFO);
	cache_count (privatemessages, u);
	u->MessageCnt++;
	u->CacheException++;

	break;
      }
    case TOKEN_CONNECTTOME:
      {
	int l;
	unsigned char *c, *n;
	user_t *t;
	struct in_addr addr;

	if (!(u->rights & CAP_DL))
	  break;

	/* check quota */
	if (!get_token (&rates.downloads, &u->rate_downloads, now.tv_sec)) {
	  nmdc_stats.ctmoverflow++;
	  break;
	}

	c = tkn->argument;
	n = tkn->argument;

	/* find end of nick */
	//for (; *c && (*c != ' '); c++);
	SKIPTOCHAR (c, b->e, ' ');
	l = c - n;

	/* find target */
	t = hash_find_nick (&hashlist, n, l);
	if (!t) {
	  *c = '\0';
	  DPRINTF ("CTM: cannot find target %s\n", n);
	  nmdc_stats.ctmbadtarget++;
	  break;
	}

	if ((t->rights & CAP_SHAREBLOCK) && t->active) {
	  proto_nmdc_user_say (HubSec, output, bf_buffer ("You cannot download from this user."));
	  retval = server_write (u->parent, output);
	  break;
	}

	n = ++c;
	//for (; *c && (*c != ':'); c++);
	SKIPTOCHAR (c, b->e, ':');
	if (*c != ':')
	  break;
	l = c - n;

	/* convert address */
	*c = 0;
	if (!inet_aton (n, &addr)) {
	  break;
	}
	*c = ':';

	/* must be identical */
	if (u->ipaddress != addr.s_addr) {
	  break;
	}

	if (t->state == PROTO_STATE_ONLINE) {
	  /* queue search result with the correct user
	   * \0 termination should not be necessary
	   */
	  cache_queue (((nmdc_user_t *) t->pdata)->privatemessages, u, b);
	  //cache_count (cache.privatemessages, t, b);
	  cache_count (privatemessages, t);
	  t->MessageCnt++;
	  t->CacheException++;
	};

	break;
      }
    case TOKEN_REVCONNECTOTME:
      {
	int l;
	unsigned char *c, *n;
	user_t *t;

	if (!(u->rights & CAP_DL))
	  break;

	/* check quota */
	if (!get_token (&rates.downloads, &u->rate_downloads, now.tv_sec)) {
	  nmdc_stats.rctmoverflow++;
	  break;
	}

	/* check target */
	n = c = tkn->argument;
	//for (; *c && (*c != ' '); c++);
	SKIPTOCHAR (c, b->e, ' ');
	l = c - n;

	if (strncasecmp (u->nick, tkn->argument, l)) {
	  DPRINTF ("RCTM: FAKED source, user %s\n", u->nick);
	  nmdc_stats.rctmbadsource++;
	  break;
	}

	/* find end of nick, start at the end. */
	c = b->e - 1;
	n = b->e;
	if (!*c) {
	  c--;
	  n--;
	}
	for (; *c && (*c != ' '); c--);
	l = n - ++c;

	/* find target */
	t = hash_find_nick (&hashlist, c, l);
	if (!t) {
	  DPRINTF ("RCTM: cannot find target %s (%d)\n", c, l);
	  nmdc_stats.rctmbadtarget++;
	  break;
	}

	if (t->rights & CAP_SHAREBLOCK) {
	  proto_nmdc_user_say (HubSec, output, bf_buffer ("You cannot download from this user."));
	  retval = server_write (u->parent, output);
	  break;
	}

	if (t->state == PROTO_STATE_ONLINE) {
	  /* don't penitalize users for serving passive users: */
	  if (t->rate_downloads.tokens <= rates.downloads.burst)
	    t->rate_downloads.tokens++;

	  /* queue search result with the correct user
	   * \0 termination should not be necessary
	   */
	  cache_queue (((nmdc_user_t *) t->pdata)->privatemessages, u, b);
	  //cache_count (cache.privatemessages, t, b);
	  cache_count (privatemessages, t);
	  t->MessageCnt++;
	  t->CacheException++;
	}

	break;
      }
    case TOKEN_TO:
      {
	int l;
	unsigned char *c, *n;
	user_t *t;

	if (!(u->rights & (CAP_PM | CAP_PMOP))) {
	  proto_nmdc_user_warn (u, &now, "You are not allowed to send private messages.");
	  break;
	}

	if (u->flags & PROTO_FLAG_ZOMBIE)
	  break;

	/* check quota */
	if ((!(u->rights & CAP_SPAM)) && (!get_token (&rates.chat, &u->rate_chat, now.tv_sec))) {
	  proto_nmdc_user_warn (u, &now, "Don't send private messages so fast.");
	  nmdc_stats.pmoverflow++;
	  break;
	}


	c = tkn->argument;
	n = tkn->argument;

	/* find end of nick */
	//for (; *c && (*c != ' '); c++);
	SKIPTOCHAR (c, b->e, ' ');
	l = c - n;

	/* find target */
	t = hash_find_nick (&hashlist, n, l);
	if (!t) {
	  nmdc_stats.pmbadtarget++;
	  break;
	}

	/* find end of From: */
	for (c++; *c && (*c != ' '); c++);

	/* find end of from Nick */
	n = ++c;
	for (c++; *c && (*c != ' '); c++);
	l = c - n;

	if (strncmp (u->nick, n, l)) {
	  bf_printf (output, "Bad From: nick. No faking.");
	  retval = server_write (u->parent, output);
	  server_disconnect_user (u->parent);
	  nmdc_stats.pmbadsource++;
	  retval = -1;
	  break;
	};

	/* find $ */
	for (c++; *c && (*c != '$'); c++);
	c++;
	if (*c != '<')
	  break;
	c++;

	/* find end of from Nick */
	n = c;
	for (c++; *c && (*c != '>'); c++);
	l = c - n;

	if (strncmp (u->nick, n, l)) {
	  bf_printf (output, "Bad display nick. No faking.");
	  retval = server_write (u->parent, output);
	  server_disconnect_user (u->parent);
	  nmdc_stats.pmbadsource++;
	  retval = -1;
	  break;
	};

	if (plugin_send_event (u->plugin_priv, PLUGIN_EVENT_PM_OUT, b) != PLUGIN_RETVAL_CONTINUE) {
	  nmdc_stats.pmoutevent++;
	  break;
	}

	/* do not send if only PMOP and target is not an OP */
	if ((!(u->rights & CAP_PM)) && (!t->op)) {
	  proto_nmdc_user_warn (u, &now, "Sorry, you can only send private messages to operators.");
	  break;
	}
	if (plugin_send_event (t->plugin_priv, PLUGIN_EVENT_PM_IN, b) != PLUGIN_RETVAL_CONTINUE) {
	  nmdc_stats.pminevent++;
	  break;
	}

	/* only pm to online users. */
	if (t->state == PROTO_STATE_ONLINE) {
	  /* queue search result with the correct user 
	   * \0 termination should not be necessary
	   */
	  cache_queue (((nmdc_user_t *) t->pdata)->privatemessages, u, b);
	  //cache_count (cache.privatemessages, t, b);
	  cache_count (privatemessages, t);
	  t->MessageCnt++;
	  t->CacheException++;
	}

	break;
      }
    case TOKEN_QUIT:
      break;
    case TOKEN_OPFORCEMOVE:
      {
	unsigned char *c, *who, *where, *why;
	user_t *target;

	if (!(u->rights & CAP_KEY))
	  break;

	/* parse argments. */
	c = tkn->argument;

	/* find first "$Who:" token */
	//for (; *c && (*c != '$'); ++c);
	SKIPTOCHAR (c, b->e, '$');
	if (!*c++)
	  break;

	if (strncmp (c, "Who", 3))
	  break;
	if (!*c++)
	  break;

	//for (; *c && (*c != ':'); c++);
	SKIPTOCHAR (c, b->e, ':');
	if (!*c)
	  break;
	who = ++c;

	/* terminate string */
	//for (; *c && (*c != '$'); c++);
	SKIPTOCHAR (c, b->e, '$');
	if (!*c)
	  break;
	*c = '\0';

	/* find user */
	target = hash_find_nick (&hashlist, who, c - who);
	if (!target)
	  break;

	/* check if this users doesn't have more rights */
	if (target->op && (target->rights & ~u->rights))
	  break;

	/* find "Where:" token */
	if (strncmp (++c, "Where", 5))
	  break;
	//for (; *c && (*c != ':'); c++);
	SKIPTOCHAR (c, b->e, ':');
	if (!*c)
	  break;
	where = ++c;

	/* terminate */
	//for (; *c && (*c != '$'); c++);
	SKIPTOCHAR (c, b->e, '$');
	if (!*c)
	  break;
	*c++ = '\0';

	/* find "Msg:" token */
	if (strncmp (c, "Msg", 3))
	  break;
	//for (; *c && (*c != ':'); c++);
	SKIPTOCHAR (c, b->e, ':');
	if (!*c)
	  break;
	why = ++c;

	/* move user.
	 * this check will allow us to forcemove users that are not yet online.
	 * while not forcemoving robots.
	 */
	if (target->state != PROTO_STATE_VIRTUAL)
	  retval = proto_nmdc_user_forcemove (target, where, bf_buffer (why));

	if (u == target)
	  u = NULL;

	break;
      }
    case TOKEN_KICK:
      {
	unsigned char *n, *c;
	user_t *target;

	if (!(u->rights & CAP_KICK))
	  break;

	c = tkn->argument;
	while (*c && *c == ' ')
	  c++;
	n = c;
	while (*c && *c != ' ')
	  c++;
	if (*c)
	  *c = '\0';

	target = hash_find_nick (&hashlist, n, c - n);
	if (!target)
	  break;
	/* don't kick robots. */
	if (target->state == PROTO_STATE_VIRTUAL)
	  break;

	if (~u->rights & target->rights)
	  break;

	bf_printf (output, "You were kicked.");
	banlist_add (&softbanlist, u->nick, target->nick, target->ipaddress, 0xFFFFFFFF, output,
		     now.tv_sec + config.defaultKickPeriod);

	retval = proto_nmdc_user_forcemove (target, config.KickBanRedirect, output);
	break;
      }
    case TOKEN_BOTINFO:
      {
	config_element_t *share, *slot, *hub, *users, *owner;

	share = config_find ("sharemin.unregistered");
	slot = config_find ("slot.unregistered.min");
	hub = config_find ("hub.unregistered.max");
	users = config_find ("userlimit.unregistered");
	owner = config_find ("hubowner");

	bf_printf (output, "$HubINFO %s$%s:%d$%s$%lu$%llu$%lu$%lu$%s$%s|",
		   config.HubName,
		   config.Hostname,
		   config.ListenPort,
		   config.HubDesc,
		   users ? *users->val.v_ulong : 100,
		   share ? *share->val.v_ulonglong : 0,
		   slot ? *slot->val.v_ulong : 0,
		   hub ? *hub->val.v_ulong : 100,
		   HUBSOFT_NAME, owner ? *owner->val.v_string : (unsigned char *) "Unknown");

	retval = server_write (u->parent, output);

	nmdc_stats.botinfo++;
	break;
      }
  }

  if (u && (u->state != PROTO_STATE_DISCONNECTED))
    server_settimeout (u->parent, PROTO_TIMEOUT_ONLINE);

  bf_free (output);

  return retval;
}


/******************************************************************************\
**                                                                            **
**                             PROTOCOL HANDLING                              **
**                                                                            **
\******************************************************************************/
int proto_nmdc_handle_token (user_t * u, buffer_t * b)
{
  token_t tkn;

  /* this functions is called without token to initialize the connection */
  if (!b) {
    if (u->state != PROTO_STATE_INIT)
      return 0;

    return proto_nmdc_state_init (u, &tkn);
  }

  /* parse token. if it is unknown just reset the timeout and leave */
  if (token_parse (&tkn, b->s) == TOKEN_UNIDENTIFIED) {
    if (u->state == PROTO_STATE_ONLINE)
      server_settimeout (u->parent, PROTO_TIMEOUT_ONLINE);
    return 0;
  }

  /* handle token depending on state */
  switch (u->state) {
    case PROTO_STATE_INIT:	/* initial creation state */
      return proto_nmdc_state_init (u, &tkn);
    case PROTO_STATE_SENDLOCK:	/* waiting for user $Key */
      return proto_nmdc_state_sendlock (u, &tkn);
    case PROTO_STATE_WAITNICK:	/* waiting for user $ValidateNick */
      return proto_nmdc_state_waitnick (u, &tkn);
    case PROTO_STATE_WAITPASS:	/* waiting for user $Passwd */
      return proto_nmdc_state_waitpass (u, &tkn);
    case PROTO_STATE_HELLO:	/* waiting for user $MyInfo */
      return proto_nmdc_state_hello (u, &tkn, b);
    case PROTO_STATE_ONLINE:
      return proto_nmdc_state_online (u, &tkn, b);
    case PROTO_STATE_DISCONNECTED:
      /* not supposed to happen !! */
      ASSERT (0);
  }

  return 0;
}


/******************************************************************************\
**                                                                            **
**                             MAIN CACHE HANDLING                            **
**                                                                            **
\******************************************************************************/

unsigned int proto_nmdc_build_buffer (buffer_t * buffer, user_t * u, unsigned int as,
				      unsigned int ps, unsigned int ch, unsigned int pm,
				      unsigned int res)
{
  buffer_t *b = NULL;
  unsigned long l;
  unsigned char *t, *e;
  string_list_entry_t *le;

  ASSERT ((u->ChatCnt + u->SearchCnt + u->ResultCnt + u->MessageCnt) == u->CacheException);
  t = buffer->e;
  e = buffer->buffer + buffer->size;
  if (ch) {
    /* skip all chat messages until the last send message of the user */
    le = cache.chat.messages.first;
    if (u->ChatCnt) {
      for (le = cache.chat.messages.first; le && u->ChatCnt; le = le->next)
	if (le->user == u) {
	  u->ChatCnt--;
	  u->CacheException--;
	}
    };
    /* append the other chat messages */
    for (; le; le = le->next) {
      /* data and length */
      b = le->data;
      l = bf_used (b);
      if (!l)
	continue;

      memcpy (t, b->s, l);
      t += l;
      *t++ = '|';
    };
    //ASSERT (!u->ChatCnt);
    u->CacheException -= u->ChatCnt;
    u->ChatCnt = 0;
  };

  if (u->active) {
    if (as) {
      for (le = cache.asearch.messages.first; le; le = le->next) {
	if (le->user == u)
	  continue;

	/* data and length */
	b = le->data;
	l = bf_used (b);

	/* copy data */
	memcpy (t, b->s, l);
	t += l;
	*t++ = '|';
      }
      u->CacheException -= u->SearchCnt;
      u->SearchCnt = 0;
    }
  } else {
    /* complete buffers */
    if (ps) {
      for (le = cache.psearch.messages.first; le; le = le->next) {
	if (le->user == u)
	  continue;

	/* data and length */
	b = le->data;
	l = bf_used (b);

	/* copy data */
	memcpy (t, b->s, l);
	t += l;
	*t++ = '|';
      }
      u->CacheException -= u->SearchCnt;
      u->SearchCnt = 0;
    }
  }
  /* add passive results */
  if (res && u->ResultCnt) {
    ASSERT (u->ResultCnt == ((nmdc_user_t *) u->pdata)->results.messages.count);
    for (le = ((nmdc_user_t *) u->pdata)->results.messages.first; le; le = le->next) {

      /* data and length */
      b = le->data;
      l = bf_used (b);

      /* copy data */
      memcpy (t, b->s, l);
      t += l;
      *t++ = '|';
      u->ResultCnt--;
      u->CacheException--;
    }
    ASSERT (!u->ResultCnt);
    cache_clear ((((nmdc_user_t *) u->pdata)->results));
  }
  /* add messages results */
  if (pm && u->MessageCnt) {
    ASSERT (u->MessageCnt == ((nmdc_user_t *) u->pdata)->privatemessages.messages.count);
    for (le = ((nmdc_user_t *) u->pdata)->privatemessages.messages.first; le; le = le->next) {
      /* data and length */
      b = le->data;
      l = bf_used (b);

      /* copy data */
      memcpy (t, b->s, l);
      t += l;
      *t++ = '|';
      u->MessageCnt--;
      u->CacheException--;
    }
    ASSERT (!u->MessageCnt);
    cache_clear ((((nmdc_user_t *) u->pdata)->privatemessages));
  }
  ASSERT (t <= e);
  ASSERT (t >= buffer->e);
  buffer->e = t;
  return bf_used (buffer);
}

inline int proto_nmdc_add_element (cache_element_t * elem, buffer_t * buf, unsigned long now)
{
  register buffer_t *b;
  register unsigned char *t;
  register string_list_entry_t *le;
  register unsigned int l;

  if (elem->messages.count && get_token (&elem->timertype, &elem->timer, now)) {
    t = buf->e;
    for (le = elem->messages.first; le; le = le->next) {
      /* data and length */
      b = le->data;
      l = bf_used (b);

      /* copy data */
      memcpy (t, b->s, l);
      t += l;
      *t++ = '|';
    }
    buf->e = t;
    BF_VERIFY (buf);
    return 1;
  }

  return 0;
}

void proto_nmdc_flush_cache ()
{
  buffer_t *b;
  user_t *u, *n;
  buffer_t *buf_passive, *buf_active, *buf_exception, *buf_op, *buf_aresearch, *buf_presearch;
  unsigned int psl, asl, l, t;
  unsigned int as = 0, ps = 0, ch = 0, pm = 0, mi = 0, miu = 0, res = 0, miuo = 0, ars = 0, prs = 0;
  struct timeval now;
  unsigned long deadline;

#ifdef ZLINES
  buffer_t *buf_zlinepassive = NULL, *buf_zlineactive = NULL;
  buffer_t *buf_zpipepassive = NULL, *buf_zpipeactive = NULL;
#endif

  /*
   * generate necessary buffers 
   */

  /* calculate lengths: always worst case. */
  l = cache.chat.length + cache.chat.messages.count;
  l += cache.myinfo.length + cache.myinfo.messages.count;
  l += cache.myinfoupdate.length + cache.myinfoupdate.messages.count;
  psl = l + cache.psearch.length + cache.psearch.messages.count;
  asl = l + cache.asearch.length + cache.asearch.messages.count;

  /* allocate buffers */
  buf_passive = bf_alloc (psl);
  buf_active = bf_alloc (asl);
  buf_exception =
    bf_alloc (((asl >
		psl) ? asl : psl) + cache.results.length + cache.results.messages.count +
	      cache.privatemessages.length + cache.privatemessages.messages.count);
  buf_op = bf_alloc (cache.myinfoupdateop.length + cache.myinfoupdateop.messages.count);

  buf_aresearch = bf_alloc (cache.aresearch.length + cache.aresearch.messages.count);
  buf_presearch = bf_alloc (cache.presearch.length + cache.presearch.messages.count);

  gettimeofday (&now, NULL);
  deadline = now.tv_sec - researchperiod;

  /* operator buffer */
  miuo = proto_nmdc_add_element (&cache.myinfoupdateop, buf_op, now.tv_sec);

  /* Exception buffer */
  mi = proto_nmdc_add_element (&cache.myinfo, buf_exception, now.tv_sec);
  miu = proto_nmdc_add_element (&cache.myinfoupdate, buf_exception, now.tv_sec);

  ASSERT (bf_used (buf_exception) <= (l - cache.chat.length + cache.chat.messages.count));

  /* copy identical part to passive buffer */
  t = bf_used (buf_exception);
  if (t) {
    memcpy (buf_passive->s, buf_exception->s, t);
    buf_passive->e += t;
  }

  /* add chat messages */
  ch = proto_nmdc_add_element (&cache.chat, buf_passive, now.tv_sec);

  ASSERT (bf_used (buf_passive) <= l);

  /* copy identical part to active buffer too */
  t = bf_used (buf_passive);
  if (t) {
    memcpy (buf_active->s, buf_passive->s, t);
    buf_active->e += t;
  }

  /* at the end, add the passive and active search messages */
  ps = proto_nmdc_add_element (&cache.psearch, buf_passive, now.tv_sec);
  as = proto_nmdc_add_element (&cache.asearch, buf_active, now.tv_sec);

  ASSERT (bf_used (buf_passive) <= psl);
  ASSERT (bf_used (buf_active) <= asl);

  /* check to see if we need to send pms */
  if (cache.privatemessages.messages.count
      && get_token (&cache.privatemessages.timertype, &cache.privatemessages.timer, now.tv_sec))
    pm = 1;

  /* check to see if we need to send results */
  if (cache.results.messages.count
      && get_token (&cache.results.timertype, &cache.results.timer, now.tv_sec))
    res = 1;

  ars = proto_nmdc_add_element (&cache.aresearch, buf_aresearch, now.tv_sec);
  prs = proto_nmdc_add_element (&cache.presearch, buf_presearch, now.tv_sec);

  BF_VERIFY (buf_active);
  BF_VERIFY (buf_passive);
  BF_VERIFY (buf_aresearch);
  BF_VERIFY (buf_presearch);

#ifdef ZLINES
  if ((cache.ZlineSupporters > 0) || (cache.ZpipeSupporters > 0)) {
    zline (buf_passive, cache.ZpipeSupporters ? &buf_zpipepassive : NULL,
	   cache.ZlineSupporters ? &buf_zlinepassive : NULL);
    zline (buf_active, cache.ZpipeSupporters ? &buf_zpipeactive : NULL,
	   cache.ZlineSupporters ? &buf_zlineactive : NULL);
  }

  BF_VERIFY (buf_zpipeactive);
  BF_VERIFY (buf_zlineactive);
  BF_VERIFY (buf_zpipepassive);
  BF_VERIFY (buf_zlinepassive);
#endif

  if (mi)
    nmdc_stats.cache_myinfo += cache.myinfo.length + cache.myinfo.messages.count;
  if (miu)
    nmdc_stats.cache_myinfoupdate += cache.myinfoupdate.length + cache.myinfoupdate.messages.count;
  if (ch)
    nmdc_stats.cache_chat += cache.chat.length + cache.chat.messages.count;
  if (as)
    nmdc_stats.cache_asearch += cache.asearch.length + cache.asearch.messages.count;
  if (ps)
    nmdc_stats.cache_psearch += cache.psearch.length + cache.psearch.messages.count;
  if (pm)
    nmdc_stats.cache_messages +=
      cache.privatemessages.length + cache.privatemessages.messages.count;
  if (res)
    nmdc_stats.cache_results += cache.results.length + cache.results.messages.count;

  DPRINTF
    ("//////// %10lu //////// Cache Flush \\\\\\\\\\\\\\\\\\\\\\ %7lu \\\\\\\\\\\\\\\\\\\\\\\n"
     " Chat %d (%lu), MyINFO %d (%lu), MyINFOupdate %d (%lu), as %d (%lu), ps %d (%lu), res %d\n",
     now.tv_sec, now.tv_usec,
     ch, cache.chat.length + cache.chat.messages.count, mi,
     cache.myinfo.length + cache.myinfo.messages.count, miu,
     cache.myinfoupdate.length + cache.myinfoupdate.messages.count, as,
     cache.asearch.length + cache.asearch.messages.count, ps,
     cache.psearch.length + cache.psearch.messages.count, res);

  /*
   * write out buffers 
   */

  /* if there are *any* users, search a filled bucket in the nick hashlist 
   * and use that as a starting point for the cache flush.  */
  if (userlist) {
    for (u = userlist; u; u = n) {
      n = u->next;

      if (u->state != PROTO_STATE_ONLINE)
	continue;

      ASSERT ((u->ChatCnt + u->SearchCnt + u->ResultCnt + u->MessageCnt) == u->CacheException);

      /* get buffer -- only create exception buffer if really, really necessary */
      if (u->CacheException
	  && ((u->SearchCnt && (u->active ? as : ps)) || (u->ChatCnt && ch) || (u->ResultCnt && res)
	      || (u->MessageCnt && pm))) {
	/* we need to copy this buffer cuz it could be buffered during write
	   and it is changed at every call to proto_nmdc_build_buffer */
	b = bf_copy (buf_exception, buf_exception->size - bf_used (buf_exception));
	proto_nmdc_build_buffer (b, u, as, ps, ch, pm, res);
	BF_VERIFY (b);

	DPRINTF (" Exception (%p): res (%lu, %lu) [%d], pm (%lu, %lu), buf (%lu / %lu / %lu)\n", u,
		 cache.results.length + cache.results.messages.count,
		 ((nmdc_user_t *) u->pdata)->results.length +
		 ((nmdc_user_t *) u->pdata)->results.messages.count, u->ResultCnt,
		 cache.privatemessages.length + cache.privatemessages.messages.count,
		 ((nmdc_user_t *) u->pdata)->privatemessages.length +
		 ((nmdc_user_t *) u->pdata)->privatemessages.messages.count,
		 bf_used (buf_exception), bf_used (b), buf_exception->size);

	if (bf_used (b))
	  if (server_write (u->parent, b) > 0)
	    server_settimeout (u->parent, PROTO_TIMEOUT_ONLINE);

	bf_free (b);
      } else {
#ifdef ZLINES
	if (u->supports & NMDC_SUPPORTS_ZPipe) {
	  b = (u->active ? buf_zpipeactive : buf_zpipepassive);
	} else if (u->supports & NMDC_SUPPORTS_ZLine) {
	  b = (u->active ? buf_zlineactive : buf_zlinepassive);
	} else {
	  b = (u->active ? buf_active : buf_passive);
	}
#else
	b = (u->active ? buf_active : buf_passive);
#endif
	if (bf_used (b))
	  if (server_write (u->parent, b) > 0)
	    server_settimeout (u->parent, PROTO_TIMEOUT_ONLINE);
      }
      /* send extra OP buffer */
      if (miuo && u->op)
	server_write (u->parent, buf_op);
      /* write out researches to recent clients */
      if (ars || (u->active && prs)) {
	if (u->joinstamp > deadline) {
	  server_write (u->parent, (u->active ? buf_aresearch : buf_presearch));
	}
      }
    };
  }
#ifdef ZLINES
  if (buf_zpipepassive && (buf_zpipepassive != buf_passive))
    bf_free (buf_zpipepassive);
  if (buf_zpipeactive && (buf_zpipeactive != buf_active))
    bf_free (buf_zpipeactive);
  if (buf_zlinepassive && (buf_zlinepassive != buf_passive))
    bf_free (buf_zlinepassive);
  if (buf_zlineactive && (buf_zlineactive != buf_active))
    bf_free (buf_zlineactive);
#endif

  bf_free (buf_passive);
  bf_free (buf_active);
  bf_free (buf_exception);
  bf_free (buf_op);
  bf_free (buf_aresearch);
  bf_free (buf_presearch);

  if (ch)
    cache_clear (cache.chat);
  if (mi)
    cache_clear (cache.myinfo);
  if (miu)
    cache_clear (cache.myinfoupdate);
  if (miuo)
    cache_clear (cache.myinfoupdateop);
  if (ps)
    cache_clear (cache.psearch);
  if (as)
    cache_clear (cache.asearch);
  if (prs)
    cache_clear (cache.presearch);
  if (ars)
    cache_clear (cache.aresearch);
  if (res)
    cache_clear (cache.results);
  if (pm)
    cache_clear (cache.privatemessages);

  DPRINTF
    ("\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\ end cache flush /////////////////////////////\n");

  plugin_send_event (NULL, PLUGIN_EVENT_CACHEFLUSH, NULL);
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

  return 0;
}

int proto_nmdc_setup ()
{
  HubSec = proto_nmdc_user_addrobot (config.HubSecurityNick, config.HubSecurityDesc);
  HubSec->flags |= PROTO_FLAG_HUBSEC;
  plugin_new_user ((plugin_private_t **) & HubSec->plugin_priv, HubSec, &nmdc_proto);
  return 0;
}
