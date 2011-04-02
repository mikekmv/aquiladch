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

#ifndef _BANLIST_H_
#define _BANLIST_H_

#include "../config.h"
#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
#  include <stdint.h>
# endif
#endif

#include "buffer.h"
#include "dllist.h"

#define BANLIST_HASHBITS	11
#define BANLIST_HASHSIZE	(1 << BANLIST_HASHBITS)
#define BANLIST_HASHMASK	(BANLIST_HASHSIZE-1)

typedef struct banlist {
  dllist_entry_t dllist;

  uint32_t ip;
  buffer_t *message;
  time_t expire;
} banlist_entry_t;

typedef dllist_t banlist_t;

extern banlist_entry_t *banlist_add (banlist_t * list, uint32_t ip, buffer_t * reason,
				     unsigned long expire);

extern unsigned int banlist_del (banlist_t * list, banlist_entry_t *);
extern unsigned int banlist_del_byip (banlist_t * list, uint32_t ip);

extern banlist_entry_t *banlist_find (banlist_t * list, uint32_t ip);

extern unsigned int banlist_cleanup (banlist_t * list);

extern unsigned int banlist_save (banlist_t * list, unsigned char *file);
extern unsigned int banlist_load (banlist_t * list, unsigned char *file);

extern void banlist_init (banlist_t * list);

#endif /* _BANLIST_H_ */
