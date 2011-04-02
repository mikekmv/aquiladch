/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#ifndef _STRINGLIST_H_
#define _STRINGLIST_H_

#include "buffer.h"

struct user;

typedef struct string_list_entry {
	struct string_list_entry *next, *prev;
	struct string_list_entry *hnext, *hprev;
	struct user 	*user;
	buffer_t        *data;
} string_list_entry_t;

typedef struct string_list {
	struct string_list_entry *first, *last, **hash;
	unsigned int count;
	unsigned long size;
}  string_list_t;

inline void string_list_init (string_list_t *list);
inline string_list_entry_t *string_list_add (string_list_t *list, struct user * user, buffer_t *);
inline void string_list_del (string_list_t *list, string_list_entry_t *entry);
inline void string_list_purge (string_list_t *list, struct user *user);
inline string_list_entry_t *string_list_find (string_list_t *list, struct user *user);
inline void string_list_clear (string_list_t *list);

#endif
