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

#include "banlistclient.h"
#include "buffer.h"
#include "hash.h"

banlist_client_entry_t *banlist_client_add (banlist_client_t * list, unsigned char *client,
					    double minVersion, double maxVersion, buffer_t * reason)
{
  banlist_client_entry_t *b;

  /* delete any prior ban of this client */
  banlist_client_del_byclient (list, client);

  /* alloc and clear new element */
  b = malloc (sizeof (banlist_client_entry_t));

  /* init */
  dllist_init (&b->dllist);
  strncpy (b->client, client, NICKLENGTH);
  b->minVersion = minVersion;
  b->maxVersion = maxVersion;
  b->message = reason;
  bf_claim (reason);

  /* put in hashlist */
  dlhashlist_prepend (list, SuperFastHash (client, strlen (client)) & BANLIST_NICK_HASHMASK, b);

  return b;
}



unsigned int banlist_client_del (banlist_client_t * list, banlist_client_entry_t * e)
{
  if (!e)
    return 0;

  dllist_del ((dllist_entry_t *) e);
  if (e->message)
    bf_free (e->message);
  free (e);
  return 1;
}

unsigned int banlist_client_del_byclient (banlist_client_t * list, unsigned char *client)
{
  banlist_client_entry_t *e, *l;

  l = dllist_bucket (list, SuperFastHash (client, strlen (client)) & BANLIST_NICK_HASHMASK);
  dllist_foreach (l, e)
    if (!strncmp (e->client, client, NICKLENGTH))
    break;

  if (e == dllist_end (l))
    return 0;

  dllist_del ((dllist_entry_t *) e);
  return 1;
}

banlist_client_entry_t *banlist_client_find (banlist_client_t * list, unsigned char *client,
					     double version)
{
  banlist_client_entry_t *e, *l;

  l = dllist_bucket (list, SuperFastHash (client, strlen (client)) & BANLIST_NICK_HASHMASK);
  dllist_foreach (l, e)
    if (!strncmp (e->client, client, NICKLENGTH))
    break;

  if (e == dllist_end (l))
    return 0;

  if (version == 0)
    return e;

  if ((version >= e->minVersion) && (version <= e->maxVersion))
    return 0;

  return e;
}

unsigned int banlist_client_cleanup (banlist_client_t * list)
{
  uint32_t i;
  banlist_client_entry_t *e, *n, *l;
  struct timeval now;

  gettimeofday (&now, NULL);
  dlhashlist_foreach (list, i) {
    l = dllist_bucket (list, i);
    for (e = (banlist_client_entry_t *) l->dllist.next; e != l; e = n) {
      n = (banlist_client_entry_t *) e->dllist.next;
      dllist_del ((dllist_entry_t *) e);
      bf_free (e->message);
      free (e);
    }
  }
  return 0;
}

unsigned int banlist_client_save (banlist_client_t * list, unsigned char *file)
{
  FILE *fp;
  uint32_t i;
  unsigned long l;
  banlist_client_entry_t *e, *lst;
  struct timeval now;

  fp = fopen (file, "w+");
  if (!fp)
    return errno;

  gettimeofday (&now, NULL);
  dlhashlist_foreach (list, i) {
    lst = dllist_bucket (list, i);
    dllist_foreach (lst, e) {
      l = bf_used (e->message);
      fwrite (e->client, sizeof (e->client), 1, fp);
      fwrite (&e->minVersion, sizeof (e->minVersion), 1, fp);
      fwrite (&e->maxVersion, sizeof (e->maxVersion), 1, fp);
      fwrite (&l, sizeof (l), 1, fp);
      fwrite (e->message->s, 1, l, fp);
    }
  }
  fclose (fp);
  return 0;
}

unsigned int banlist_client_load (banlist_client_t * list, unsigned char *file)
{
  FILE *fp;
  unsigned int t;
  unsigned long l;
  banlist_client_entry_t e;

  t = 0;
  fp = fopen (file, "r+");
  if (!fp)
    return 0;
  while (!feof (fp)) {
    if (!fread (e.client, sizeof (e.client), 1, fp))
      break;
    fread (&e.minVersion, sizeof (e.minVersion), 1, fp);
    fread (&e.maxVersion, sizeof (e.maxVersion), 1, fp);
    fread (&l, sizeof (l), 1, fp);
    e.message = bf_alloc (l + 1);
    fread (e.message->s, l, 1, fp);
    e.message->e += l;

    banlist_client_add (list, e.client, e.minVersion, e.maxVersion, e.message);

    bf_free (e.message);

    t++;
  }
  fclose (fp);
  return t;
}

void banlist_client_init (banlist_client_t * list)
{
  dlhashlist_init ((dllist_t *) list, BANLIST_NICK_HASHSIZE);
}

void banlist_client_clear (banlist_client_t * list)
{
  uint32_t i;
  banlist_client_entry_t *e, *n, *lst;

  dlhashlist_foreach (list, i) {
    lst = dllist_bucket (list, i);
    for (e = (banlist_client_entry_t *) lst->dllist.next; e != lst; e = n) {
      n = (banlist_client_entry_t *) e->dllist.next;
      dllist_del ((dllist_entry_t *) e);
      bf_free (e->message);
      free (e);
    }
  }

}
