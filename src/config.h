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

#ifndef _CONFIG_H_
#define _CONFIG_H_

#include "defaults.h"

typedef enum { CFG_ELEM_PTR,
  CFG_ELEM_LONG, CFG_ELEM_ULONG, CFG_ELEM_CAP, CFG_ELEM_ULONGLONG,
  CFG_ELEM_INT, CFG_ELEM_UINT,
  CFG_ELEM_DOUBLE,
  CFG_ELEM_STRING,
  CFG_ELEM_IP,
  CFG_ELEM_MEMSIZE,
  CFG_ELEM_BYTESIZE
} config_type_t;

typedef union config_value {
    void **v_ptr;
    long *v_long;
    unsigned long *v_ulong;
    unsigned long long *v_ulonglong;
    unsigned long *v_cap;
    unsigned long *v_ip;
    int *v_int;
    unsigned int *v_uint;
    double *v_double;
    unsigned char **v_string;
  } config_value_t;


typedef struct config_element {
  struct config_element *next, *prev;
  struct config_element *onext, *oprev;

  unsigned char name[CONFIG_NAMELENGTH];
  config_type_t type;
  config_value_t val;

  const unsigned char *help;
} config_element_t;


extern int config_init ();
extern config_element_t *config_register (unsigned char *name, config_type_t type, void *,
					  const unsigned char *help);
extern int config_unregister (unsigned char *name);
extern config_element_t *config_find (unsigned char *name);
extern void *config_retrieve (unsigned char *name);
extern int config_save (unsigned char *file);
extern int config_load (unsigned char *file);

#endif /* _CONFIG_H_ */
