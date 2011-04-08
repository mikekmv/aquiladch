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

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include "hash.h"
#include "cap.h"
#include "utils.h"

#include "../config.h"

#ifndef __USE_W32_SOCKETS
#  include <sys/socket.h>
#  ifdef HAVE_NETINET_IN_H
#    include <netinet/in.h>
#  endif
#  ifdef HAVE_ARPA_INET_H
#    include <arpa/inet.h>
#  endif
#endif /* __USE_W32_SOCKETS */

#ifdef USE_WINDOWS
#  include "sys_windows.h"
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
  elem->help = gettext (help);

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



int config_save (xml_node_t * base)
{
  xml_node_t *node;
  config_element_t *elem;

  node = xml_node_add (base, "Config");

  for (elem = config_sorted.onext; elem != &config_sorted; elem = elem->onext) {
    switch (elem->type) {
      case CFG_ELEM_STRING:
	xml_node_add_value (node, elem->name, elem->type, *elem->val.v_string);
	break;
      default:
	xml_node_add_value (node, elem->name, elem->type, elem->val.v_ptr);
    }
  }
  return 0;
}

int config_load (xml_node_t * node)
{
  config_element_t *elem;

  node = xml_node_find (node, "Config");
  if (!node)
    return 0;

  for (node = node->children; node; node = xml_next (node)) {
    elem = config_find (node->name);
    xml_node_get (node, elem->type, elem->val.v_ptr);
  }

  return 0;
}

int config_save_old (unsigned char *filename)
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
      case CFG_ELEM_MEMSIZE:
	fprintf (fp, "%s %lu\n", elem->name, *elem->val.v_ulong);
	break;
      case CFG_ELEM_BYTESIZE:
      case CFG_ELEM_ULONGLONG:
#ifndef USE_WINDOWS
	fprintf (fp, "%s %Lu\n", elem->name, *elem->val.v_ulonglong);
#else
	fprintf (fp, "%s %I64u\n", elem->name, *elem->val.v_ulonglong);
#endif
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
	{
	  unsigned char *out = string_escape (*elem->val.v_string);

	  fprintf (fp, "%s \"%s\"\n", elem->name, out ? out : (unsigned char *) "");
	  if (out)
	    free (out);
	}
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

int config_load_old (unsigned char *filename)
{
  unsigned int l;
  FILE *fp;
  config_element_t *elem;
  unsigned char *buffer, *c;

  fp = fopen (filename, "r");
  if (!fp)
    return errno;

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
      case CFG_ELEM_MEMSIZE:
	sscanf (c, "%lu", elem->val.v_ulong);
	break;
      case CFG_ELEM_BYTESIZE:
      case CFG_ELEM_ULONGLONG:
#ifndef USE_WINDOWS
	sscanf (c, "%Lu", elem->val.v_ulonglong);
#else
	sscanf (c, "%I64u", elem->val.v_ulonglong);
#endif
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
	*elem->val.v_string = string_unescape (c);
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
