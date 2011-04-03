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

#ifndef _PROTOCOL_H_
#define _PROTOCOL_H_

#include "stringlist.h"
#include "cap.h"
#include "leakybucket.h"

#define PROTOCOL_REBUILD_PERIOD	3

#define NMDC_SUPPORTS_NoGetINFO       1	/* no need for GetINFOs */
#define NMDC_SUPPORTS_NoHello         2	/* no $Hello for new signons */
#define NMDC_SUPPORTS_UserCommand     4	/* */
#define NMDC_SUPPORTS_UserIP2		8	/* */
#define NMDC_SUPPORTS_QuickList	16	/* NOT SUPPORTED ! deprecated. */
#define NMDC_SUPPORTS_TTHSearch	32	/* client can handle TTH searches */

#define NMDC_SUPPORTS_ZPipe		1024	/* */
#define NMDC_SUPPORTS_ZLine		2048	/* */


/* define extra size of nicklist infobuffer. when this extra space is full, 
 * nicklist is rebuild. keep this relatively small to reduce bw overhead.
 * the actual space will grow with the number of users logged in.
 * the number is used to shift the size.
 *  3 corresponds to a 12.5 % extra size before rebuild.
 */
#define NICKLISTCACHE_SPARE	3


typedef struct {
  string_list_t messages;
  unsigned long length;
  leaky_bucket_t timer;
  leaky_bucket_type_t timertype;
} cache_element_t;

#define cache_queue(element, user, buffer)	{string_list_add (&(element).messages , user, buffer); (element).length += bf_size (buffer);}
//#define cache_count(element, user, buffer)	{(element).messages.count++; (element).messages.size += bf_size (buffer); (element).length += bf_size (buffer);}
#define cache_count(element, user)		{ if (cache.element.messages.count < ((nmdc_user_t *) user->pdata)->element.messages.count) cache.element.messages.count = ((nmdc_user_t *) user->pdata)->element.messages.count; if (cache.element.messages.size < ((nmdc_user_t *) user->pdata)->element.messages.size) cache.element.messages.size = ((nmdc_user_t *) user->pdata)->element.messages.size; if (cache.element.length < ((nmdc_user_t *) user->pdata)->element.length) cache.element.length = ((nmdc_user_t *) user->pdata)->element.length ;}
#define cache_purge(element, user)		{string_list_entry_t *entry = string_list_find (&(element).messages, user); while (entry) { (element).length -= bf_size (entry->data); string_list_del (&(element).messages, entry); entry = string_list_find (&(element).messages, user); };}
#define cache_clear(element)			{string_list_clear (&(element).messages); (element).length = 0;}

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

#define NMDC_FLAG_DELAYEDNICKLIST	0x00010000
#define NMDC_FLAG_BOT			0x00020000
#define NMDC_FLAG_CACHED		0x00040000

typedef struct ratelimiting {
  leaky_bucket_type_t warnings;
  leaky_bucket_type_t chat;
  leaky_bucket_type_t asearch;
  leaky_bucket_type_t psearch;
  leaky_bucket_type_t research;
  leaky_bucket_type_t myinfo;
  leaky_bucket_type_t getnicklist;
  leaky_bucket_type_t getinfo;
  leaky_bucket_type_t downloads;
  leaky_bucket_type_t connects;
  leaky_bucket_type_t psresults_in;
  leaky_bucket_type_t psresults_out;
} ratelimiting_t;

extern ratelimiting_t rates;

typedef struct nmdc_user {
  cache_element_t privatemessages;
  cache_element_t results;
} nmdc_user_t;

typedef struct {
  unsigned long cacherebuild;	/* rebuild of nick list cache */
  unsigned long userjoin;	/* all user joins */
  unsigned long userpart;	/* all user parts */
  unsigned long banned;		/* all forcemoves  for banned users */
  unsigned long forcemove;	/* all forcemoves */
  unsigned long disconnect;	/* all drops/disconnects */
  unsigned long redirect;	/* all redirects */
  unsigned long tokens;		/* all tokens processed */
  unsigned long brokenkey;	/* all users refused cuz of broken key */
  unsigned long usednick;	/* all users refused cuz of nickname already used */
  unsigned long softban;
  unsigned long nickban;
  unsigned long badpasswd;
  unsigned long notags;
  unsigned long badmyinfo;
  unsigned long preloginevent;
  unsigned long loginevent;
  unsigned long chatoverflow;	/* when a user oversteps his chat allowence */
  unsigned long chatfakenick;	/* when a user fakes his source nick */
  unsigned long chattoolong;
  unsigned long chatevent;
  unsigned long myinfooverflow;
  unsigned long myinfoevent;
  unsigned long searchoverflow;
  unsigned long searchevent;
  unsigned long searchtoolong;
  unsigned long sroverflow;
  unsigned long srevent;
  unsigned long srrobot;
  unsigned long srtoolong;
  unsigned long srfakesource;
  unsigned long srnodest;
  unsigned long ctmoverflow;
  unsigned long ctmbadtarget;
  unsigned long rctmoverflow;
  unsigned long rctmbadtarget;
  unsigned long rctmbadsource;
  unsigned long pmoverflow;
  unsigned long pmoutevent;
  unsigned long pmbadtarget;
  unsigned long pmbadsource;
  unsigned long pminevent;
  unsigned long botinfo;
  unsigned long cache_quit;
  unsigned long cache_myinfo;
  unsigned long cache_myinfoupdate;
  unsigned long cache_chat;
  unsigned long cache_asearch;
  unsigned long cache_psearch;
  unsigned long cache_messages;
  unsigned long cache_results;


} nmdc_stats_t;
extern nmdc_stats_t nmdc_stats;

/*
 *  This stuff includes and defines the protocol structure to be exported.
 */
#include "proto.h"
extern proto_t nmdc_proto;

#endif
