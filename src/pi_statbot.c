/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "plugin.h"
#include "user.h"
#include "commands.h"
#include "hub.h"
#include "proto.h"

/*********************************************************************************************/

extern unsigned long users_peak, users_alltimepeak, account_count;
extern struct timeval boottime;

plugin_t *plugin_statbot = NULL;

/*********************************************************************************************/

extern float cpu_calculate (int time);

unsigned long pi_statbot_handler_statbot (plugin_user_t * user, buffer_t * output, void *dummy,
					  unsigned int argc, unsigned char **argv)
{
  struct timeval now;
  config_element_t *userlimit, *minshare, *minslots, *maxslots, *maxhubs, *hubname, *listenport,
    *hostname, *hubdesc;

  hubname = config_find ("hubname");
  listenport = config_find ("NMDC.listenport");
  hostname = config_find ("hubaddress");
  hubdesc = config_find ("hubdescription");
  userlimit = config_find ("userlimit.unregistered");
  minshare = config_find ("sharemin.unregistered");
  minslots = config_find ("slot.unregistered.min");
  maxslots = config_find ("slot.unregistered.max");
  maxhubs = config_find ("hub.unregistered.max");

  gettimeofday (&now, NULL);
  bf_printf (output,
	     "HubInfo %s$%s$%s$%s$%u$%lu$%llu$%llu$%u$%u$%lu$%lu$%lu$%lu$%lu$%lu$%lu$%.2f$%.2f$%.2f$%lu$%lu$%lu$",
	     hubname ? *(hubname->val.v_string) : (unsigned char *) "", "" /* config.HubTopic */ ,
	     hubdesc ? *(hubdesc->val.v_string) : (unsigned char *) "", hostname ? *(hostname->val.v_string) : (unsigned char *) "", listenport ? (*listenport->val.v_uint) : 411, userlimit ? *(userlimit->val.v_ulong) : 0L, minshare ? *(minshare->val.v_ulonglong) : 0LL, 0LL,	/* max share */
	     minslots ? *(minslots->val.v_uint) : 0,
	     maxslots ? *(maxslots->val.v_uint) : 0,
	     maxhubs ? *(maxhubs->val.v_ulong) : 0L, account_count /* regged users */ ,
	     0L /* permbans */ ,
	     0L /* timebans */ ,
	     now.tv_sec - boottime.tv_sec /* uptime */ ,
	     users_peak, users_alltimepeak, cpu_calculate (60) /* cpu usage */ ,
	     0.0 /* cpu time */ ,
	     0.0 /* cpu available */ ,
	     0L /* memory usage */ ,
	     0L /* peak memory usage */ ,
	     0L			/* memory available */
    );

  return 0;
}

/********************************* INIT *************************************/

int pi_statbot_init ()
{
  plugin_statbot = plugin_register ("statbot");

  command_register ("statbot", &pi_statbot_handler_statbot, 0,
		    "Command returns info for the statbot.");

  return 0;
}