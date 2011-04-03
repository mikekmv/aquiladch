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
extern int command_setrights (unsigned char *name, unsigned long cap, unsigned long ncap);

extern unsigned int command_flags_print (command_flag_t * flags, buffer_t * buf,
					 unsigned long flag);
extern unsigned int command_flags_help (command_flag_t * flags, buffer_t * buf);
extern unsigned int command_flags_parse (command_flag_t * flags, buffer_t * buf, unsigned int argc,
					 unsigned char **argv, unsigned int flagstart,
					 unsigned long *flag, unsigned long *nflag);

#endif
