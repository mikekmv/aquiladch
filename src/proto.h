/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#ifndef _PROTO_H_
#define _PROTO_H_

#include <arpa/inet.h>
#include "hashlist.h"
#include "buffer.h"
#include "config.h"
#include "leakybucket.h"
#include "searchlist.h"

#define PROTO_STATE_INIT         0	/* initial creation state */
#define PROTO_STATE_SENDLOCK     1	/* waiting for user $Key */
#define PROTO_STATE_WAITNICK     2	/* waiting for user $ValidateNick */
#define PROTO_STATE_WAITPASS     3	/* waiting for user $MyPass */
#define PROTO_STATE_HELLO        4	/* waiting for user $MyInfo */
#define PROTO_STATE_ONLINE       5	/* uaser is online and active */
#define PROTO_STATE_DISCONNECTED 6	/* user is disconnected */
#define PROTO_STATE_VIRTUAL	  7	/* this is a "fake" user, for robots */

#define PROTO_TIMEOUT_INIT		5000	/* initial creation state */
#define PROTO_TIMEOUT_SENDLOCK 		5000	/* waiting for user $Key */
#define PROTO_TIMEOUT_WAITNICK		5000	/* waiting for user $ValidateNick */
#define PROTO_TIMEOUT_WAITPASS		30000	/* waiting for user $MyPass */
#define PROTO_TIMEOUT_HELLO		5000	/* waiting for user $MyInfo */
#define PROTO_TIMEOUT_ONLINE		300000	/* send $ForceMove command to user */
#define PROTO_TIMEOUT_BUFFERING		10000	/* we buffer for a max of x seconds. then disconnect */
#define PROTO_TIMEOUT_OVERFLOW		1000	/* on too many buffers, we buffer for a max of x seconds. then disconnect */
#define PROTO_TIMEOUT_DISCONNECTED	2000	/* users is disconnected */

#define PROTO_FLAG_MASK			0xffff	/* use this to remove protocol specific flags. */
#define PROTO_FLAG_HUBSEC		1
#define PROTO_FLAG_REGISTERED		2
#define PROTO_FLAG_ZOMBIE		4

/* FIXME split of nmdc specific fields */
typedef struct user {
  hashlist_entry_t hash;
  struct user *next, *prev;

  int state;			/* current connection state */

  unsigned char nick[NICKLENGTH];
  unsigned long supports;

  unsigned long long share;	/* share size */
  int active;			/* active? 1: active, 0: passive, -1: invalid */
  unsigned int slots;		/* slots user have open */
  unsigned int hubs[3];		/* hubs user is in */
  unsigned long ipaddress;	/* ip address */
  unsigned char client[64];	/* client used */
  unsigned char versionstring[64];	/* client version */
  double version;
  unsigned int op;
  unsigned int flags;
  unsigned long rights;

  /* user data */
  unsigned char lock[LOCKLENGTH];
  unsigned long joinstamp;

  /* plugin private user data */
  void *plugin_priv;

  /* rate limiting counters */
  leaky_bucket_t rate_chat;
  leaky_bucket_t rate_search;
  leaky_bucket_t rate_myinfo;
  leaky_bucket_t rate_getnicklist;
  leaky_bucket_t rate_getinfo;
  leaky_bucket_t rate_downloads;
  leaky_bucket_t rate_psresults;

  /* cache counters */
  unsigned int ChatCnt, SearchCnt, ResultCnt, MessageCnt;
  unsigned int CacheException;

  /* search caching */
  searchlist_t searchlist;

  /* cache data */
  buffer_t *MyINFO;

  /* pointer for protocol private data */
  void *pdata;

  /* back linking pointer for parent */
  void *parent;
} user_t;

typedef struct {
  int (*init) (void);
  int (*setup) (void);

  int (*handle_input) (user_t * user, buffer_t ** buffers);
  int (*handle_token) (user_t *, buffer_t *);
  void (*flush_cache) (void);

  user_t *(*user_alloc) (void *priv);
  int (*user_free) (user_t *);

  int (*user_disconnect) (user_t *);

  int (*user_forcemove) (user_t *, unsigned char *, buffer_t *);
  int (*user_redirect) (user_t *, buffer_t *);
  int (*user_drop) (user_t *, buffer_t *);
  user_t *(*user_find) (unsigned char *);

  user_t *(*robot_add) (unsigned char *nick, unsigned char *description);
  int (*robot_del) (user_t *);

  int (*chat_main) (user_t *, buffer_t *);
  int (*chat_send) (user_t *, user_t *, buffer_t *);
  int (*chat_priv) (user_t *, user_t *, user_t *, buffer_t *);
  int (*chat_priv_direct) (user_t *, user_t *, user_t *, buffer_t *);

  unsigned char *name;
} proto_t;

#endif /* _PROTO_H_ */
