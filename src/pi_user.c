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

#include "hub.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "plugin.h"
#include "user.h"
#include "commands.h"
#include "banlist.h"
#include "banlistclient.h"
#include "utils.h"

#include "esocket.h"

#define PI_USER_CLIENTBANFILE "clientbanlist.conf"
#define PI_USER_RESTRICTFILE "restrict.conf"

typedef struct slotratio {
  unsigned int minslot;
  unsigned int maxslot;
  unsigned int minhub;
  unsigned int maxhub;
  double ratio;
} slotratio_t;


banlist_t sourcelist;
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
  bf_printf (output, _("Unregistered users: %lu / %lu\n"
		       "Registered users:   %lu / %lu\n"
		       "Operators           %lu / %lu\n"
		       " Total      %lu / %lu\n"
		       " Peak             %lu\n"
		       " Dropped          %lu\n"
		       " Buffering        %lu\n"),
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
      plugin_user_printf (user, _("Sorry, your class has a maximum of %u hubs.\n"),
			  slotratios[class].maxhub);
      goto drop;
    }
    if (user->hubs[0] < slotratios[class].minhub) {
      plugin_user_printf (user, _("Sorry, your class has a minimum of %u hubs.\n"),
			  slotratios[class].minhub);
      goto drop;
    }
    if (user->slots > slotratios[class].maxslot) {
      plugin_user_printf (user, _("Sorry, your class has a maximum of %u slots.\n"),
			  slotratios[class].maxslot);
      goto drop;
    }
    if (user->slots < slotratios[class].minslot) {
      plugin_user_printf (user, _("Sorry, your class has a minimum of %u slots.\n"),
			  slotratios[class].minslot);
      goto drop;
    }
    /* if slot rules allow 0 slots, do not kick for a bad slotratio... */
    if ((user->slots) && (user->hubs[0] > (slotratios[class].ratio * user->slots))) {
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
	  plugin_user_printf (user, _("Sorry, your class has a minimum share of %s.\n"),
			      format_size (sharereq_op_share));
	  goto drop;
	}
      } else {
	if (user->share < sharereq_registered_share) {
	  plugin_user_printf (user, _("Sorry, your class has a minimum share of %s.\n"),
			      format_size (sharereq_registered_share));
	  goto drop;
	}
      }
    } else {
      if (user->share < sharereq_unregistered_share) {
	plugin_user_printf (user, _("Sorry, your class has a minimum share of %s.\n"),
			    format_size (sharereq_unregistered_share));
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
  if ((cl = banlist_client_find (&clientbanlist, user->client, user->version))
      && (!(user->rights & CAP_TAG))) {
    plugin_user_printf (user, _("Sorry, this client is not accepted because: %*s\n"),
			bf_used (cl->message), cl->message->s);
    goto drop;
  }

  /* check if the user requires source ip verification */
  if ((user->rights & CAP_SOURCEVERIFY)
      && (!banlist_find (&sourcelist, user->nick, user->ipaddress))) {
    struct in_addr ip;

    ip.s_addr = user->ipaddress;
    plugin_user_printf (user, _("Sorry, your login is not accepted from this IP (%s)"),
			inet_ntoa (ip));
    goto drop;
  }

  /* check hub/slot/ratio values */
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
		      _("Sorry, the hub is full. It cannot accept more users from your class.\n"));

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

unsigned long pi_user_handler_userrestrict (plugin_user_t * user, buffer_t * output, void *priv,
					    unsigned int argc, unsigned char **argv)
{
  struct in_addr ip, netmask;
  plugin_user_t *tgt;

  if (argc < 3) {
    bf_printf (output, _("Usage: %s <nick> <ip/network>"), argv[0]);
    return 0;
  }

  if (parse_ip (argv[2], &ip, &netmask)) {
    banlist_add (&sourcelist, user->nick, argv[1], ip.s_addr, netmask.s_addr, bf_buffer (""), 0);
    if ((tgt = plugin_user_find (plugin_user, argv[1]))) {
      if (!(tgt->rights & CAP_SOURCEVERIFY)) {
	bf_printf (output,
		   _("Please do not forget to assign the \"sourceverify\" right to user %s\n"),
		   argv[1]);
      }
    }
    bf_printf (output, _("User %s is now allowed to log in from %s\n"), argv[1],
	       print_ip (ip, netmask));
  } else {
    bf_printf (output, _("Sorry, \"%s\" is not a recognisable IP address or network."), argv[2]);
  }

  return 0;
}

unsigned long pi_user_handler_userunrestrict (plugin_user_t * user, buffer_t * output, void *priv,
					      unsigned int argc, unsigned char **argv)
{
  struct in_addr ip, netmask;
  banlist_entry_t *e;

  if (argc < 3) {
    bf_printf (output, _("Usage: %s <nick> <ip/network>"), argv[0]);
    return 0;
  }

  if (parse_ip (argv[2], &ip, &netmask)) {
    e = banlist_find_exact (&sourcelist, argv[1], ip.s_addr, netmask.s_addr);
    if (e) {
      banlist_del (&sourcelist, e);
      bf_printf (output, _("User %s is no longer allowed to log in from %s\n"), argv[1],
		 print_ip (ip, netmask));
    } else {
      bf_printf (output, _("Could not find source restriction \"%s\" for nick %s\n"), argv[2],
		 argv[1]);
    }
  } else {
    bf_printf (output, _("Sorry, \"%s\" is not a recognisable IP address or network."), argv[2]);
  }

  return 0;
}

unsigned long pi_user_handler_userrestrictlist (plugin_user_t * user, buffer_t * output, void *priv,
						unsigned int argc, unsigned char **argv)
{
  int i;
  struct in_addr ip, netmask;
  banlist_entry_t *e = NULL;

  if (argc < 2) {
    bf_printf (output, _("Usage: %s <nick>"), argv[0]);
    return 0;
  }

  i = 0;
  bf_printf (output, _("Allow user %s from:\n"), argv[1]);
  while ((e = banlist_find_bynick_next (&sourcelist, e, argv[1]))) {
    ip.s_addr = e->ip;
    netmask.s_addr = e->netmask;
    bf_printf (output, _(" Source %s\n"), print_ip (ip, netmask));
    i++;
  }
  if (!i)
    bf_printf (output, _(" None."));

  return 0;
}

unsigned long pi_user_handler_clientban (plugin_user_t * user, buffer_t * output, void *dummy,
					 unsigned int argc, unsigned char **argv)
{
  buffer_t *buf;
  double min, max;

  if (argc < 4) {
    bf_printf (output,
	       _
	       ("Usage: !%s <clienttag> <minversion> <maxversion> <reason>\nUse 0 for version if unimportant."),
	       argv[0]);
    return 0;
  }

  sscanf (argv[2], "%lf", &min);
  sscanf (argv[3], "%lf", &max);

  if (argv[4]) {
    buf = bf_alloc (strlen (argv[4]));
    bf_strcat (buf, argv[4]);
  } else
    buf = NULL;

  banlist_client_add (&clientbanlist, argv[1], min, max, buf);

  if (buf)
    bf_free (buf);

  bf_printf (output, _("Client \"%s\" (%lf, %lf) added to banned list because: %s\n"), argv[1], min,
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
      bf_printf (output, _("Client \"%s\" (%lf, %lf) because: %.*s\n"), b->client, b->minVersion,
		 b->maxVersion, bf_used (b->message), b->message->s);
    }
  }

  if (!bf_used (output)) {
    bf_printf (output, _("No clients banned."));
  }

  return 0;
}


unsigned long pi_user_handler_clientunban (plugin_user_t * user, buffer_t * output, void *dummy,
					   unsigned int argc, unsigned char **argv)
{
  double min, max;

  if (argc < 4) {
    bf_printf (output, _("Usage: !%s <clientname> <min> <max>\n"), argv[0]);
    return 0;
  }

  sscanf (argv[2], "%lf", &min);
  sscanf (argv[3], "%lf", &max);

  if (banlist_client_del_byclient (&clientbanlist, argv[1], min, max)) {
    bf_printf (output, _("Client \"%s\" removed from banlist\n"), argv[1]);
  } else {
    bf_printf (output, _("Client \"%s\" not found in banlist\n"), argv[1]);
  }

  return 0;
}


unsigned long pi_user_event_save (plugin_user_t * user, void *dummy, unsigned long event,
				  buffer_t * token)
{
  banlist_client_save (&clientbanlist, ClientBanFileName);
  banlist_save (&sourcelist, PI_USER_RESTRICTFILE);
  return PLUGIN_RETVAL_CONTINUE;
}

unsigned long pi_user_event_load (plugin_user_t * user, void *dummy, unsigned long event,
				  buffer_t * token)
{
  banlist_client_clear (&clientbanlist);
  banlist_client_load (&clientbanlist, ClientBanFileName);
  banlist_clear (&sourcelist);
  banlist_load (&sourcelist, PI_USER_RESTRICTFILE);
  return PLUGIN_RETVAL_CONTINUE;
}

unsigned long pi_user_event_config (plugin_user_t * user, void *dummy, unsigned long event,
				    config_element_t * elem)
{
  buffer_t *buf;
  struct rlimit limit;

  if ((elem->name[0] != 'u') || strncasecmp (elem->name, "userlimit", 9))
    return PLUGIN_RETVAL_CONTINUE;

  buf = bf_alloc (1024);

  getrlimit (RLIMIT_NOFILE, &limit);

  if (limit.rlim_cur < (user_unregistered_max + user_registered_max + user_op_max)) {

    bf_printf (buf,
	       _
	       ("WARNING: resourcelimit for this process allows a absolute maximum of %lu users, currently %lu are configured.\n"),
	       limit.rlim_cur, (user_unregistered_max + user_registered_max));

  };

#ifndef USE_EPOLL
#ifndef USE_POLL
  if ((user_unregistered_max + user_registered_max + user_op_max) >= (FD_SETSIZE - 5)) {
    bf_printf (buf,
	       _
	       ("WARNING: You are using an Aquila version based on select(). This limits the effective maximum size of your hub to %u. Going over this limit may crash your hub.\n"),
	       (FD_SETSIZE - 5));
  }
#else
  if ((user_unregistered_max + user_registered_max + user_op_max) >= 5000) {
    bf_printf (buf,
	       _
	       ("WARNING: You are using an Aquila version based on poll(). This limits the performance of your hub with larger sizes. Please consider moving to kernel version 2.6 and a recent glibc.\n"));
  }
#endif
#endif

  if (user_registered_max == user_op_max) {
    bf_printf (buf,
	       _
	       ("WARNING: userlimit.registered equals userlimit.op. userlimit.registered includes the ops (since they are registered too): setting them equal means that you only allow OPs, but not normal registered users.\n"));
  }

  plugin_user_sayto (NULL, user, buf, 0);

  bf_free (buf);

  return PLUGIN_RETVAL_CONTINUE;
}


/*************************************************************************************************************************/

int pi_user_init (plugin_manager_t * manager)
{
  int i;

  banlist_client_init (&clientbanlist);
  banlist_init (&sourcelist);

  plugin_user = plugin_register (manager, "user");

  ClientBanFileName = strdup (PI_USER_CLIENTBANFILE);

  for (i = 0; i < 3; i++) {
    slotratios[i].minslot = 0;
    slotratios[i].maxslot = ~0;
    slotratios[i].minhub = 0;
    slotratios[i].maxhub = ~0;
    slotratios[i].ratio = 1000.0;
  }

  /* *INDENT-OFF* */
  
  config_register ("userlimit.unregistered",  CFG_ELEM_ULONG, &user_unregistered_max, _("Maximum unregistered users."));
  config_register ("userlimit.registered",    CFG_ELEM_ULONG, &user_registered_max,   _("Maximum registered users."));
  config_register ("userlimit.op",            CFG_ELEM_ULONG, &user_op_max,           _("Reserve place for this many ops in the registered users."));

  /* config_register ("file.clientbanlist",      CFG_ELEM_STRING, &ClientBanFileName,     "Name of the file containing the client banlist."); */
  
  config_register ("sharemin.unregistered",   CFG_ELEM_BYTESIZE, &sharereq_unregistered_share, _("Minimum share requirement for unregistered users."));
  config_register ("sharemin.registered",     CFG_ELEM_BYTESIZE, &sharereq_registered_share,   _("Minimum share requirement for registered users."));
  config_register ("sharemin.op",             CFG_ELEM_BYTESIZE, &sharereq_op_share,           _("Minimum share requirement for OPS"));

  config_register ("hub.unregistered.min",    CFG_ELEM_UINT,   &slotratios[0].minhub,  _("Minimum hubs for unregistered users."));
  config_register ("hub.unregistered.max",    CFG_ELEM_UINT,   &slotratios[0].maxhub,  _("Maximum hubs for unregistered users."));
  config_register ("slot.unregistered.min",   CFG_ELEM_UINT,   &slotratios[0].minslot, _("Minimum slots for unregistered users."));
  config_register ("slot.unregistered.max",   CFG_ELEM_UINT,   &slotratios[0].maxslot, _("Maximum slots for unregistered users."));
  config_register ("slot.unregistered.ratio", CFG_ELEM_DOUBLE, &slotratios[0].ratio,   _("Minimum hubs/slot ratio for unregistered users."));
    
  config_register ("hub.registered.min",      CFG_ELEM_UINT,   &slotratios[1].minhub,  _("Minimum hubs for registered users."));
  config_register ("hub.registered.max",      CFG_ELEM_UINT,   &slotratios[1].maxhub,  _("Maximum hubs for registered users."));
  config_register ("slot.registered.min",     CFG_ELEM_UINT,   &slotratios[1].minslot, _("Minimum slots for registered users."));
  config_register ("slot.registered.max",     CFG_ELEM_UINT,   &slotratios[1].maxslot, _("Maximum slots for registered users."));
  config_register ("slot.registered.ratio",   CFG_ELEM_DOUBLE, &slotratios[1].ratio,   _("Minimum hubs/slot ratio for registered users."));
    
  config_register ("hub.op.min",              CFG_ELEM_UINT,   &slotratios[2].minhub,  _("Minimum hubs for Operators."));
  config_register ("hub.op.max",              CFG_ELEM_UINT,   &slotratios[2].maxhub,  _("Maximum hubs for Operators."));
  config_register ("slot.op.min",             CFG_ELEM_UINT,   &slotratios[2].minslot, _("Minimum slots for Operators."));
  config_register ("slot.op.max",             CFG_ELEM_UINT,   &slotratios[2].maxslot, _("Maximum slots for Operators."));
  config_register ("slot.op.ratio",           CFG_ELEM_DOUBLE, &slotratios[2].ratio,   _("Minimum hubs/slot ratio for Operators."));

  plugin_request (plugin_user, PLUGIN_EVENT_PRELOGIN,   (plugin_event_handler_t *) &pi_user_event_prelogin);
  plugin_request (plugin_user, PLUGIN_EVENT_LOGOUT,     (plugin_event_handler_t *) &pi_user_event_logout);
  plugin_request (plugin_user, PLUGIN_EVENT_INFOUPDATE, (plugin_event_handler_t *) &pi_user_event_infoupdate);

  plugin_request (plugin_user, PLUGIN_EVENT_LOAD,  &pi_user_event_load);
  plugin_request (plugin_user, PLUGIN_EVENT_SAVE,  &pi_user_event_save);
  
  plugin_request (plugin_user, PLUGIN_EVENT_CONFIG,  (plugin_event_handler_t *) &pi_user_event_config);
  
  command_register ("statuser", &pi_user_handler_userstat, 0, _("Show logged in user counts."));

  command_register ("clientban",     &pi_user_handler_clientban,   CAP_CONFIG, _("Ban a client."));
  command_register ("clientbanlist", &pi_user_handler_clientlist,  CAP_KEY,    _("List client bans."));
  command_register ("clientunban",   &pi_user_handler_clientunban, CAP_CONFIG, _("Unban a client."));

  command_register ("userrestrict",     &pi_user_handler_userrestrict,     CAP_USER, _("Add a source IP restriction for a user."));
  command_register ("userunrestrict",   &pi_user_handler_userunrestrict,   CAP_USER, _("Remove a source IP restriction for a user."));
  command_register ("userrestrictlist", &pi_user_handler_userrestrictlist, CAP_USER, _("Show source IP restrictions for a user."));
 
  /* *INDENT-ON* */  

  return 0;
}
