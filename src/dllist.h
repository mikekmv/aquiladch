/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#ifndef _DLLIST_H_
#define _DLLIST_H_

#include <stdlib.h>

/*
 * Structure definitions
 */

typedef struct dllist_entry {
  struct dllist_entry *next, *prev;
} dllist_entry_t;

typedef struct {
  dllist_entry_t *hashlist;
  unsigned long buckets;
} dllist_t;

/*
 * Inline functions and some defines
 */

#define dllist_bucket(lst, val) ((void *)&(((dllist_t *)lst)->hashlist[val]))
#define dllist_foreach(list, var) for (var = (void *)((dllist_entry_t *)list)->next; (void *)var != (void*)list; var = (void *)((dllist_entry_t *)var)->next)
#define dlhashlist_foreach(list, bckt) for (bckt=0; bckt < ((dllist_t *)list)->buckets; bckt++)

#define dllist_first(lst) ((void*)((dllist_entry_t *)list)->next)
#define dllist_next(e) ((void*)((dllist_entry_t *)e)->next)
#define dllist_prev(e) ((void*)((dllist_entry_t *)e)->prev)
#define dllist_end(lst) (lst)

extern __inline__ void dllist_del (dllist_entry_t * e);
extern __inline__ void dllist_append (dllist_entry_t * list, dllist_entry_t * new);
extern __inline__ void dllist_prepend (dllist_entry_t * list, dllist_entry_t * new);
extern __inline__ void dllist_init (dllist_entry_t * list);
extern __inline__ int dlhashlist_init (dllist_t * list, unsigned long buckets);

#define dlhashlist_append(lst, hsh, entry) dllist_append (&(((dllist_t *)lst)->hashlist[hsh]), entry)
#define dlhashlist_prepend(lst, hsh, Entry) dllist_prepend (&( ((dllist_t *)lst)->hashlist[hsh] ), (dllist_entry_t *)Entry)

#endif /* _DLLIST_H_ */
