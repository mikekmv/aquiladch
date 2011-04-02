/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#ifndef _BANLISTUSER_H_
#define _BANLISTUSER_H_

#include "../config.h"
#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
#  include <stdint.h>
# endif
#endif

#include "config.h"
#include "buffer.h"
#include "dllist.h"

#define BANLIST_NICK_HASHBITS   10
#define BANLIST_NICK_HASHSIZE	(1 << BANLIST_NICK_HASHBITS)
#define BANLIST_NICK_HASHMASK	(BANLIST_NICK_HASHSIZE-1)

typedef struct banlist_nick {
  dllist_entry_t dllist;

  unsigned char nick[NICKLENGTH];
  buffer_t *message;
  time_t expire;
} banlist_nick_entry_t;

typedef dllist_t banlist_nick_t;

extern banlist_nick_entry_t *banlist_nick_add (banlist_nick_t * list, unsigned char *nick,
					       buffer_t * reason, unsigned long expire);

extern unsigned int banlist_nick_del (banlist_nick_t * list, banlist_nick_entry_t *);
extern unsigned int banlist_nick_del_bynick (banlist_nick_t * list, unsigned char *nick);

extern banlist_nick_entry_t *banlist_nick_find (banlist_nick_t * list, unsigned char *nick);

extern unsigned int banlist_nick_cleanup (banlist_nick_t * list);
extern void banlist_nick_clear (banlist_nick_t * list);

extern unsigned int banlist_nick_save (banlist_nick_t * list, unsigned char *file);
extern unsigned int banlist_nick_load (banlist_nick_t * list, unsigned char *file);

extern void banlist_nick_init (banlist_nick_t * list);

#endif /* _BANLIST_H_ */