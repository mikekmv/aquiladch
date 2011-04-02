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
#include <sys/time.h>
#include <string.h>

#include <arpa/inet.h>

#include "defaults.h"
#include "banlist.h"
#include "buffer.h"

typedef union {
  uint32_t ip;
  char key[4];
} map_t;

__inline__ uint32_t one_at_a_time (uint32_t key)
{
  uint32_t hash, i;
  map_t m;

  m.ip = key;
  for (hash = 0, i = 0; i < 4; ++i) {
    hash += m.key[i];
    hash += (hash << 10);
    hash ^= (hash >> 6);
  }

  hash += (hash << 3);
  hash ^= (hash >> 11);
  hash += (hash << 15);
  return hash;
}

__inline__ uint32_t netmask_to_numbits (uint32_t netmask)
{
  uint32_t nm, i;

  netmask = htonl (netmask);
  /* determine netmask */
  i = 32;
  nm = 0xffffffff;
  while (nm && (nm != netmask)) {
    nm = (nm << 1) & 0xfffffffe;
    --i;
  };

  return i;
}

banlist_entry_t *banlist_add (banlist_t * list, uint32_t ip, uint32_t netmask, buffer_t * reason,
			      unsigned long expire)
{
  banlist_entry_t *b;
  uint32_t i;

  /* bad netmask */
  i = netmask_to_numbits (netmask);
  if (i > 32)
    return NULL;

  /* delete any earlier bans of this ip. */
  banlist_del_byip (list, ip, netmask);

  /* alloc and clear new element */
  b = malloc (sizeof (banlist_entry_t));

  /* init */
  dllist_init (&b->dllist);
  b->ip = ip & netmask;
  b->netmask = netmask;
  b->message = reason;
  bf_claim (reason);
  b->expire = expire;

  /* mask netmask as used */
  list->netmask_inuse[i]++;

  /* put in hashlist */
  dlhashlist_prepend (&list->list, one_at_a_time (ip & netmask) & BANLIST_HASHMASK, b);

  return b;
}

unsigned int banlist_del (banlist_t * list, banlist_entry_t * e)
{
  if (!e)
    return 0;

  ASSERT (list->netmask_inuse[netmask_to_numbits (e->netmask)]);
  list->netmask_inuse[netmask_to_numbits (e->netmask)]--;

  dllist_del ((dllist_entry_t *) e);
  if (e->message)
    bf_free (e->message);
  free (e);

  return 1;
}

unsigned int banlist_del_byip (banlist_t * list, uint32_t ip, uint32_t netmask)
{
  banlist_entry_t *e, *l;

  l = dllist_bucket (&list->list, one_at_a_time (ip & netmask) & BANLIST_HASHMASK);
  dllist_foreach (l, e)
    if (e->ip == ip)
    break;

  if (e == dllist_end (l))
    return 0;

  banlist_del (list, e);

  return 1;
}

banlist_entry_t *banlist_find_net (banlist_t * list, uint32_t ip, uint32_t netmask)
{
  struct timeval now;
  banlist_entry_t *e, *l;

  l = dllist_bucket (&list->list, one_at_a_time (ip & netmask) & BANLIST_HASHMASK);
  dllist_foreach (l, e)
    if ((e->ip == (ip & netmask)) && (e->netmask == netmask))
    break;

  if (e == dllist_end (l))
    return 0;

  gettimeofday (&now, NULL);
  if (e->expire && (now.tv_sec > e->expire)) {
    banlist_del (list, e);
    e = NULL;
  }

  return e;
}

banlist_entry_t *banlist_find (banlist_t * list, uint32_t ip)
{
  struct timeval now;
  banlist_entry_t *e = NULL, *l = NULL;
  uint32_t netmask;
  long i;

  netmask = 0xFFFFFFFF;
  for (i = 32; i >= 0; --i, netmask = (netmask << 1) & 0xFFFFFFFE) {
    if (!list->netmask_inuse[i])
      continue;
    l = dllist_bucket (&list->list, one_at_a_time (ip & ntohl (netmask)) & BANLIST_HASHMASK);
    dllist_foreach (l, e)
      if (e->ip == (ip & e->netmask))
      break;
    if (e != dllist_end (l))
      break;

  }

  if (e == dllist_end (l))
    return 0;

  gettimeofday (&now, NULL);
  if (e->expire && (now.tv_sec > e->expire)) {
    banlist_del (list, e);
    e = NULL;
  }

  return e;
}

unsigned int banlist_cleanup (banlist_t * list)
{
  uint32_t i;
  banlist_entry_t *e, *n, *l;
  struct timeval now;

  dlhashlist_foreach (&list->list, i) {
    l = dllist_bucket (&list->list, i);
    for (e = (banlist_entry_t *) l->dllist.next; e != dllist_end (l); e = n) {
      n = (banlist_entry_t *) e->dllist.next;;
      if (e->expire && (e->expire > now.tv_sec))
	continue;

      ASSERT (list->netmask_inuse[netmask_to_numbits (e->netmask)]);
      list->netmask_inuse[netmask_to_numbits (e->netmask)]--;
      dllist_del ((dllist_entry_t *) e);
      bf_free (e->message);
      free (e);
    }
  }
  return 0;
}

unsigned int banlist_save (banlist_t * list, unsigned char *file)
{
  FILE *fp;
  uint32_t i;
  unsigned long l;
  banlist_entry_t *e, *lst;
  struct timeval now;

  banlist_cleanup (list);

  fp = fopen (file, "w+");
  if (!fp)
    return errno;

  gettimeofday (&now, NULL);
  fwrite (list->netmask_inuse, sizeof (list->netmask_inuse), 1, fp);
  dlhashlist_foreach (&list->list, i) {
    lst = dllist_bucket (&list->list, i);
    for (e = (banlist_entry_t *) lst->dllist.next; e != dllist_end (lst);
	 e = (banlist_entry_t *) e->dllist.next) {
      if (e->expire && (now.tv_sec > e->expire))
	continue;

      l = bf_used (e->message);
      fwrite (&e->ip, sizeof (e->ip), 1, fp);
      fwrite (&e->netmask, sizeof (e->netmask), 1, fp);
      fwrite (&e->expire, sizeof (e->expire), 1, fp);
      fwrite (&l, sizeof (l), 1, fp);
      fwrite (e->message->s, 1, l, fp);
    }
  }
  fclose (fp);
  return 0;
}

unsigned int banlist_load (banlist_t * list, unsigned char *file)
{
  FILE *fp;
  unsigned long l;
  banlist_entry_t e;

  fp = fopen (file, "r+");
  if (!fp)
    return errno;
  fread (list->netmask_inuse, sizeof (list->netmask_inuse), 1, fp);
  while (!feof (fp)) {
    if (!fread (&e.ip, sizeof (e.ip), 1, fp))
      break;
    fread (&e.netmask, sizeof (e.netmask), 1, fp);
    fread (&e.expire, sizeof (e.expire), 1, fp);
    fread (&l, sizeof (l), 1, fp);
    e.message = bf_alloc (l);
    fread (e.message->s, l, 1, fp);
    e.message->e += l;

    banlist_add (list, e.ip, e.netmask, e.message, e.expire);

    bf_free (e.message);
  }
  fclose (fp);
  return 0;
}

void banlist_init (banlist_t * list)
{
  memset (list, 0, sizeof (banlist_t));
  dlhashlist_init ((dllist_t *) & list->list, BANLIST_HASHSIZE);
}

void banlist_clear (banlist_t * list)
{
  uint32_t i;
  banlist_entry_t *e, *n, *lst;

  dlhashlist_foreach (&list->list, i) {
    lst = dllist_bucket (&list->list, i);
    for (e = (banlist_entry_t *) lst->dllist.next; e != dllist_end (lst); e = n) {
      n = (banlist_entry_t *) e->dllist.next;

      ASSERT (list->netmask_inuse[netmask_to_numbits (e->netmask)]);
      list->netmask_inuse[netmask_to_numbits (e->netmask)]--;
      dllist_del ((dllist_entry_t *) e);
      bf_free (e->message);
      free (e);
    }
  }
}
