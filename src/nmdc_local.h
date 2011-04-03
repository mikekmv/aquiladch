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

#ifndef _NMDC_LOCAL_H_
#define _NMDC_LOCAL_H_

#include "proto.h"
#include "nmdc_protocol.h"
#include "searchlist.h"
#include "leakybucket.h"
#include "plugin.h"
#include "tth.h"

#define PROTO_STATE_INIT         0	/* initial creation state */
#define PROTO_STATE_SENDLOCK     1	/* waiting for user $Key */
#define PROTO_STATE_WAITNICK     2	/* waiting for user $ValidateNick */
#define PROTO_STATE_WAITPASS     3	/* waiting for user $MyPass */
#define PROTO_STATE_HELLO        4	/* waiting for user $MyInfo */
#define PROTO_STATE_ONLINE       5	/* uaser is online and active */
#define PROTO_STATE_DISCONNECTED 6	/* user is disconnected */
#define PROTO_STATE_VIRTUAL	  7	/* this is a "fake" user, for robots */

/* everything in ms */
#define PROTO_TIMEOUT_INIT		5000	/* initial creation state */
#define PROTO_TIMEOUT_SENDLOCK 		5000	/* waiting for user $Key */
#define PROTO_TIMEOUT_WAITNICK		5000	/* waiting for user $ValidateNick */
#define PROTO_TIMEOUT_WAITPASS		30000	/* waiting for user $MyPass */
#define PROTO_TIMEOUT_HELLO		5000	/* waiting for user $MyInfo */
#define PROTO_TIMEOUT_ONLINE		300000	/* send $ForceMove command to user */

/* define extra size of nicklist infobuffer. when this extra space is full, 
 * nicklist is rebuild. keep this relatively small to reduce bw overhead.
 * the actual space will grow with the number of users logged in.
 * the number is used to shift the size.
 *  3 corresponds to a 12.5 % extra size before rebuild.
 */

#define NICKLISTCACHE_SPARE	3

typedef struct cache_element {
  string_list_t messages;
  unsigned long length;
  leaky_bucket_t timer;
  leaky_bucket_type_t timertype;
} cache_element_t;

/* FIXME split of nmdc specific fields */
typedef struct nmdc_user {
  user_t user;

  int state;			/* current connection state */

  unsigned long supports;

  unsigned int slots;		/* slots user have open */
  unsigned int hubs[3];		/* hubs user is in */
  unsigned int op;

  /* user data */
  unsigned char lock[LOCKLENGTH];

  /* rate limiting counters */
  leaky_bucket_t rate_warnings;
  leaky_bucket_t rate_violations;
  leaky_bucket_t rate_chat;
  leaky_bucket_t rate_search;
  leaky_bucket_t rate_myinfo;
  leaky_bucket_t rate_myinfoop;
  leaky_bucket_t rate_getnicklist;
  leaky_bucket_t rate_getinfo;
  leaky_bucket_t rate_downloads;
  leaky_bucket_t rate_psresults_in;
  leaky_bucket_t rate_psresults_out;

  /* cache counters */
  unsigned int ChatCnt, SearchCnt, ResultCnt, MessageCnt;
  unsigned int CacheException;

  /* search caching */
  tth_list_t *tthlist;

  /* cache data */
  buffer_t *MyINFO;

  /* pointer for protocol private data */
  cache_element_t privatemessages;
  cache_element_t results; 
} nmdc_user_t;

#define cache_queue(element, usr, buffer)	{string_list_add (&(element).messages , usr, buffer); (element).length += bf_size (buffer);}
//#define cache_count(element, usr, buffer)	{(element).messages.count++; (element).messages.size += bf_size (buffer); (element).length += bf_size (buffer);}
#define cache_count(element, usr)		{ if (cache.element.messages.count < usr->element.messages.count) cache.element.messages.count = usr->element.messages.count; if (cache.element.messages.size < usr->element.messages.size) cache.element.messages.size = usr->element.messages.size; if (cache.element.length < usr->element.length) cache.element.length = usr->element.length ;}
#define cache_purge(element, usr)		{string_list_entry_t *entry = string_list_find (&(element).messages, (usr ? usr : NULL)); while (entry) { (element).length -= bf_size (entry->data); string_list_del (&(element).messages, entry); entry = string_list_find (&(element).messages, usr); };}
#define cache_clear(element)			{string_list_clear (&(element).messages); (element).length = 0;}
#define cache_clearcount(element)              { element.messages.count = 0; (element).length = 0;}

typedef struct {
  /*
   *    Normal communication caching
   */
  cache_element_t chat;		/* chatmessages */
  cache_element_t myinfo;	/* my info messages */
  cache_element_t myinfoupdate;	/* my info update messages */
  cache_element_t myinfoupdateop;	/* my info update messages for ops. not shortened and immediate */
  cache_element_t asearch;	/* active search list */
  cache_element_t psearch;	/* passive search list */
  cache_element_t aresearch;	/* active search list */
  cache_element_t presearch;	/* passive search list */
  cache_element_t results;	/* results */
  cache_element_t privatemessages;	/* privatemessages */

  /*
   *
   */
  unsigned long ZpipeSupporters;
  unsigned long ZlineSupporters;

  /*
   *  nicklist caching
   */
  unsigned long usercount;
  unsigned long lastrebuild;
  unsigned long needrebuild;
  buffer_t *nicklist;
  buffer_t *oplist;
  buffer_t *infolist;
  buffer_t *hellolist;
#ifdef ZLINES
  buffer_t *infolistzline;
  buffer_t *nicklistzline;
  buffer_t *infolistzpipe;
  buffer_t *nicklistzpipe;
#endif
  buffer_t *infolistupdate;
  unsigned long length_estimate;
  unsigned long length_estimate_op;
  unsigned long length_estimate_info;
} cache_t;

extern int nicklistcache_adduser (nmdc_user_t * u);
extern int nicklistcache_updateuser (nmdc_user_t * u, buffer_t * new);
extern int nicklistcache_deluser (nmdc_user_t * u);
//extern int nicklistcache_rebuild (struct timeval now);
extern int nicklistcache_sendnicklist (nmdc_user_t * target);
extern int nicklistcache_sendoplist (nmdc_user_t * target);


extern cache_t cache;
extern ratelimiting_t rates;
extern nmdc_user_t *userlist;
extern hashlist_t hashlist;

extern plugin_manager_t *nmdc_pluginmanager;

extern nmdc_user_t *   cachelist;
extern nmdc_user_t *   cachelist_last;
extern hashlist_t cachehashlist;

extern nmdc_user_t *HubSec;
extern server_t *nmdc_server;

extern unsigned int keylen;
extern unsigned int keyoffset;
extern char key[16 + sizeof (LOCK) + 4 + LOCKLENGTH + 1];

extern banlist_t reconnectbanlist;
extern banlist_t softbanlist;

extern unsigned int cloning;

extern unsigned int chatmaxlength;
extern unsigned int searchmaxlength;
extern unsigned int srmaxlength;
extern unsigned int researchmininterval, researchperiod, researchmaxcount;

extern unsigned char *defaultbanmessage;

/* function prototypes */
extern proto_t *proto_nmdc_setup ();
extern proto_t *proto_nmdc_init ();
extern int proto_nmdc_handle_token (nmdc_user_t * u, buffer_t * b);
extern int proto_nmdc_handle_input (nmdc_user_t * user, buffer_t ** buffers);
extern void proto_nmdc_flush_cache ();
extern int proto_nmdc_user_disconnect (nmdc_user_t * u, char * reason);
extern int proto_nmdc_user_forcemove (nmdc_user_t * u, unsigned char *destination, buffer_t * message);
extern int proto_nmdc_user_redirect (nmdc_user_t * u, buffer_t * message);
extern int proto_nmdc_user_drop (nmdc_user_t * u, buffer_t * message);
extern nmdc_user_t *proto_nmdc_user_find (unsigned char *nick);
extern nmdc_user_t *proto_nmdc_user_alloc (void *priv, unsigned long ipaddress);
extern int proto_nmdc_user_free (nmdc_user_t * user);

extern nmdc_user_t *proto_nmdc_user_addrobot (unsigned char *nick, unsigned char *description);
extern int proto_nmdc_user_delrobot (nmdc_user_t * u);

extern int proto_nmdc_user_say (nmdc_user_t * u, buffer_t * b, buffer_t * message);
extern int proto_nmdc_user_say_string (nmdc_user_t * u, buffer_t * b, unsigned char *message);

extern int proto_nmdc_user_warn (nmdc_user_t * u, struct timeval *now, const char *message, ...);

extern int proto_nmdc_user_chat_all (nmdc_user_t * u, buffer_t * message);
extern int proto_nmdc_user_send (nmdc_user_t * u, nmdc_user_t * target, buffer_t * message);
extern int proto_nmdc_user_send_direct (nmdc_user_t * u, nmdc_user_t * target, buffer_t * message);
extern int proto_nmdc_user_priv (nmdc_user_t * u, nmdc_user_t * target, nmdc_user_t * source, buffer_t * message);
extern int proto_nmdc_user_priv_direct (nmdc_user_t * u, nmdc_user_t * target, nmdc_user_t * source, buffer_t * message);
extern int proto_nmdc_user_raw (nmdc_user_t * target, buffer_t * message);
extern int proto_nmdc_user_raw_all (buffer_t * message);

extern banlist_t *proto_nmdc_banlist_hard (void);
extern banlist_t *proto_nmdc_banlist_soft (void); 


extern void proto_nmdc_user_cachelist_add (nmdc_user_t *user);
extern void proto_nmdc_user_cachelist_invalidate (nmdc_user_t *u);
extern void proto_nmdc_user_cachelist_clear ();

extern int proto_nmdc_violation (nmdc_user_t * u, struct timeval *now, char *reason);

#endif
