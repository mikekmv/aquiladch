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
