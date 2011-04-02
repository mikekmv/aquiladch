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
#include <string.h>
#include <assert.h>
#include <strings.h>
#include <errno.h>

#include "../config.h"
#ifdef HAVE_CRYPT_H
#  include <crypt.h>
#endif

#include "user.h"

unsigned int type_ids = 0;
unsigned long account_ids = 0;

unsigned long account_count = 0;

account_type_t *accountTypes;
account_t *accounts;

account_type_t *account_type_add (unsigned char *name, unsigned long rights)
{
  account_type_t *t;

  t = malloc (sizeof (account_type_t));
  memset (t, 0, sizeof (account_type_t));
  strncpy (t->name, name, NICKLENGTH);
  t->rights = rights;
  t->id = type_ids++;

  t->next = accountTypes;
  if (t->next)
    t->next->prev = t;
  t->prev = NULL;
  accountTypes = t;

  t->refcnt = 0;

  return t;
}

account_type_t *account_type_find (unsigned char *name)
{
  account_type_t *r;

  for (r = accountTypes; r; r = r->next)
    if (!strncasecmp (r->name, name, NICKLENGTH))
      break;

  return r;
}

account_type_t *account_type_find_byid (unsigned long id)
{
  account_type_t *r;

  for (r = accountTypes; r; r = r->next)
    if (r->id == id)
      break;

  return r;
}

unsigned int account_type_del (account_type_t * t)
{
  if (t->next)
    t->next->prev = t->prev;
  if (t->prev) {
    t->prev->next = t->next;
  } else {
    accountTypes = t->next;
  }

  free (t);

  return 0;
}

account_t *account_add (account_type_t * type, unsigned char *nick)
{
  account_t *a;

  a = malloc (sizeof (account_t));
  memset (a, 0, sizeof (account_t));
  strncpy (a->nick, nick, NICKLENGTH);
  a->rights = 0;
  a->id = account_ids++;

  a->next = accounts;
  if (a->next)
    a->next->prev = a;
  a->prev = NULL;
  accounts = a;

  a->class = type->id;
  type->refcnt ++;
  a->classp = type;
 
  account_count++;

  return a;
}

int account_pwd_set (account_t *account, unsigned char *pwd) 
{
  unsigned char salt[3];

  salt [0] = ((random() % 46) + 45);
  salt [1] = ((random() % 46) + 45);
  salt [2] = 0;
  
  strcpy (account->passwd, crypt (pwd, salt));
  
  return 0;
}

int account_pwd_check (account_t *account, unsigned char *pwd) 
{
  return !strcmp (account->passwd, crypt (pwd, account->passwd));
}

account_t * account_find (unsigned char *nick)
{
  account_t *r;

  for (r = accounts; r; r = r->next)
    if (!strncasecmp (r->nick, nick, NICKLENGTH))
      break;

  return r;

}

unsigned int account_set_type (account_t *a, account_type_t *new) {
  account_type_t *old = a->classp;

  /* adjust group refcnts. */
  new->refcnt ++;
  old->refcnt --;
  
  /* store new data */
  a->class = new->id;
  a->classp = new;

  /* return old id. */
  return old->id;
}

unsigned int account_del (account_t * a)
{
  account_type_t *t;
  
  account_count--;
  
  t = account_type_find_byid (a->class);
  t->refcnt--;
  
  if (a->next)
    a->next->prev = a->prev;
  if (a->prev) {
    a->prev->next = a->next;
  } else {
    accounts = a->next;
  }
  
  free (a);

  return 0;
}

unsigned int accounts_load (const unsigned char *filename)
{
  FILE *fp;
  account_type_t *t, *ot;
  account_t *a, *oa;
  unsigned char buffer[1024];

  fp = fopen (filename, "r");
  if (!fp) return errno;

  fgets (buffer, 1024, fp);
  while (!feof (fp)) {
    switch (buffer[0]) {
      case 'T':
	t = malloc (sizeof (account_type_t));
	memset (t, 0, sizeof (account_type_t));
	sscanf (buffer, "T %s %lu %u", t->name, &t->rights, &t->id);

	if ((ot = account_type_find (t->name)))
		account_type_del (ot);

	t->next = accountTypes;
	if (t->next)
	  t->next->prev = t;
	t->prev = NULL;
	accountTypes = t;

	t->refcnt = 0;

	if (t->id >= type_ids)
	  type_ids = t->id + 1;

	break;
      case 'A':
	a = malloc (sizeof (account_t));
	memset (a, 0, sizeof (account_t));
	sscanf (buffer, "A %s %s %lu %u %lu", a->nick, a->passwd, &a->rights, &a->class, &a->id);

	if ((oa = account_find (a->nick)))
		account_del (oa);

	a->next = accounts;
	if (a->next)
	  a->next->prev = a;
	a->prev = NULL;
	accounts = a;

	if (a->passwd[0] == 1)
		a->passwd[0] = '\0';
	
	for (t = accountTypes; t; t = t->next)
	  if (t->id == a->class)
	    break;

	if (t)
	  t->refcnt++;

	ASSERT (t);	
	a->classp = t;
	if (a->id >= account_ids)
	  account_ids = t->id + 1;

	account_count++;

	break;
    }
    fgets (buffer, 1024, fp);
  }
  fclose (fp);
  return 0;
}

unsigned int accounts_save (const unsigned char *filename)
{
  FILE *fp;
  account_type_t *t;
  account_t *a;
  unsigned char nopasswd[2];

  nopasswd[0] = 1;
  nopasswd[1] = 0;
  fp = fopen (filename, "w+");
  if (!fp) return errno;

  for (t = accountTypes; t; t = t->next)
    fprintf (fp, "T %s %lu %d\n", t->name, t->rights, t->id);

  for (a = accounts; a; a = a->next)
    fprintf (fp, "A %s %s %lu %d %lu\n", a->nick, a->passwd[0] ? a->passwd : nopasswd, a->rights, a->class, a->id);

  fclose (fp);
  return 0;
}

unsigned int accounts_init ()
{

  accountTypes = NULL;
  accounts = NULL;

  return 0;
}
