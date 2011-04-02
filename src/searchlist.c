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
#include <time.h>
#include "searchlist.h"

inline void searchlist_init (searchlist_t * list)
{
  list->first = NULL;
  list->last = NULL;
  list->count = 0;
  list->size = 0;
}

inline searchlist_entry_t *searchlist_add (searchlist_t * list, struct user *user, buffer_t * data)
{
  searchlist_entry_t *entry;

  entry = malloc (sizeof (searchlist_entry_t));
  entry->next = NULL;
  entry->user = user;
  entry->data = data;
  entry->prev = list->last;
  time (&entry->timestamp);
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

inline void searchlist_del (searchlist_t * list, searchlist_entry_t * entry)
{
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

inline void searchlist_update (searchlist_t * list, searchlist_entry_t * entry)
{
  /* update timestamp */
  time (&entry->timestamp);

  if (entry == list->last)
    return;

  /* remove from list */
  entry->next->prev = entry->prev;
  if (entry->prev) {
    entry->prev->next = entry->next;
  } else {
    list->first = entry->next;
  }
  /* re-add at end */
  entry->next = NULL;
  entry->prev = list->last;
  entry->prev->next = entry;
  list->last = entry;
  if (!list->first)
    list->first = entry;
}

inline void searchlist_purge (searchlist_t * list, struct user *user)
{
  searchlist_entry_t *entry, *next;

  entry = list->first;
  while (entry) {
    next = entry->next;
    if (entry->user == user)
      searchlist_del (list, entry);
    entry = next;
  };
}

inline searchlist_entry_t *searchlist_find (searchlist_t * list, struct user *user)
{
  searchlist_entry_t *entry, *next;

  entry = list->first;
  while (entry) {
    next = entry->next;
    if (entry->user == user)
      return entry;
    entry = next;
  };

  return NULL;
}

inline void searchlist_clear (searchlist_t * list)
{
  searchlist_entry_t *entry, *next;

  entry = list->first;
  while (entry) {
    next = entry->next;
    if (entry->data)
      bf_free (entry->data);
    free (entry);
    entry = next;
  };
  list->first = NULL;
  list->last = NULL;
  list->count = 0;
  list->size = 0;
}
