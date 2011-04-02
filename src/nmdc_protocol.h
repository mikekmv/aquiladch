/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
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

#define NMDC_SUPPORTS_ZLines		2048	/* */


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

#define cache_queue(element, user, buffer)	{string_list_add (&element.messages , user, buffer); element.length += bf_used (buffer);}
#define cache_purge(element, user)		{string_list_entry_t *entry = string_list_find (&element.messages, user); while (entry) { element.length -= bf_used (entry->data); string_list_del (&element.messages, entry); entry = string_list_find (&element.messages, user); };}
#define cache_clear(element)			{string_list_clear (&element.messages); element.length = 0;}

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
  cache_element_t results;	/* results */
  cache_element_t privatemessages;	/* privatemessages */
  cache_element_t quit;		/* quit list */

  /*
   *
   */
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
  buffer_t *infolistz;
  buffer_t *nicklistz;
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
  leaky_bucket_type_t chat;
  leaky_bucket_type_t search;
  leaky_bucket_type_t myinfo;
  leaky_bucket_type_t getnicklist;
  leaky_bucket_type_t getinfo;
  leaky_bucket_type_t downloads;
  leaky_bucket_type_t connects;
  leaky_bucket_type_t psresults;
} ratelimiting_t;

extern ratelimiting_t rates;

typedef struct {
  unsigned long cacherebuild;	/* rebuild of nick list cache */
  unsigned long userjoin;	/* all user joins */
  unsigned long userpart;	/* all user parts */
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
