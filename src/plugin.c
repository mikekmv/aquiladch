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

#include "plugin_int.h"
#include <sys/types.h>
#include <string.h>
#include <assert.h>

#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif

#include "aqtime.h"
#include "utils.h"
#include "banlist.h"
#include "user.h"
#include "core_config.h"
#include "hashlist_func.h"

/*
 *    user_t -> plugin_priv -> plugin_user_t
 *    
 */

#define USERT_TO_PLUGINPRIV(usr)	((plugin_private_t *) usr->plugin_priv)
#define USERT_TO_PLUGINUSER(usr)	(&((plugin_private_t *) usr->plugin_priv)->user)
#define USERT_TO_PROTO(usr)		(((plugin_private_t *) usr->plugin_priv)->proto)

#define PLUGINPRIV(user) ((plugin_private_t *) (user)->private)
#define USERT(user) 	PLUGINPRIV(user)->parent
#define PROTO(user)	PLUGINPRIV(user)->proto

unsigned char *ConfigFile;
unsigned char *HardBanFile;
unsigned char *SoftBanFile;
unsigned char *AccountsFile;

unsigned char *KickBanRedirect;

unsigned long pluginIDs;

cap_array_t plugin_supports[] = {
  {"NoGetINFO", 1, ""},
  {"NoHello", 2, ""},
  {"UserCommand", 4, ""},
  {"UserIP2", 8, ""},
  {"QuickList", 16, ""},
  {"TTHSearch", 32, ""},
  {NULL, 0, NULL}
};

dllist_entry_t managerlist;

plugin_manager_t *plugin_manager_find (proto_t * proto)
{
  plugin_manager_t *m;

  dllist_foreach (&managerlist, m)
    if (m->proto == proto)
    return m;
};

/******************************* UTILITIES: REPORING **************************************/

unsigned int plugin_report (plugin_t * plugin, buffer_t * message)
{
  user_t *u;
  proto_t *proto = plugin->manager->proto;

  if (!*config.SysReportTarget)
    return EINVAL;

  u = proto->user_find (config.SysReportTarget);

  if (!u)
    return EINVAL;

  return proto->chat_priv (proto->HubSec, u, proto->HubSec, message);
}

unsigned int plugin_perror (plugin_t * plugin, unsigned char *format, ...)
{
  va_list ap;
  int retval;
  buffer_t *b;
  proto_t *proto = plugin->manager->proto;

  b = bf_alloc (1024);

  va_start (ap, format);
  bf_vprintf (b, format, ap);
  va_end (ap);

  bf_printf (b, ": %s", strerror (errno));

  retval = plugin_report (plugin, b);

  bf_free (b);

  return retval;
}

/******************************* UTILITIES: USER MANAGEMENT **************************************/

unsigned int plugin_user_next (plugin_t * plugin, plugin_user_t ** user)
{
  user_t *u = *user ? USERT (*user)->next : plugin->manager->proto->user_find (NULL);

  //while (u && !(u->flags & PROTO_FLAG_ONLINE))
  //  u = u->next;

  *user = u ? &((plugin_private_t *) u->plugin_priv)->user : NULL;

  return (*user != NULL);
}

plugin_user_t *plugin_user_find (plugin_t * plugin, unsigned char *name)
{
  user_t *u;
  proto_t *proto = plugin->manager->proto;

  u = proto->user_find (name);
  if (!u)
    return NULL;

  return USERT_TO_PLUGINUSER (u);
}

plugin_user_t *plugin_user_find_ip (plugin_t * plugin, plugin_user_t * last, unsigned long ip)
{
  user_t *u = last ? ((plugin_private_t *) (last)->private)->parent : NULL;

  plugin->manager->proto->user_find_ip (u, ip);

  return USERT_TO_PLUGINUSER (u);
}

plugin_user_t *plugin_user_find_net (plugin_t * plugin, plugin_user_t * last, unsigned long ip,
				     unsigned long netmask)
{
  user_t *u = last ? ((plugin_private_t *) (last)->private)->parent : NULL;

  plugin->manager->proto->user_find_net (u, ip, netmask);

  return &((plugin_private_t *) u->plugin_priv)->user;
}

buffer_t *plugin_user_getmyinfo (plugin_user_t * user)
{
  //return ((nmdc_user_t *) ((plugin_private_t *) user->private)->parent)->MyINFO;
  return NULL;
}

plugin_user_t *plugin_hubsec (plugin_t * plugin)
{
  return USERT_TO_PLUGINUSER (plugin->manager->proto->HubSec);
}

/******************************* UTILITIES: KICK/BAN **************************************/

unsigned int plugin_user_drop (plugin_user_t * user, buffer_t * message)
{
  user_t *u;

  if (!user)
    return 0;

  u = USERT (user);

  if (u->flags & PROTO_FLAG_VIRTUAL)
    return 0;

  return PROTO (user)->user_drop (u, message);
}

unsigned int plugin_user_kick (plugin_user_t * op, plugin_user_t * user, buffer_t * message)
{
  user_t *u;
  buffer_t *b;
  unsigned int retval;
  proto_t *proto = PROTO (op);

  if (!user)
    return 0;

  u = USERT (user);

  if (u->flags & PROTO_FLAG_VIRTUAL)
    return 0;

  banlist_add (proto->get_banlist_soft (), (op ? op->nick : proto->HubSec->nick), u->nick,
	       u->ipaddress, 0xffffffff, message, now.tv_sec + config.defaultKickPeriod);

  b = bf_alloc (265 + bf_used (message));

  bf_printf (b, _("You have been kicked by %s because: %.*s\n"),
	     (op ? op->nick : proto->HubSec->nick), bf_used (message), message->s);

  plugin_send_event (plugin_manager_find (proto), PLUGINPRIV (user), PLUGIN_EVENT_KICK, b);

  retval = proto->user_forcemove (u, config.KickBanRedirect, b);

  bf_free (b);

  return retval;
}

unsigned int plugin_user_banip (plugin_user_t * op, plugin_user_t * user, buffer_t * message,
				unsigned long period)
{
  user_t *u;
  buffer_t *b;
  unsigned int retval;
  proto_t *proto = PROTO (op);

  if (!user)
    return 0;

  u = ((plugin_private_t *) user->private)->parent;

  if (u->flags & PROTO_FLAG_VIRTUAL)
    return 0;

  banlist_add (proto->get_banlist_soft (), (op ? op->nick : proto->HubSec->nick), u->nick,
	       u->ipaddress, 0xffffffff, message, period ? now.tv_sec + period : 0);

  b = bf_alloc (265 + bf_used (message));

  bf_printf (b, _("You have been banned by %s because: %.*s\n"),
	     (op ? op->nick : proto->HubSec->nick), bf_used (message), message->s);

  plugin_send_event (plugin_manager_find (proto), PLUGINPRIV (user), PLUGIN_EVENT_BAN, b);

  retval = proto->user_forcemove (u, config.KickBanRedirect, b);

  bf_free (b);

  return retval;
}

unsigned int plugin_user_unban (plugin_user_t * user)
{
  user_t *u;

  if (!user)
    return 0;

  u = ((plugin_private_t *) user->private)->parent;

  if (u->flags & PROTO_FLAG_VIRTUAL)
    return 0;

  banlist_del_bynick (PROTO (user)->get_banlist_soft (), u->nick);
  return 0;
}

unsigned int plugin_user_zombie (plugin_user_t * user)
{
  user_t *u;

  if (!user)
    return 0;

  u = ((plugin_private_t *) user->private)->parent;

  user->flags |= PLUGIN_FLAG_ZOMBIE;
  u->flags |= PROTO_FLAG_ZOMBIE;

  return 0;
}

unsigned int plugin_user_unzombie (plugin_user_t * user)
{
  user_t *u;

  if (!user)
    return 0;

  u = ((plugin_private_t *) user->private)->parent;

  user->flags &= !PLUGIN_FLAG_ZOMBIE;
  u->flags &= !PROTO_FLAG_ZOMBIE;

  return 0;
}

unsigned int plugin_unban (plugin_user_t * op, unsigned char *nick)
{
  proto_t *proto = PROTO (op);

  if (!nick)
    return 0;

  return banlist_del_bynick (proto->get_banlist_soft (), nick);
}

unsigned int plugin_ban_ip (plugin_user_t * op, unsigned long ip, unsigned long netmask,
			    buffer_t * message, unsigned long period)
{
  proto_t *proto = PROTO (op);

  banlist_add (proto->get_banlist_soft (), (op ? op->nick : proto->HubSec->nick), "", ip, netmask,
	       message, period ? now.tv_sec + period : 0);
  return 0;
}

unsigned int plugin_unban_ip (plugin_user_t * op, unsigned long ip, unsigned long netmask)
{
  return banlist_del_byip (PROTO (op)->get_banlist_soft (), ip, netmask);
}

unsigned int plugin_ban_nick (plugin_user_t * op, unsigned char *nick, buffer_t * message,
			      unsigned long period)
{
  proto_t *proto = PROTO (op);

  banlist_add (proto->get_banlist_soft (), (op ? op->nick : proto->HubSec->nick), nick, 0L, 0L,
	       message, period ? now.tv_sec + period : 0);
  return 0;
}

unsigned int plugin_ban (plugin_user_t * op, unsigned char *nick, unsigned long ip,
			 unsigned long netmask, buffer_t * message, unsigned long period)
{
  proto_t *proto = PROTO (op);

  banlist_add (proto->get_banlist_soft (), (op ? op->nick : proto->HubSec->nick), nick, ip, netmask,
	       message, period ? now.tv_sec + period : 0);
  return 0;
}

unsigned int plugin_unban_nick (plugin_user_t * op, unsigned char *nick)
{
  banlist_del_bynick (PROTO (op)->get_banlist_soft (), nick);
  return 0;
}

unsigned int plugin_unban_ip_hard (plugin_user_t * op, unsigned long ip, unsigned long netmask)
{
  banlist_del_byip (PROTO (op)->get_banlist_hard (), ip, netmask);
  return 0;
}

unsigned int plugin_ban_ip_hard (plugin_user_t * op, unsigned long ip, unsigned long netmask,
				 buffer_t * message, unsigned long period)
{
  proto_t *proto = PROTO (op);

  banlist_add (proto->get_banlist_hard (), (op ? op->nick : proto->HubSec->nick), "", ip, netmask,
	       message, period ? now.tv_sec + period : 0);
  return 0;
}

unsigned int plugin_user_banip_hard (plugin_user_t * op, plugin_user_t * user, buffer_t * message,
				     unsigned long period)
{
  user_t *u;
  buffer_t *b;
  unsigned int retval;
  proto_t *proto = PROTO (op);

  if (!user)
    return 0;

  u = ((plugin_private_t *) user->private)->parent;

  if (u->flags & PROTO_FLAG_VIRTUAL)
    return 0;

  banlist_add (proto->get_banlist_soft (), (op ? op->nick : proto->HubSec->nick), u->nick,
	       u->ipaddress, 0xffffffff, message, period ? now.tv_sec + period : 0);

  b = bf_alloc (265 + bf_used (message));

  bf_printf (b, _("You have been banned by %s because: %.*s\n"),
	     (op ? op->nick : proto->HubSec->nick), bf_used (message), message->s);

  plugin_send_event (plugin_manager_find (proto), PLUGINPRIV (user), PLUGIN_EVENT_BAN, b);

  retval = proto->user_forcemove (u, config.KickBanRedirect, b);

  bf_free (b);

  return retval;
}

unsigned int plugin_user_bannick (plugin_user_t * op, plugin_user_t * user, buffer_t * message,
				  unsigned long period)
{
  user_t *u;
  buffer_t *b;
  unsigned int retval;
  proto_t *proto = PROTO (op);

  if (!user)
    return 0;

  u = ((plugin_private_t *) user->private)->parent;

  if (u->flags & PROTO_FLAG_VIRTUAL)
    return 0;

  banlist_add (proto->get_banlist_soft (), (op ? op->nick : proto->HubSec->nick), u->nick, 0L, 0L,
	       message, period ? now.tv_sec + period : 0);

  b = bf_alloc (265 + bf_used (message));

  bf_printf (b, _("You have been banned by %s because: %.*s\n"),
	     (op ? op->nick : proto->HubSec->nick), bf_used (message), message->s);

  plugin_send_event (plugin_manager_find (proto), PLUGINPRIV (user), PLUGIN_EVENT_BAN, b);

  retval = proto->user_forcemove (u, config.KickBanRedirect, b);

  bf_free (b);

  return retval;
}

unsigned int plugin_user_ban (plugin_user_t * op, plugin_user_t * user, buffer_t * message,
			      unsigned long period)
{
  user_t *u;
  buffer_t *b;
  unsigned int retval;
  proto_t *proto = PROTO (op);

  if (!user)
    return 0;

  u = ((plugin_private_t *) user->private)->parent;

  if (u->flags & PROTO_FLAG_VIRTUAL)
    return 0;

  banlist_add (proto->get_banlist_soft (), (op ? op->nick : proto->HubSec->nick), u->nick,
	       u->ipaddress, 0xffffffff, message, period ? now.tv_sec + period : 0);

  b = bf_alloc (265 + bf_used (message));

  bf_printf (b, _("You have been banned by %s because: %.*s\n"),
	     (op ? op->nick : proto->HubSec->nick), bf_used (message), message->s);
  retval = proto->user_forcemove (u, config.KickBanRedirect, b);

  bf_free (b);

  return retval;
}

unsigned int plugin_user_findnickban (plugin_user_t * op, buffer_t * buf, unsigned char *nick)
{
  banlist_entry_t *ne;
  proto_t *proto = PROTO (op);

  ne = banlist_find_bynick (proto->get_banlist_soft (), nick);
  if (!ne)
    return 0;

  if (ne->expire) {
    return bf_printf (buf, _("Found nick ban by %s for %s for %lus because: %.*s"), ne->op,
		      ne->nick, ne->expire - now.tv_sec, bf_used (ne->message), ne->message->s);
  } else {
    return bf_printf (buf, _("Found permanent nick ban by %s for %s because: %.*s"), ne->op,
		      ne->nick, bf_used (ne->message), ne->message->s);
  }
}

unsigned int plugin_user_findipban (plugin_user_t * op, buffer_t * buf, unsigned long ip)
{
  struct in_addr ipa, netmask;
  banlist_entry_t *ie;
  proto_t *proto = PROTO (op);

  ie = banlist_find_byip (proto->get_banlist_soft (), ip);
  if (!ie)
    return 0;

  ipa.s_addr = ie->ip;
  netmask.s_addr = ie->netmask;
  if (ie->expire) {
    return bf_printf (buf, _("Found IP ban by %s for %s for %lus because: %.*s"), ie->op,
		      print_ip (ipa, netmask), ie->expire - now.tv_sec, bf_used (ie->message),
		      ie->message->s);
  } else {
    return bf_printf (buf, _("Found permanent ban by %s for %s because: %.*s"), ie->op,
		      print_ip (ipa, netmask), bf_used (ie->message), ie->message->s);
  }
}

unsigned int plugin_banlist (plugin_user_t * op, buffer_t * output)
{
  unsigned long bucket, n;
  banlist_entry_t *lst, *e;
  struct in_addr ip, nm;
  proto_t *proto = PROTO (op);

  n = 0;
  dlhashlist_foreach (&(proto->get_banlist_soft ()->list_ip), bucket) {
    lst = dllist_bucket (&(proto->get_banlist_soft ()->list_ip), bucket);
    dllist_foreach (lst, e) {
      ip.s_addr = e->ip;
      nm.s_addr = e->netmask;
      if (e->expire) {
	bf_printf (output, _("%s %s by %s Expires: %s Message: %.*s\n"), e->nick, print_ip (ip, nm),
		   e->op, time_print (e->expire - now.tv_sec), bf_used (e->message), e->message->s);
      } else {
	bf_printf (output, _("%s %s by %s Message: %.*s\n"), e->nick, print_ip (ip, nm), e->op,
		   bf_used (e->message), e->message->s);
      }
      n++;
    }
  }
  return (n > 0);
}

unsigned int plugin_user_setrights (plugin_user_t * user, unsigned long cap, unsigned long ncap)
{
  user_t *u;

  u = ((plugin_private_t *) user->private)->parent;

  user->rights |= cap;
  user->rights &= ~ncap;

  u->rights |= cap;
  u->rights &= ~ncap;

  return 0;
}

/******************************* UTILITIES: USER MANAGEMENT **************************************/

unsigned int plugin_user_redirect (plugin_user_t * user, buffer_t * message)
{
  user_t *u;

  if (!user)
    return 0;

  u = ((plugin_private_t *) user->private)->parent;

  if (u->flags & PROTO_FLAG_VIRTUAL)
    return 0;

  return ((plugin_private_t *) user->private)->proto->user_redirect (u, message);
}

unsigned int plugin_user_forcemove (plugin_user_t * user, unsigned char *destination,
				    buffer_t * message)
{
  user_t *u;

  if (!user)
    return 0;

  u = ((plugin_private_t *) user->private)->parent;

  if (u->flags & PROTO_FLAG_VIRTUAL)
    return 0;

  if (!destination || !*destination)
    return ((plugin_private_t *) user->private)->proto->user_redirect (u, message);

  return ((plugin_private_t *) user->private)->proto->user_forcemove (u, destination, message);
}

/******************************* UTILITIES: USER MANAGEMENT **************************************/

unsigned int plugin_user_say (plugin_user_t * src, buffer_t * message)
{
  user_t *u;
  proto_t *proto = PROTO (src);

  if (src) {
    u = USERT (src);
  } else {
    u = proto->HubSec;
  }

  /* delete trailing \n */
  if (bf_used (message) && (message->s[bf_used (message) - 1] == '\n')) {
    message->s[bf_used (message) - 1] = '\0';
    message->e--;
  }
  return proto->chat_main (u, message);
}

unsigned int plugin_user_raw (plugin_user_t * tgt, buffer_t * message)
{
  user_t *u;

  if (!tgt)
    return 0;

  u = USERT (tgt);

  /* delete trailing \n */
  if (bf_used (message) && (message->s[bf_used (message) - 1] == '\n')) {
    message->s[bf_used (message) - 1] = '\0';
    message->e--;
  }

  return PROTO (tgt)->raw_send (u, message);
}


unsigned int plugin_user_raw_all (plugin_t * plugin, buffer_t * message)
{
  /* delete trailing \n */
  if (bf_used (message) && (message->s[bf_used (message) - 1] == '\n')) {
    message->s[bf_used (message) - 1] = '\0';
    message->e--;
  }

  return plugin->manager->proto->raw_send_all (message);
}

unsigned int plugin_user_sayto (plugin_user_t * src, plugin_user_t * target, buffer_t * message,
				int direct)
{
  user_t *u, *t;
  proto_t *proto = PROTO (target);

  if (src) {
    u = USERT (src);
  } else {
    u = proto->HubSec;
  }

  /* delete trailing \n */
  if (bf_used (message) && (message->s[bf_used (message) - 1] == '\n')) {
    message->s[bf_used (message) - 1] = '\0';
    message->e--;
  }

  t = USERT (target);

  if (t->flags & PROTO_FLAG_VIRTUAL)
    return 0;

  return direct ? proto->chat_send_direct (u, t, message) : proto->chat_send (u, t, message);
}

unsigned int plugin_user_priv (plugin_user_t * src, plugin_user_t * target, plugin_user_t * user,
			       buffer_t * message, int direct)
{
  user_t *u, *t, *s;
  proto_t *proto = PROTO (target);

  if (src) {
    u = USERT (src);
  } else {
    u = proto->HubSec;
  }

  /* delete trailing \n */
  if (bf_used (message) && (message->s[bf_used (message) - 1] == '\n')) {
    message->s[bf_used (message) - 1] = '\0';
    message->e--;
  }

  t = USERT (target);

  if (user) {
    s = USERT (user);
  } else {
    s = proto->HubSec;
  }

  return direct ? proto->chat_priv_direct (u, t, s, message) : proto->chat_priv (u, t, s, message);
}

unsigned int plugin_user_printf (plugin_user_t * user, const char *format, ...)
{
  va_list ap;
  buffer_t *buf;

  buf = bf_alloc (1024);

  va_start (ap, format);
  bf_vprintf (buf, format, ap);
  va_end (ap);

  plugin_user_sayto (NULL, user, buf, 0);

  bf_free (buf);

  return 0;
}

/******************************* ROBOTS EVENTS *******************************************/

plugin_user_t *plugin_robot_add (plugin_t * plugin, unsigned char *name, unsigned char *description,
				 plugin_event_handler_t * handler)
{
  user_t *u;
  plugin_private_t *priv;

  u = plugin->manager->proto->robot_add (name, description);
  u->rights |= CAP_KEY;
  plugin_new_user (plugin->manager, (plugin_private_t **) & u->plugin_priv, u);

  priv = ((plugin_private_t *) u->plugin_priv);
  priv->handler = handler;

  return &priv->user;
}



int plugin_robot_remove (plugin_user_t * robot)
{
  user_t *u;

  u = USERT (robot);

  PROTO (robot)->robot_del (u);

  return 0;
}

plugin_event_handler_t *plugin_robot_set_handler (plugin_user_t * robot,
						  plugin_event_handler_t * handler)
{
  user_t *u;
  plugin_private_t *priv;
  plugin_event_handler_t *old;

  u = ((plugin_private_t *) robot->private)->parent;
  priv = ((plugin_private_t *) u->plugin_priv);
  old = priv->handler;
  priv->handler = handler;

  return old;
}

/******************************* REQUEST EVENTS *******************************************/

int plugin_parse (plugin_user_t * user, buffer_t * buf)
{
  user_t *u;

  u = ((plugin_private_t *) user->private)->parent;

  ((plugin_private_t *) user->private)->proto->handle_token (u, buf);
  return 0;
}

/******************************* REQUEST EVENTS *******************************************/

int plugin_request (plugin_t * plugin, unsigned long event, plugin_event_handler_t * handler)
{
  plugin_event_request_t *request;
  plugin_manager_t *manager = plugin->manager;

  if (event > PLUGIN_EVENT_NUMBER)
    return -1;

  request = malloc (sizeof (plugin_event_request_t));
  memset (request, 0, sizeof (plugin_event_request_t));
  request->plugin = plugin;
  request->handler = handler;

  /* link it in the list */
  request->next = manager->eventhandles[event].next;
  request->next->prev = request;
  request->prev = &manager->eventhandles[event];
  manager->eventhandles[event].next = request;

  if (plugin)
    plugin->events++;

  return 0;
}

int plugin_ignore (plugin_t * plugin, unsigned long event, plugin_event_handler_t * handler)
{
  plugin_event_request_t *request;
  plugin_manager_t *manager = plugin->manager;

  for (request = manager->eventhandles[event].next; request != &manager->eventhandles[event];
       request = request->next)
    if ((request->plugin == plugin) && (request->handler == handler))
      break;

  if (!request)
    return 0;

  request->next->prev = request->prev;
  request->prev->next = request->next;

  free (request);
  if (plugin)
    plugin->events--;

  return 0;
}

/******************************* CLAIM/RELEASE *******************************************/

int plugin_claim (plugin_t * plugin, plugin_user_t * user, void *cntxt)
{
  plugin_private_t *priv = user->private;

  ASSERT (!priv->store[plugin->id]);
  priv->store[plugin->id] = cntxt;

  plugin->privates++;

  return 0;
}

int plugin_release (plugin_t * plugin, plugin_user_t * user)
{
  plugin_private_t *priv = user->private;

  priv->store[plugin->id] = NULL;

  plugin->privates--;

  return 0;
}

void *plugin_retrieve (plugin_t * plugin, plugin_user_t * user)
{
  plugin_private_t *priv = user->private;

  return priv->store[plugin->id];
}



/******************************* REGISTER/UNREGISTER *******************************************/

plugin_t *plugin_register (plugin_manager_t * manager, const char *name)
{
  plugin_t *plugin;

  plugin = malloc (sizeof (plugin_t));
  if (!plugin)
    return NULL;

  memset (plugin, 0, sizeof (plugin_t));
  strncpy ((char *) plugin->name, name, PLUGIN_NAME_LENGTH);
  ((char *) plugin->name)[PLUGIN_NAME_LENGTH - 1] = 0;

  plugin->id = pluginIDs++;

  /* link into dllink list */
  plugin->next = manager->plugins.next;
  plugin->prev = &manager->plugins;
  plugin->next->prev = plugin;
  plugin->manager = manager;
  manager->plugins.next = plugin;

  return plugin;
}

int plugin_unregister (plugin_t * plugin)
{
  plugin_private_t *p;
  plugin_manager_t *manager = plugin->manager;

  /* force clearing of all private data */
  for (p = manager->privates.next; p != &manager->privates; p = p->next)
    if (p->store[plugin->id]) {
      free (p->store[plugin->id]);
      p->store[plugin->id] = NULL;
      plugin->privates--;
    }

  /* verify everything is clean. */
  ASSERT (!plugin->privates);
  ASSERT (!plugin->events);
  ASSERT (!plugin->robots);

  /* unlink and free */
  plugin->next->prev = plugin->prev;
  plugin->prev->next = plugin->next;
  free (plugin);

  return 0;
}

/******************************* ENTRYPOINT *******************************************/

unsigned long plugin_user_event (plugin_t * plugin, plugin_user_t * user, unsigned long event,
				 void *token)
{
  return plugin_send_event (plugin->manager, ((plugin_private_t *) user->private), event, token);
}

unsigned long plugin_send_event (plugin_manager_t * manager, plugin_private_t * priv,
				 unsigned long event, void *token)
{
  unsigned long retval = PLUGIN_RETVAL_CONTINUE;
  plugin_event_request_t *r, *e;

  e = &manager->eventhandles[event];
  r = manager->eventhandles[event].next;
  if (priv) {
    if (priv->handler)
      retval = priv->handler (&priv->user, NULL, event, token);

    if (retval)
      return retval;

    for (; r != e; r = r->next) {
      retval = r->handler (&priv->user, priv->store[r->plugin->id], event, token);
      if (retval)
	break;
    }
  } else {
    for (; r != e; r = r->next) {
      retval = r->handler (NULL, NULL, event, token);
      if (retval)
	break;
    }
  }

  return retval;
}

/****************************** CORE USERMANAGEMENT ******************************************/

#include "nmdc_local.h"
unsigned long plugin_update_user (plugin_manager_t * manager, user_t * u)
{
  plugin_user_t *user;
  nmdc_user_t *nmdc;

  ASSERT (u);
  if (!u)
    return 0;

  user = USERT_TO_PLUGINUSER (u);

  if (manager->proto->name[0] == 'N')
    nmdc = (nmdc_user_t *) u;

  if ((user->share == u->share) &&
      (user->active == u->active) &&
      (!nmdc ||
       ((user->slots == nmdc->slots) &&
	(user->hubs[0] == nmdc->hubs[0]) &&
	(user->hubs[1] == nmdc->hubs[1]) && (user->hubs[2] == nmdc->hubs[2])
       )
      ) &&
      (user->ipaddress == u->ipaddress) &&
      (user->op == (u->rights & CAP_KEY)) &&
      (user->flags == u->flags) && (user->rights == u->rights))
    return 0;

  user->share = u->share;
  user->active = u->active;
  if (nmdc) {
    user->slots = nmdc->slots;
    user->hubs[0] = nmdc->hubs[0];
    user->hubs[1] = nmdc->hubs[1];
    user->hubs[2] = nmdc->hubs[2];
  }
  user->ipaddress = u->ipaddress;
  user->op = (u->rights & CAP_KEY);
  user->flags = u->flags;
  user->rights = u->rights;

  return plugin_send_event (manager, ((plugin_private_t *) u->plugin_priv), PLUGIN_EVENT_UPDATE,
			    NULL);
}


unsigned long plugin_new_user (plugin_manager_t * manager, plugin_private_t ** priv, user_t * u)
{

  ASSERT (!*priv);

  *priv = malloc (sizeof (plugin_private_t) + (sizeof (void *) * manager->num));
  memset (*priv, 0, sizeof (plugin_private_t));

  strncpy (((plugin_private_t *) u->plugin_priv)->user.nick, u->nick, NICKLENGTH);
  strncpy (((plugin_private_t *) u->plugin_priv)->user.client, u->client, 64);
  strncpy (((plugin_private_t *) u->plugin_priv)->user.versionstring, u->versionstring, 64);
  ((plugin_private_t *) u->plugin_priv)->user.version = u->version;

  (*priv)->parent = u;
  (*priv)->proto = manager->proto;
  (*priv)->user.private = *priv;
  (*priv)->store = (void *) (((char *) *priv) + sizeof (plugin_private_t));

  return plugin_update_user (manager, u);
}


unsigned long plugin_del_user (plugin_private_t ** priv)
{
  ASSERT (*priv);
  free (*priv);
  *priv = NULL;

  return 0;
}


/******************************* INIT *******************************************/

unsigned int plugin_config_save (plugin_t * plugin, buffer_t * output)
{
  int retval = 0;

  retval = config_save (ConfigFile);
  if (retval)
    bf_printf (output, _("Error saving configuration to %s: %s\n"), ConfigFile, strerror (retval));

  retval = accounts_save (AccountsFile);
  if (retval)
    bf_printf (output, _("Error saving configuration to %s: %s\n"), AccountsFile,
	       strerror (retval));

  retval = banlist_save (plugin->manager->proto->get_banlist_hard (), HardBanFile);
  if (retval)
    bf_printf (output, _("Error saving configuration to %s: %s\n"), HardBanFile, strerror (retval));

  retval = banlist_save (plugin->manager->proto->get_banlist_soft (), SoftBanFile);
  if (retval)
    bf_printf (output, _("Error saving configuration to %s: %s\n"), SoftBanFile, strerror (retval));

  plugin_send_event (plugin->manager, NULL, PLUGIN_EVENT_SAVE, output);

  return 0;
}

unsigned int plugin_config_load (plugin_t * plugin)
{
  config_load (ConfigFile);
  accounts_load (AccountsFile);
  banlist_clear (plugin->manager->proto->get_banlist_hard ());
  banlist_load (plugin->manager->proto->get_banlist_hard (), HardBanFile);
  banlist_clear (plugin->manager->proto->get_banlist_soft ());
  banlist_load (plugin->manager->proto->get_banlist_soft (), SoftBanFile);

  plugin_send_event (plugin->manager, NULL, PLUGIN_EVENT_LOAD, NULL);

  return 0;
}

/******************************* INIT *******************************************/

plugin_manager_t *plugin_init (proto_t * proto)
{
  int i;
  plugin_manager_t *manager;

  pluginIDs = 0;

  manager = malloc (sizeof (plugin_manager_t));
  memset (manager, 0, sizeof (plugin_manager_t));

  /* init dl lists */
  manager->plugins.next = &manager->plugins;
  manager->plugins.prev = &manager->plugins;
  manager->privates.next = &manager->privates;
  manager->privates.prev = &manager->privates;
  for (i = 0; i < PLUGIN_EVENT_NUMBER; i++) {
    manager->eventhandles[i].next = &(manager->eventhandles[i]);
    manager->eventhandles[i].prev = &(manager->eventhandles[i]);
  }
  manager->num = PLUGIN_MAX_PLUGINS;

  manager->proto = proto;

  ConfigFile = strdup (DEFAULT_SAVEFILE);
  HardBanFile = strdup (DEFAULT_HARDBANFILE);
  SoftBanFile = strdup (DEFAULT_SOFTBANFILE);
  AccountsFile = strdup (DEFAULT_ACCOUNTSFILE);

  return manager;
}
