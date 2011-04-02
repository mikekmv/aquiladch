/*
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)    
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.
 *
 */

#include <stdio.h>
#include <sys/time.h>
#include <string.h>

#include "banlist.h"
#include "buffer.h"

#define DPRINTF printf

typedef union {
	uint32_t ip;
	char     key[4];
} map_t;

__inline__ uint32_t one_at_a_time(uint32_t key)
{
  uint32_t   hash, i;
  map_t      m;

  m.ip = key;
  for (hash=0, i=0; i < 4; ++i)
  {
    hash += m.key[i];
    hash += (hash << 10);
    hash ^= (hash >> 6);
  }

  hash += (hash << 3);
  hash ^= (hash >> 11);
  hash += (hash << 15);
  return hash;
} 

banlist_entry_t * banlist_add (banlist_t* list, uint32_t ip, buffer_t *reason, unsigned long expire) {
	banlist_entry_t *b;

	/* delete any earlier bans of this ip. */
	banlist_del_byip (list, ip);
	
	/* alloc and clear new element */
	b = malloc (sizeof(banlist_entry_t));

	/* init */
	dllist_init (&b->dllist);
	b->ip = ip;
	b->message = reason;
	bf_claim (reason);
	b->expire = expire;

	/* put in hashlist */
	dlhashlist_prepend (list, one_at_a_time(ip) & BANLIST_HASHMASK, b);
	
	return b;
}

unsigned int banlist_del (banlist_t* list, banlist_entry_t *e) {
	if (!e) return 0;

	dllist_del ((dllist_entry_t *)e);
	if (e->message)
		bf_free (e->message);
	free (e);

	return 1;
}

unsigned int banlist_del_byip (banlist_t* list, uint32_t ip) {
	banlist_entry_t *e, *l;

	l = dllist_bucket (list, one_at_a_time(ip) & BANLIST_HASHMASK);
	dllist_foreach (l, e)
		if (e->ip == ip) break;

	if (e==dllist_end(l)) return 0;

	dllist_del ((dllist_entry_t *)e);
	return 1;
}

banlist_entry_t * banlist_find (banlist_t* list, uint32_t ip) {
	struct timeval now;
	banlist_entry_t *e, *l;

	l = dllist_bucket (list, one_at_a_time(ip) & BANLIST_HASHMASK);
	dllist_foreach (l, e)
		if (e->ip == ip) break;

	if (e==dllist_end(l)) return 0;
		
        gettimeofday (&now, NULL);
	if (e->expire && (now.tv_sec > e->expire)) {
		banlist_del (list, e);
		e = NULL;
	}

	return e;
}

unsigned int banlist_cleanup (banlist_t * list) {
	uint32_t i;
	banlist_entry_t *e, *n, *l;
	struct timeval now;
	
	dlhashlist_foreach (list, i) {
		l = dllist_bucket (list, i);
		for (e = (banlist_entry_t *)l->dllist.next; e != dllist_end(l); e = n) {
			n = (banlist_entry_t *)e->dllist.next;;
			if (e->expire && (e->expire > now.tv_sec)) continue;

			dllist_del ((dllist_entry_t *)e);
			bf_free (e->message);
			free (e);
		}
	}
	return 0;
}

unsigned int banlist_save (banlist_t* list, unsigned char *file) {
	FILE *fp;
	uint32_t i;
	unsigned long l;
        banlist_entry_t *e, *lst;
        struct timeval now;

	fp = fopen (file, "w+");
	if (!fp) return errno;
	
	gettimeofday (&now, NULL);
	dlhashlist_foreach (list, i) { 
		lst = dllist_bucket (list, i);
		for (e = (banlist_entry_t *)lst->dllist.next; e != dllist_end(lst); e = (banlist_entry_t *)e->dllist.next) {
			if (e->expire && (now.tv_sec > e->expire))
				continue;

			l = bf_used (e->message);
			fwrite (&e->ip,     sizeof (e->ip),     1, fp);
			fwrite (&e->expire, sizeof (e->expire), 1, fp);
			fwrite (&l, sizeof (l), 		1, fp);
			fwrite (e->message->s, 1, 		l, fp);
		}
	}
	fclose (fp);
	return 0;
}

unsigned int banlist_load (banlist_t* list, unsigned char *file) {
	FILE *fp;
	unsigned long l;
	banlist_entry_t e;

	fp = fopen (file, "r+");
	if (!fp) return errno;
	while (!feof(fp)) {
		if (!fread (&e.ip, 	  sizeof (e.ip),     1, fp))
			break;
		fread (&e.expire, sizeof (e.expire), 1, fp);
		fread (&l, 	  sizeof (l),        1, fp);
		e.message = bf_alloc (l);
		fread (e.message->s,      l, 		     1, fp);
		e.message->e += l;

		banlist_add (list, e.ip, e.message, e.expire);

		bf_free(e.message);
	}
	fclose (fp);
	return 0;
}

void banlist_init (banlist_t* list) {
	dlhashlist_init ((dllist_t *)list, BANLIST_HASHSIZE);
}

void banlist_clear (banlist_t* list) {
	uint32_t i;
        banlist_entry_t *e, *n, *lst;

	dlhashlist_foreach (list, i) {
                lst = dllist_bucket (list, i);
		for (e = (banlist_entry_t *)lst->dllist.next; e != dllist_end(lst); e = n) {
			n = (banlist_entry_t *)e->dllist.next;
			dllist_del ((dllist_entry_t *)e);
			bf_free (e->message);
			free (e);
		}
	}

}
