/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include "hash.h"
#include "cap.h"

#include "../config.h"
#include <sys/socket.h>
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

config_element_t configlist[CONFIG_HASHSIZE];
config_element_t config_sorted;

config_element_t *config_register (unsigned char *name, config_type_t type, void *ptr,
				   const unsigned char *help)
{
  int i;
  unsigned long hash;
  config_element_t *elem;

  elem = malloc (sizeof (config_element_t));
  if (!elem)
    return NULL;

  for (i = 0; name[i] && i < CONFIG_NAMELENGTH; i++)
    elem->name[i] = tolower (name[i]);
  if (i == CONFIG_NAMELENGTH)
    i--;
  elem->name[i] = '\0';
  elem->type = type;
  elem->val.v_ptr = ptr;
  elem->help = help;

  hash = SuperFastHash (elem->name, strlen (elem->name)) & CONFIG_HASHMASK;
  elem->next = configlist[hash].next;
  elem->next->prev = elem;
  elem->prev = &configlist[hash];
  configlist[hash].next = elem;

  {
    config_element_t *e;

    for (e = config_sorted.onext; e != &config_sorted; e = e->onext)
      if (strcmp (elem->name, e->name) < 0)
	break;
    e = e->oprev;

    elem->onext = e->onext;
    elem->onext->oprev = elem;
    elem->oprev = e;
    e->onext = elem;
  }

  return elem;
}

int config_unregister (unsigned char *name)
{
  unsigned int i;
  config_element_t *elem;
  unsigned long hash;
  unsigned char n[CONFIG_NAMELENGTH];

  for (i = 0; name[i] && i < CONFIG_NAMELENGTH; i++)
    n[i] = tolower (name[i]);
  if (i == CONFIG_NAMELENGTH)
    i--;
  n[i] = '\0';

  hash = SuperFastHash (n, strlen (n)) & CONFIG_HASHMASK;

  for (elem = configlist[hash].next; elem != &configlist[hash]; elem = elem->next)
    if (!strcmp (n, elem->name))
      break;

  if (elem == &configlist[hash])
    return -1;

  elem->next->prev = elem->prev;
  elem->prev->next = elem->next;

  free (elem);

  return 0;
}

config_element_t *config_find (unsigned char *name)
{
  unsigned int i;
  config_element_t *elem;
  unsigned long hash;
  unsigned char n[CONFIG_NAMELENGTH];

  for (i = 0; name[i] && i < CONFIG_NAMELENGTH; i++)
    n[i] = tolower (name[i]);
  if (i == CONFIG_NAMELENGTH)
    i--;
  n[i] = '\0';

  hash = SuperFastHash (n, strlen (n)) & CONFIG_HASHMASK;

  for (elem = configlist[hash].next; elem != &configlist[hash]; elem = elem->next)
    if (!strcmp (n, elem->name))
      return elem;

  return NULL;
}

void *config_retrieve (unsigned char *name)
{
  unsigned int i;
  config_element_t *elem;
  unsigned long hash;
  unsigned char n[CONFIG_NAMELENGTH];

  for (i = 0; name[i] && i < CONFIG_NAMELENGTH; i++)
    n[i] = tolower (name[i]);
  if (i == CONFIG_NAMELENGTH)
    i--;
  n[i] = '\0';

  hash = SuperFastHash (n, strlen (n)) & CONFIG_HASHMASK;

  for (elem = configlist[hash].next; elem != &configlist[hash]; elem = elem->next)
    if (!strcmp (n, elem->name))
      return elem->val.v_ptr;

  return NULL;
}

int config_save (unsigned char *filename)
{
  FILE *fp;
  config_element_t *elem;

  fp = fopen (filename, "w+");
  if (!fp)
    return errno;

  for (elem = config_sorted.onext; elem != &config_sorted; elem = elem->onext) {
    switch (elem->type) {
      case CFG_ELEM_PTR:
	fprintf (fp, "%s %p\n", elem->name, *elem->val.v_ptr);
	break;
      case CFG_ELEM_LONG:
	fprintf (fp, "%s %ld\n", elem->name, *elem->val.v_long);
	break;
      case CFG_ELEM_ULONG:
      case CFG_ELEM_CAP:
	fprintf (fp, "%s %lu\n", elem->name, *elem->val.v_ulong);
	break;
      case CFG_ELEM_ULONGLONG:
	fprintf (fp, "%s %Lu\n", elem->name, *elem->val.v_ulonglong);
	break;
      case CFG_ELEM_INT:
	fprintf (fp, "%s %d\n", elem->name, *elem->val.v_int);
	break;
      case CFG_ELEM_UINT:
	fprintf (fp, "%s %u\n", elem->name, *elem->val.v_int);
	break;
      case CFG_ELEM_DOUBLE:
	fprintf (fp, "%s %lf\n", elem->name, *elem->val.v_double);
	break;
      case CFG_ELEM_STRING:
	fprintf (fp, "%s \"%s\"\n", elem->name,
		 *elem->val.v_string ? *elem->val.v_string : (unsigned char *) "");
	break;
      case CFG_ELEM_IP:
	{
#ifdef HAVE_INET_NTOA
	  struct in_addr ia;

	  ia.s_addr = *elem->val.v_ip;
	  fprintf (fp, "%s %s\n", elem->name, inet_ntoa (ia));
#else
#warning "inet_ntoa not support. Support for CFG_ELEM_IP disabled."
#endif
	  break;
	}
      default:
	fprintf (fp, "%s !Unknown Type!\n", elem->name);
    }
  }
  fclose (fp);

  return 0;
}

int config_load (unsigned char *filename)
{
  unsigned int l;
  FILE *fp;
  config_element_t *elem;
  unsigned char *buffer, *c;

  fp = fopen (filename, "r");
  if (!fp)
    return -1;

  buffer = malloc (4096);
  while (!feof (fp)) {
    fgets (buffer, 4096, fp);
    for (c = buffer; *c && *c != ' '; c++);
    if (!*c)
      continue;
    *c++ = '\0';
    elem = config_find (buffer);
    if (!elem)
      continue;
    switch (elem->type) {
      case CFG_ELEM_PTR:
	sscanf (c, "%p", elem->val.v_ptr);
	break;
      case CFG_ELEM_LONG:
	sscanf (c, "%ld", elem->val.v_long);
	break;
      case CFG_ELEM_ULONG:
      case CFG_ELEM_CAP:
	sscanf (c, "%lu", elem->val.v_ulong);
	break;
      case CFG_ELEM_ULONGLONG:
	sscanf (c, "%Lu", elem->val.v_ulonglong);
	break;
      case CFG_ELEM_INT:
	sscanf (c, "%d", elem->val.v_int);
	break;
      case CFG_ELEM_UINT:
	sscanf (c, "%u", elem->val.v_uint);
	break;
      case CFG_ELEM_DOUBLE:
	sscanf (c, "%lf", elem->val.v_double);
	break;
      case CFG_ELEM_STRING:
	if (*elem->val.v_string)
	  free (*elem->val.v_string);
	l = strlen (c);
	if (c[l - 1] == '\n')
	  c[l-- - 1] = '\0';
	if ((*c == '"') && (c[l - 1] == '"')) {
	  c[l - 1] = '\0';
	  c++;
	};
	*elem->val.v_string = strdup (c);
	break;
      case CFG_ELEM_IP:
	{
#ifdef HAVE_INET_NTOA
	  struct in_addr ia;

	  if (inet_aton (c, &ia))
	    *elem->val.v_ip = ia.s_addr;
#else
#warning "inet_ntoa not support. Support for CFG_ELEM_IP disabled."
#endif
	  break;
	}
    }
  };

  free (buffer);
  fclose (fp);

  return 0;

}

int config_init ()
{
  int i;

  memset (&configlist, 0, sizeof (configlist));

  for (i = 0; i < CONFIG_HASHSIZE; i++) {
    configlist[i].next = &configlist[i];
    configlist[i].prev = &configlist[i];
  }

  config_sorted.onext = &config_sorted;
  config_sorted.oprev = &config_sorted;

  return 0;
}
