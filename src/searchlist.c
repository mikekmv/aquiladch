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
