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
#include <ctype.h>

#include "banlistnick.h"
#include "buffer.h"
#include "hash.h"

banlist_nick_entry_t *banlist_nick_add (banlist_nick_t * list, unsigned char *nick,
					buffer_t * reason, unsigned long expire)
{
  banlist_nick_entry_t *b;
  unsigned char n[NICKLENGTH], *s, *d;
  unsigned int l;

  d = n;
  s = nick;
  for (l = 0; *s && (l < NICKLENGTH); l++)
    *d++ = isalpha (*s) ? tolower (*s++) : *s++;

  /* delete any earlier bans of this nick */
  banlist_nick_del_bynick (list, nick);

  /* alloc and clear new element */
  b = malloc (sizeof (banlist_nick_entry_t));

  /* init */
  dllist_init (&b->dllist);
  strncpy (b->nick, nick, NICKLENGTH);
  b->nick[NICKLENGTH - 1] = 0;
  b->message = reason;
  bf_claim (reason);
  b->expire = expire;

  /* put in hashlist */
  dlhashlist_prepend (list, SuperFastHash (n, l) & BANLIST_NICK_HASHMASK, b);

  return b;
}



unsigned int banlist_nick_del (banlist_nick_t * list, banlist_nick_entry_t * e)
{
  if (!e)
    return 0;

  dllist_del ((dllist_entry_t *) e);
  if (e->message)
    bf_free (e->message);
  free (e);
  return 1;
}

unsigned int banlist_nick_del_bynick (banlist_nick_t * list, unsigned char *nick)
{
  banlist_nick_entry_t *e, *l;
  unsigned char n[NICKLENGTH], *s, *d;
  unsigned int i;

  d = n;
  s = nick;
  for (i = 0; *s && (i < NICKLENGTH); i++)
    *d++ = isalpha (*s) ? tolower (*s++) : *s++;

  l = dllist_bucket (list, SuperFastHash (n, i) & BANLIST_NICK_HASHMASK);
  dllist_foreach (l, e)
    if (!strncasecmp (e->nick, nick, NICKLENGTH))
    break;

  if (e == dllist_end (l))
    return 0;

  dllist_del ((dllist_entry_t *) e);
  return 1;
}

banlist_nick_entry_t *banlist_nick_find (banlist_nick_t * list, unsigned char *nick)
{
  struct timeval now;
  banlist_nick_entry_t *e, *l;
  unsigned char n[NICKLENGTH], *s, *d;
  unsigned int i;

  d = n;
  s = nick;
  for (i = 0; *s && (i < NICKLENGTH); i++)
    *d++ = isalpha (*s) ? tolower (*s++) : *s++;

  l = dllist_bucket (list, SuperFastHash (n, i) & BANLIST_NICK_HASHMASK);
  dllist_foreach (l, e)
    if (!strncasecmp (e->nick, nick, NICKLENGTH))
    break;

  if (e == dllist_end (l))
    return 0;

  gettimeofday (&now, NULL);
  if (e->expire && (now.tv_sec > e->expire)) {
    banlist_nick_del (list, e);
    e = NULL;
  }

  return e;
}

unsigned int banlist_nick_cleanup (banlist_nick_t * list)
{
  uint32_t i;
  banlist_nick_entry_t *e, *n, *l;
  struct timeval now;

  gettimeofday (&now, NULL);
  dlhashlist_foreach (list, i) {
    l = dllist_bucket (list, i);
    for (e = (banlist_nick_entry_t *) l->dllist.next; e != l; e = n) {
      n = (banlist_nick_entry_t *) e->dllist.next;
      if (e->expire && (e->expire > now.tv_sec))
	continue;

      dllist_del ((dllist_entry_t *) e);
      bf_free (e->message);
      free (e);
    }
  }
  return 0;
}

unsigned int banlist_nick_save (banlist_nick_t * list, unsigned char *file)
{
  FILE *fp;
  uint32_t i;
  unsigned long l;
  banlist_nick_entry_t *e, *lst;
  struct timeval now;

  fp = fopen (file, "w+");
  if (!fp)
    return errno;

  gettimeofday (&now, NULL);
  dlhashlist_foreach (list, i) {
    lst = dllist_bucket (list, i);
    for (e = (banlist_nick_entry_t *) lst->dllist.next; e != lst;
	 e = (banlist_nick_entry_t *) e->dllist.next) {
      if (e->expire && (now.tv_sec > e->expire))
	continue;

      l = bf_used (e->message);
      fwrite (e->nick, sizeof (e->nick), 1, fp);
      fwrite (&e->expire, sizeof (e->expire), 1, fp);
      fwrite (&l, sizeof (l), 1, fp);
      fwrite (e->message->s, 1, l, fp);
    }
  }
  fclose (fp);
  return 0;
}

unsigned int banlist_nick_load (banlist_nick_t * list, unsigned char *file)
{
  FILE *fp;
  unsigned int t;
  unsigned long l;
  banlist_nick_entry_t e;

  t = 0;
  fp = fopen (file, "r+");
  if (!fp)
    return 0;
  while (!feof (fp)) {
    if (!fread (e.nick, sizeof (e.nick), 1, fp))
      break;
    fread (&e.expire, sizeof (e.expire), 1, fp);
    fread (&l, sizeof (l), 1, fp);
    e.message = bf_alloc (l);
    fread (e.message->s, l, 1, fp);
    e.message->e += l;

    banlist_nick_add (list, e.nick, e.message, e.expire);

    bf_free (e.message);

    t++;
  }
  fclose (fp);
  return t;
}

void banlist_nick_init (banlist_nick_t * list)
{
  dlhashlist_init ((dllist_t *) list, BANLIST_NICK_HASHSIZE);
}

void banlist_nick_clear (banlist_nick_t * list)
{
  uint32_t i;
  banlist_nick_entry_t *e, *n, *lst;

  dlhashlist_foreach (list, i) {
    lst = dllist_bucket (list, i);
    for (e = (banlist_nick_entry_t *) lst->dllist.next; e != lst; e = n) {
      n = (banlist_nick_entry_t *) e->dllist.next;
      dllist_del ((dllist_entry_t *) e);
      bf_free (e->message);
      free (e);
    }
  }

}
