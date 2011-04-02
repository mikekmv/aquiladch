/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#include <stdio.h>
#include <stdlib.h>
#include "stringlist.h"

inline void string_list_init (string_list_t *list) {
	list->first = NULL;
	list->last  = NULL;
	list->count = 0;
	list->size = 0;
}

inline string_list_entry_t *string_list_add (string_list_t *list, struct user * user, buffer_t *data) {
	string_list_entry_t *entry;
	
	entry = malloc (sizeof (string_list_entry_t));
	entry->next = NULL;
	entry->user = user;
	entry->data = data;
	entry->prev = list->last;
	if (list->last) 
		list->last->next = entry;
	list->last = entry;
	if (!list->first)
		list->first = entry;
	
	list->count++;
	list->size += bf_size (data);

	bf_claim (data);

	return entry;
}

inline void string_list_del (string_list_t *list, string_list_entry_t *entry) {
	if (entry->next) {
		entry->next->prev = entry->prev;
	} else {
		list->last = entry->prev;
	}
	if (entry->prev) {
		entry->prev->next = entry->next;
	} else {	
		list->first = entry->next;
	}
	if (entry->data) {
	 	list->size -= bf_size (entry->data);
		bf_free (entry->data);
	}

	free (entry);
	list->count--;
}

inline void string_list_purge (string_list_t *list, struct user *user) {
	string_list_entry_t *entry, *next;
	
	entry = list->first;
	while (entry) {
		next = entry->next; 
		if (entry->user == user)
			string_list_del (list, entry);
		entry = next;
	};
}

inline string_list_entry_t *string_list_find (string_list_t *list, struct user *user) {
	string_list_entry_t *entry, *next;
	
	entry = list->first;
	while (entry) {
		next = entry->next; 
		if (entry->user == user)
			return entry;
		entry = next;
	};
	
	return NULL;
}

inline void string_list_clear (string_list_t *list) {
	string_list_entry_t *entry, *next;

	entry = list->first;
	while (entry) {
		next = entry->next; 
		if (entry->data)
			bf_free (entry->data);
		free (entry);
		entry = next;
	};
	list->first = NULL;
	list->last  = NULL;
	list->count = 0;
	list->size = 0;
}

