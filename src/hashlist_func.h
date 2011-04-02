/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#ifndef _HASHLIST_FUNC_H_
#define _HASHLIST_FUNC_H_

#include "hashlist.h"

void hash_init (hashlist_t * list);

unsigned int hash_adduser (hashlist_t * list, user_t *);
void hash_deluser (hashlist_t * list, hashlist_entry_t *);

user_t *hash_find_nick (hashlist_t * list, unsigned char *n, unsigned int len);
user_t *hash_find_ip (hashlist_t * list, unsigned long ip);

#endif /* _HASHLIST_FUNC_H_ */
