/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#include <string.h>

#include "proto.h"
#include "config.h"
#include "hub.h"
#include "nmdc_protocol.h"

int nmdc_init ()
{
  return nmdc_proto.init ();
}

int nmdc_setup (esocket_handler_t * h)
{
  config_element_t *listenport;
  config_element_t *extraports;

  nmdc_proto.setup ();

  /* setup main listen port */
  listenport = config_find ("NMDC.listenport");
  if (listenport)
    server_add_port (h, &nmdc_proto, *listenport->val.v_uint);

  /* setup extra nmdc ports */
  extraports = config_find ("NMDC.ExtraPorts");
  if (extraports && **extraports->val.v_string) {
    unsigned long port;
    char *work, *p;

    work = strdup (*extraports->val.v_string);

    p = work;
    while (*p && (port = strtol (p, &p, 0))) {
      server_add_port (h, &nmdc_proto, port);
      while ((*p == ' ') || (*p == ','))
	p++;
    }

    free (work);
  }

  return 0;
}
