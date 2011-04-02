/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#ifndef _COMMANDS_H_
#define _COMMANDS_H_

#include "plugin.h"
#include "cap.h"

#define COMMAND_MAX_LENGTH	 64
#define COMMAND_MAX_ARGS	256

#define COMMAND_HASHTABLE	256
#define COMMAND_HASHMASK	(COMMAND_HASHTABLE-1)

typedef unsigned long (command_handler_t) (plugin_user_t *, buffer_t *, void *, unsigned int,
					   unsigned char **);

typedef struct command_flag {
  unsigned char *name;
  unsigned long flag;
  unsigned char *help;
} command_flag_t;

typedef struct command {
  struct command *next, *prev;
  struct command *onext, *oprev;

  unsigned char name[COMMAND_MAX_LENGTH];
  command_handler_t *handler;
  unsigned long req_cap;
  unsigned char *help;
} command_t;

extern int command_init ();
extern int command_setup ();
extern int command_register (unsigned char *name, command_handler_t * handler, unsigned long cap,
			     unsigned char *help);
extern int command_unregister (unsigned char *name);

extern unsigned int command_flags_print (command_flag_t * flags, buffer_t * buf,
					 unsigned long flag);
extern unsigned int command_flags_help (command_flag_t * flags, buffer_t * buf);
extern unsigned int command_flags_parse (command_flag_t * flags, buffer_t * buf, unsigned int argc,
					 unsigned char **argv, unsigned int flagstart,
					 unsigned long *flag, unsigned long *nflag);

#endif
