/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#ifndef _BANLISTCLIENT_H_
#define _BANLISTCLIENT_H_

#include "../config.h"
#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
#  include <stdint.h>
# endif
#endif

#include "config.h"
#include "buffer.h"
#include "dllist.h"

#define BANLIST_NICK_HASHBITS   10
#define BANLIST_NICK_HASHSIZE	(1 << BANLIST_NICK_HASHBITS)
#define BANLIST_NICK_HASHMASK	(BANLIST_NICK_HASHSIZE-1)

typedef struct banlist_client {
  dllist_entry_t dllist;

  unsigned char client[NICKLENGTH];
  double minVersion;
  double maxVersion;
  buffer_t *message;


} banlist_client_entry_t;

typedef dllist_t banlist_client_t;

extern banlist_client_entry_t *banlist_client_add (banlist_client_t * list, unsigned char *client,
						   double minVersion, double maxVersion,
						   buffer_t * reason);

extern unsigned int banlist_client_del (banlist_client_t * list, banlist_client_entry_t *);
extern unsigned int banlist_client_del_byclient (banlist_client_t * list, unsigned char *client);

extern banlist_client_entry_t *banlist_client_find (banlist_client_t * list, unsigned char *client,
						    double version);

extern unsigned int banlist_client_cleanup (banlist_client_t * list);
extern void banlist_client_clear (banlist_client_t * list);

extern unsigned int banlist_client_save (banlist_client_t * list, unsigned char *file);
extern unsigned int banlist_client_load (banlist_client_t * list, unsigned char *file);

extern void banlist_client_init (banlist_client_t * list);

#endif /* _BANLISTCLIENT_H_ */
