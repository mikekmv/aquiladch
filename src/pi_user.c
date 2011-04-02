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
#include <sys/time.h>
#include <sys/resource.h>

#include "plugin.h"
#include "user.h"
#include "commands.h"
#include "hub.h"
#include "banlistclient.h"

#define PI_USER_CLIENTBANFILE "clientbanlist.conf"

typedef struct slotratio {
  unsigned int minslot;
  unsigned int maxslot;
  unsigned int minhub;
  unsigned int maxhub;
  double ratio;
} slotratio_t;


banlist_client_t clientbanlist;

unsigned char *ClientBanFileName;

plugin_t *plugin_user = NULL;

unsigned long users_total = 0;
unsigned long users_peak = 0;
unsigned long users_alltimepeak = 0;
unsigned long users_dropped = 0;

unsigned long user_unregistered_max = 0;
unsigned long user_registered_max = 0;
unsigned long user_op_max = 0;

unsigned long user_unregistered_current = 0;
unsigned long user_registered_current = 0;
unsigned long user_op_current = 0;

unsigned long long sharereq_unregistered_share = 0;
unsigned long long sharereq_registered_share = 0;
unsigned long long sharereq_op_share = 0;

slotratio_t slotratios[3];

unsigned long pi_user_handler_userstat (plugin_user_t * user, buffer_t * output, void *dummy,
					unsigned int argc, unsigned char **argv)
{
  bf_printf (output, "Unregistered users: %lu / %lu\n"
	     "Registered users:   %lu / %lu\n"
	     "Operators           %lu / %lu\n"
	     " Total      %lu / %lu\n"
	     " Peak             %lu\n"
	     " Dropped          %lu\n",
	     user_unregistered_current, user_unregistered_max,
	     user_registered_current, user_registered_max,
	     user_op_current, user_op_max,
	     users_total, user_unregistered_max + user_registered_max, users_peak, users_dropped);

  return 0;
}

unsigned long pi_user_check_user (plugin_user_t * user)
{
  unsigned int class = 0;

  /* registered? op? */
  if (user->flags & PROTO_FLAG_REGISTERED) {
    class = 1;
    if (user->op)
      class = 2;
  }

  DPRINTF (" User %s class %d, hubs %d, slots %d\n", user->nick, class, user->hubs[0], user->slots);
  /* check slot and hub requirements */
  if (!(user->rights & CAP_TAG)) {
    if (user->hubs[0] > slotratios[class].maxhub) {
      plugin_user_printf (user, "Sorry, your class has a maximum of %u hubs.\n",
			  slotratios[class].maxhub);
      goto drop;
    }
    if (user->hubs[0] < slotratios[class].minhub) {
      plugin_user_printf (user, "Sorry, your class has a minimum of %u hubs.\n",
			  slotratios[class].minhub);
      goto drop;
    }
    if (user->slots > slotratios[class].maxslot) {
      plugin_user_printf (user, "Sorry, your class has a maximum of %u slots.\n",
			  slotratios[class].maxslot);
      goto drop;
    }
    if (user->slots < slotratios[class].minslot) {
      plugin_user_printf (user, "Sorry, your class has a minimum of %u slots.\n",
			  slotratios[class].minslot);
      goto drop;
    }
    if (user->hubs[0] > (slotratios[class].ratio * user->slots)) {
      plugin_user_printf (user, "Sorry, your class has a maximum hub/slot ratio of %f.\n",
			  slotratios[class].ratio);
      goto drop;
    }
  }

  /* check share sizes */
  if (!(user->rights & CAP_SHARE)) {
    if (user->flags & PROTO_FLAG_REGISTERED) {
      if (user->op) {
	if (user->share < sharereq_op_share) {
	  plugin_user_printf (user, "Sorry, your class has a minimum share of %lu.\n",
			      sharereq_op_share);
	  goto drop;
	}
      } else {
	if (user->share < sharereq_registered_share) {
	  plugin_user_printf (user, "Sorry, your class has a minimum share of %lu.\n",
			      sharereq_registered_share);
	  goto drop;
	}
      }
    } else {
      if (user->share < sharereq_unregistered_share) {
	plugin_user_printf (user, "Sorry, your class has a minimum share of %lu.\n",
			    sharereq_unregistered_share);
	goto drop;
      }
    }
  }
  return 1;
drop:
  return 0;
}

unsigned long pi_user_event_prelogin (plugin_user_t * user, void *dummy, unsigned long event,
				      buffer_t * token)
{
  banlist_client_entry_t *cl;

  /* check if client is accepted */
  if ((cl = banlist_client_find (&clientbanlist, user->client, user->version))) {
    plugin_user_printf (user, "Sorry, this client is not accepted because: %*s\n",
			bf_used (cl->message), cl->message->s);
    goto drop;
  }

  if (!pi_user_check_user (user))
    goto drop;

  /* if this is a registered user, check for registered/op max */
  if (user->flags & PROTO_FLAG_REGISTERED) {
    if (user->op) {
      if (user_registered_current < user_registered_max) {
	user_registered_current++;
	user_op_current++;
	goto accept;
      }
    } else {
      if (user_registered_current < (user_registered_max - user_op_max + user_op_current)) {
	user_registered_current++;
	goto accept;

      }
    }
  } else {
    if (user_unregistered_current < user_unregistered_max) {
      user_unregistered_current++;
      goto accept;
    }
  }

  plugin_user_printf (user,
		      "Sorry, the hub is full. It cannot accept more users from your class.\n");

drop:
  /* owners always get in. */
  if (user->rights & CAP_OWNER)
    goto accept;

  users_dropped++;
  return PLUGIN_RETVAL_DROP;

accept:
  users_total++;
  if (users_total > users_peak) {
    users_peak = users_total;
    if (users_peak > users_alltimepeak)
      users_alltimepeak = users_peak;
  }
  return PLUGIN_RETVAL_CONTINUE;
}

unsigned long pi_user_event_infoupdate (plugin_user_t * user, void *dummy, unsigned long event,
					buffer_t * token)
{
  if ((!(user->rights & CAP_OWNER)) && (!pi_user_check_user (user))) {
    users_dropped++;
    return PLUGIN_RETVAL_DROP;
  }

  return PLUGIN_RETVAL_CONTINUE;
}

unsigned long pi_user_event_logout (plugin_user_t * user, void *dummy, unsigned long event,
				    buffer_t * token)
{
  /* if this is a registered user, check for registered/op max */
  if (user->flags & PROTO_FLAG_REGISTERED) {
    if (user_registered_current > 0)
      user_registered_current--;
    if (user->op && (user_op_current > 0))
      user_op_current--;
  } else {
    if (user_unregistered_current > 0)
      user_unregistered_current--;
  }

  users_total--;
  return PLUGIN_RETVAL_CONTINUE;
}

/*************************************************************************************************************************/

unsigned long pi_user_handler_clientban (plugin_user_t * user, buffer_t * output, void *dummy,
					 unsigned int argc, unsigned char **argv)
{
  buffer_t *buf;
  double min, max;

  if (argc < 4) {
    bf_printf (output,
	       "Usage: !%s <clienttag> <minversion> <maxversion> <reason>\nUse 0 for version if unimportant.",
	       argv[0]);
    return 0;
  }

  sscanf (argv[2], "%lf", &min);
  sscanf (argv[3], "%lf", &max);

  buf = bf_alloc (strlen (argv[4]));
  if (argv[4])
    bf_strcat (buf, argv[4]);

  banlist_client_add (&clientbanlist, argv[1], min, max, buf);

  bf_free (buf);

  bf_printf (output, "Client \"%s\" (%lf, %lf) added to banned list because: %s\n", argv[1], min,
	     max, argv[4]);

  return 0;
}

unsigned long pi_user_handler_clientlist (plugin_user_t * user, buffer_t * output, void *dummy,
					  unsigned int argc, unsigned char **argv)
{
  unsigned int i;
  banlist_client_entry_t *b;

  dlhashlist_foreach (&clientbanlist, i) {
    dllist_foreach (dllist_bucket (&clientbanlist, i), b) {
      bf_printf (output, "Client \"%s\" (%lf, %lf) because: %.*s\n", b->client, b->minVersion,
		 b->maxVersion, bf_used (b->message), b->message->s);
    }
  }

  if (!bf_used (output)) {
    bf_printf (output, "No clients banned.");
  }

  return 0;
}


unsigned long pi_user_handler_clientunban (plugin_user_t * user, buffer_t * output, void *dummy,
					   unsigned int argc, unsigned char **argv)
{
  if (argc < 2) {
    bf_printf (output, "Usage: !%s <clientname>\n", argv[0]);
    return 0;
  }

  if (banlist_client_del_byclient (&clientbanlist, argv[1])) {
    bf_printf (output, "Client \"%s\" removed from banlist\n", argv[1]);
  } else {
    bf_printf (output, "Client \"%s\" not found in banlist\n", argv[1]);
  }

  return 0;
}


unsigned long pi_user_event_save (plugin_user_t * user, void *dummy, unsigned long event,
				  buffer_t * token)
{
  banlist_client_save (&clientbanlist, ClientBanFileName);
  return PLUGIN_RETVAL_CONTINUE;
}

unsigned long pi_user_event_load (plugin_user_t * user, void *dummy, unsigned long event,
				  buffer_t * token)
{
  banlist_client_clear (&clientbanlist);
  banlist_client_load (&clientbanlist, ClientBanFileName);
  return PLUGIN_RETVAL_CONTINUE;
}

unsigned long pi_user_event_config (plugin_user_t * user, void *dummy, unsigned long event,
				    config_element_t * elem)
{
  buffer_t *buf;
  struct rlimit limit;

  if ((elem->name[0] != 'u') || strncasecmp (elem->name, "userlimit", 9))
    return PLUGIN_RETVAL_CONTINUE;

  getrlimit (RLIMIT_NOFILE, &limit);

  if (limit.rlim_cur < (user_unregistered_max + user_registered_max + user_op_max)) {
    buf = bf_alloc (1024);

    bf_printf (buf,
	       "WARNING: resourcelimit for this process allows a absolute maximum of %lu users, currently %lu are configured.\n",
	       limit.rlim_cur, (user_unregistered_max + user_registered_max));

    plugin_user_sayto (NULL, user, buf);

    bf_free (buf);
  };

  return PLUGIN_RETVAL_CONTINUE;
}


/*************************************************************************************************************************/

int pi_user_init ()
{
  int i;

  banlist_client_init (&clientbanlist);

  plugin_user = plugin_register ("user");

  ClientBanFileName = strdup (PI_USER_CLIENTBANFILE);

  for (i = 0; i < 3; i++) {
    slotratios[i].minslot = 0;
    slotratios[i].maxslot = ~0;
    slotratios[i].minhub = 0;
    slotratios[i].maxhub = ~0;
    slotratios[i].ratio = 1000.0;
  }

  /* *INDENT-OFF* */
  
  config_register ("userlimit.unregistered",  CFG_ELEM_ULONG, &user_unregistered_max, "Maximum unregistered users.");
  config_register ("userlimit.registered",    CFG_ELEM_ULONG, &user_registered_max,   "Maximum registered users.");
  config_register ("userlimit.op",            CFG_ELEM_ULONG, &user_op_max,           "Reserve place for this many ops in the registered users.");

  /* config_register ("file.clientbanlist",      CFG_ELEM_STRING, &ClientBanFileName,     "Name of the file containing the client banlist."); */
  
  config_register ("sharemin.unregistered",   CFG_ELEM_ULONGLONG, &sharereq_unregistered_share, "Minimum share requirement for unregistered users.");
  config_register ("sharemin.registered",     CFG_ELEM_ULONGLONG, &sharereq_registered_share,   "Minimum share requirement for registered users.");
  config_register ("sharemin.op",             CFG_ELEM_ULONGLONG, &sharereq_op_share,           "Minimum share requirement for OPS");

  config_register ("hub.unregistered.min",    CFG_ELEM_UINT,   &slotratios[0].minhub,  "Minimum hubs for unregistered users.");
  config_register ("hub.unregistered.max",    CFG_ELEM_UINT,   &slotratios[0].maxhub,  "Maximum hubs for unregistered users.");
  config_register ("slot.unregistered.min",   CFG_ELEM_UINT,   &slotratios[0].minslot, "Minimum slots for unregistered users.");
  config_register ("slot.unregistered.max",   CFG_ELEM_UINT,   &slotratios[0].maxslot, "Maximum slots for unregistered users.");
  config_register ("slot.unregistered.ratio", CFG_ELEM_DOUBLE, &slotratios[0].ratio,   "Minimum hubs/slot ratio for unregistered users.");
    
  config_register ("hub.registered.min",      CFG_ELEM_UINT,   &slotratios[1].minhub,  "Minimum hubs for registered users.");
  config_register ("hub.registered.max",      CFG_ELEM_UINT,   &slotratios[1].maxhub,  "Maximum hubs for registered users.");
  config_register ("slot.registered.min",     CFG_ELEM_UINT,   &slotratios[1].minslot, "Minimum slots for registered users.");
  config_register ("slot.registered.max",     CFG_ELEM_UINT,   &slotratios[1].maxslot, "Maximum slots for registered users.");
  config_register ("slot.registered.ratio",   CFG_ELEM_DOUBLE, &slotratios[1].ratio,   "Minimum hubs/slot ratio for registered users.");
    
  config_register ("hub.op.min",              CFG_ELEM_UINT,   &slotratios[2].minhub,  "Minimum hubs for Operators.");
  config_register ("hub.op.max",              CFG_ELEM_UINT,   &slotratios[2].maxhub,  "Maximum hubs for Operators.");
  config_register ("slot.op.min",             CFG_ELEM_UINT,   &slotratios[2].minslot, "Minimum slots for Operators.");
  config_register ("slot.op.max",             CFG_ELEM_UINT,   &slotratios[2].maxslot, "Maximum slots for Operators.");
  config_register ("slot.op.ratio",           CFG_ELEM_DOUBLE, &slotratios[2].ratio,   "Minimum hubs/slot ratio for Operators.");

  plugin_request (plugin_user, PLUGIN_EVENT_PRELOGIN,   (plugin_event_handler_t *) &pi_user_event_prelogin);
  plugin_request (plugin_user, PLUGIN_EVENT_LOGOUT,     (plugin_event_handler_t *) &pi_user_event_logout);
  plugin_request (plugin_user, PLUGIN_EVENT_INFOUPDATE, (plugin_event_handler_t *) &pi_user_event_infoupdate);

  plugin_request (plugin_user, PLUGIN_EVENT_LOAD,  &pi_user_event_load);
  plugin_request (plugin_user, PLUGIN_EVENT_SAVE,  &pi_user_event_save);
  
  plugin_request (plugin_user, PLUGIN_EVENT_CONFIG,  (plugin_event_handler_t *) &pi_user_event_config);
  
  command_register ("statuser", &pi_user_handler_userstat, 0, "Show logged in user counts.");

  command_register ("clientban",     &pi_user_handler_clientban,   CAP_CONFIG, "Ban a client.");
  command_register ("clientbanlist", &pi_user_handler_clientlist,  CAP_KEY,    "List client bans.");
  command_register ("clientunban",   &pi_user_handler_clientunban, CAP_CONFIG, "Unban a client.");
  
  /* *INDENT-ON* */  

  return 0;
}
