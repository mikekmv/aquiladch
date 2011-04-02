/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#include "dllist.h"
#include <errno.h>

__inline__ void dllist_del (dllist_entry_t *e) {
	e->next->prev = e->prev;
	e->prev->next = e->next;
}

__inline__ void dllist_append (dllist_entry_t* list, dllist_entry_t *new) {
	new->next = list->next;
	new->next->prev = new;
	new->prev = list;
	list->next = new;
}

__inline__ void dllist_prepend (dllist_entry_t *list, dllist_entry_t *new) {
	new->prev = list->prev;
	new->prev->next = new;
	new->next = list;
	list->prev = new;
}

__inline__ void dllist_init (dllist_entry_t* list) {
	list->next = list;
	list->prev = list;
}

__inline__ int dlhashlist_init (dllist_t *list, unsigned long buckets) {
	unsigned long i;
	dllist_entry_t *e;
	
	if (!list->hashlist) 
		list->hashlist = malloc (sizeof(dllist_entry_t) * buckets);
		
	if (!list->hashlist)
		return -errno;
		
	list->buckets = buckets;
	for (i=0, e = list->hashlist; i < buckets; i++, e++) {
		e->next = e;
		e->prev = e;
	}
	return 0;
}
