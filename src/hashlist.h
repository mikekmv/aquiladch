/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#ifndef _HASHLIST_H_
#define _HASHLIST_H_

#include "dllist.h"

#define SERVER_HASH_BITS	12
#define SERVER_HASH_ENTRIES     (1<<SERVER_HASH_BITS)
#define SERVER_HASH_MASK	(SERVER_HASH_ENTRIES-1)

typedef struct hashlist_entry {
	dllist_entry_t nick;
	dllist_entry_t ip;
} hashlist_entry_t;

typedef struct hashlist {
  dllist_t nick;
  dllist_t ip;
} hashlist_t;

#endif /* _HASHLIST_H_ */
