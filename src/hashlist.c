/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#include <ctype.h>
#include <strings.h>

#include "../config.h"
#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
#  include <stdint.h>
# endif
#endif

#include "proto.h"
#include "hash.h"
#include "hashlist.h"

void hash_init (hashlist_t * list)
{
  dlhashlist_init (&list->nick, SERVER_HASH_ENTRIES);
  dlhashlist_init (&list->ip, SERVER_HASH_ENTRIES);
  list->count = 0;
}

void hash_deluser (hashlist_t * list, hashlist_entry_t * entry)
{
  dllist_del (&entry->nick);
  dllist_del (&entry->ip);
  --list->count;

}

/* i choose append so it doesn't influence "to top" handling */
unsigned int hash_adduser (hashlist_t * list, user_t * u)
{
  uint32_t h, l;
  unsigned char nick[NICKLENGTH], *s, *d;
  hashlist_entry_t *entry = (hashlist_entry_t *) u;

  /* Use nick to link it in list */
  d = nick;
  s = u->nick;
  for (l = 0; *s && (l < NICKLENGTH); l++)
    *d++ = isalpha (*s) ? tolower (*s++) : *s++;

  h = SuperFastHash (nick, l);
  h &= SERVER_HASH_MASK;

  dllist_append (dllist_bucket (&list->nick, h), (dllist_entry_t *) & entry->nick);

  /* use ip to link it in ip list */
  h = SuperFastHash ((const unsigned char *) &u->ipaddress, 4);
  h &= SERVER_HASH_MASK;

  dllist_append (dllist_bucket (&list->ip, h), (dllist_entry_t *) & entry->ip);

  ++list->count;

  return 0;
}

user_t *hash_find_nick (hashlist_t * list, unsigned char *n, unsigned int len)
{
  user_t *u, *lst;
  uint32_t h, l;
  unsigned char nick[NICKLENGTH], *s, *d;

  if (len > NICKLENGTH)
    return NULL;

  /* lowercase nick for hashing */
  d = nick;
  s = n;
  for (l = 0; *s && (l < len); l++)
    *d++ = isalpha (*s) ? tolower (*s++) : *s++;

  /* hash it */
  h = SuperFastHash (nick, l);
  h &= SERVER_HASH_MASK;

  /* get nick */
  u = lst = dllist_bucket (&list->nick, h);
  dllist_foreach (lst, u)
    if (!(strncasecmp (u->nick, n, len) || u->nick[len]))
    break;

  if (lst == u)
    return NULL;

  return u;
}

user_t *hash_find_ip (hashlist_t * list, unsigned long ip)
{
  dllist_t *p;
  user_t *u, *lst;
  uint32_t h;

  h = SuperFastHash ((const unsigned char *) &ip, 4);
  h &= SERVER_HASH_MASK;

  lst = dllist_bucket (&list->ip, h);
  dllist_foreach (lst, p) {
    u = (user_t *) ((char *) p - sizeof (dllist_t));
    if (u->ipaddress == ip)
      return u;
  }

  return NULL;
}
