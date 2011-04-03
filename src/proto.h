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

#ifndef _PROTO_H_
#define _PROTO_H_

#include "defaults.h"
#include <arpa/inet.h>
#include "hashlist.h"
#include "buffer.h"
#include "banlist.h"

#define PROTO_FLAG_MASK			0xffff	/* use this to remove protocol specific flags. */
#define PROTO_FLAG_VIRTUAL		1
#define PROTO_FLAG_ONLINE		2
#define PROTO_FLAG_REGISTERED		4
#define PROTO_FLAG_ZOMBIE		8

typedef struct user {
  hashlist_entry_t hash;
  struct user *next, *prev;
  
  unsigned char nick[NICKLENGTH];
  unsigned long long share;	/* share size */
  int active;			/* active? 1: active, 0: passive, -1: invalid */
  unsigned long ipaddress;	/* ip address */
  unsigned char client[64];	/* client used */
  unsigned char versionstring[64];	/* client version */
  double version;
  unsigned int flags;
  unsigned long rights;
  unsigned long joinstamp;

  /* plugin private user data */
  void *plugin_priv;

  /* back linking pointer for client_t * */
  void *parent;
} user_t;

typedef struct proto {
  struct proto * (*init) (void);
  int (*setup) (void);

  int (*handle_input) (user_t * user, buffer_t ** buffers);
  int (*handle_token) (user_t *, buffer_t *);
  void (*flush_cache) (void);

  user_t *(*user_alloc) (void *priv, unsigned long ipaddress);
  int (*user_free) (user_t *);

  int (*user_disconnect) (user_t *, char *);

  int (*user_forcemove) (user_t *, unsigned char *, buffer_t *);
  int (*user_redirect) (user_t *, buffer_t *);
  int (*user_drop) (user_t *, buffer_t *);
  user_t *(*user_find) (unsigned char *);
  user_t *(*user_find_ip) (user_t * last, unsigned long ip);
  user_t *(*user_find_net) (user_t * last, unsigned long ip, unsigned long netmask);

  user_t *(*robot_add) (unsigned char *nick, unsigned char *description);
  int (*robot_del) (user_t *);

  int (*chat_main) (user_t *, buffer_t *);
  int (*chat_send) (user_t *, user_t *, buffer_t *);
  int (*chat_send_direct) (user_t *, user_t *, buffer_t *);
  int (*chat_priv) (user_t *, user_t *, user_t *, buffer_t *);
  int (*chat_priv_direct) (user_t *, user_t *, user_t *, buffer_t *);
  int (*raw_send) (user_t *, buffer_t *);
  int (*raw_send_all) (buffer_t *);

  banlist_t *(*get_banlist_hard) ();
  banlist_t *(*get_banlist_soft) ();

  user_t *HubSec;

  unsigned char *name;
} proto_t;

#endif /* _PROTO_H_ */
