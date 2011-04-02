/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#ifndef _SEARCHLIST_H_
#define _SEARCHLIST_H_

#include "buffer.h"

struct user;

typedef struct searchlist_entry {
  struct searchlist_entry *next, *prev;
  struct searchlist_entry *hnext, *hprev;
  struct user *user;
  unsigned long timestamp;
  buffer_t *data;
} searchlist_entry_t;

typedef struct searchlist {
  struct searchlist_entry *first, *last, **hash;
  unsigned int count;
  unsigned long size;
} searchlist_t;

inline void searchlist_init (searchlist_t * list);
inline searchlist_entry_t *searchlist_add (searchlist_t * list, struct user *user, buffer_t *);
inline void searchlist_del (searchlist_t * list, searchlist_entry_t * entry);
inline void searchlist_update (searchlist_t * list, searchlist_entry_t * entry);
inline void searchlist_purge (searchlist_t * list, struct user *user);
inline searchlist_entry_t *searchlist_find (searchlist_t * list, struct user *user);
inline void searchlist_clear (searchlist_t * list);

#endif
