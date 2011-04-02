/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#ifndef _USER_H_
#define _USER_H_

#include "config.h"


typedef struct account_type {
  struct account_type *next, *prev;

  unsigned char name[NICKLENGTH];
  unsigned long rights;
  unsigned int id;

  unsigned long refcnt;
} account_type_t;

typedef struct account {
  struct account *next, *prev;

  unsigned char nick[NICKLENGTH];
  unsigned char passwd[NICKLENGTH];
  unsigned long rights;
  unsigned int class;
  unsigned long id;

  unsigned int refcnt;		/* usually 1 or 0 : user is logged in or not */

  account_type_t *classp;
} account_t;

extern account_type_t *account_type_add (unsigned char *name, unsigned long rights);
extern account_type_t *account_type_find (unsigned char *name);
extern unsigned int account_type_del (account_type_t *);
extern account_type_t *account_type_find_byid (unsigned long id);

extern account_t *account_add (account_type_t * type, unsigned char *nick);
extern account_t *account_find (unsigned char *nick);
extern unsigned int account_set_type (account_t * a, account_type_t * new);
extern unsigned int account_del (account_t * a);

extern int account_pwd_set (account_t * account, unsigned char *pwd);
extern int account_pwd_check (account_t * account, unsigned char *pwd);

extern unsigned int accounts_load (const unsigned char *filename);
extern unsigned int accounts_save (const unsigned char *filename);

extern unsigned int accounts_init ();

#endif
