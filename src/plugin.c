/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#include "plugin_int.h"
#include <sys/types.h>
#include <string.h>
#include <assert.h>

#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif

#include "hub.h"
#include "banlist.h"
#include "banlistnick.h"
#include "user.h"
#include "core_config.h"
#include "hashlist_func.h"

unsigned char *ConfigFile;
unsigned char *HardBanFile;
unsigned char *SoftBanFile;
unsigned char *NickBanFile;
unsigned char *AccountsFile;

extern user_t *HubSec;
extern hashlist_t hashlist;
extern user_t *userlist;

plugin_manager_t *manager;
unsigned long pluginIDs;


/******************************* UTILITIES: REPORING **************************************/

unsigned int plugin_report (buffer_t * message)
{
  user_t *u;
  config_element_t *target;

  target = config_find ("reporttarget");

  u = hash_find_nick (&hashlist, *target->val.v_string, strlen (*target->val.v_string));

  if (!u)
    return EINVAL;

  return ((plugin_private_t *) u->plugin_priv)->proto->chat_priv (HubSec, u, HubSec, message);
}

/******************************* UTILITIES: USER MANAGEMENT **************************************/

unsigned int plugin_user_next (plugin_user_t ** user)
{
  user_t *u = *user ? ((plugin_private_t *) (*user)->private)->parent->next : userlist;

  while (u && (u->state != PROTO_STATE_ONLINE))
    u = u->next;

  *user = u ? &((plugin_private_t *) u->plugin_priv)->user : NULL;

  return (*user != NULL);
}

plugin_user_t *plugin_user_find (unsigned char *name)
{
  user_t *u;

  u = hash_find_nick (&hashlist, name, strlen (name));
  if (!u)
    return NULL;

  return &((plugin_private_t *) u->plugin_priv)->user;
}

/******************************* UTILITIES: KICK/BAN **************************************/

unsigned int plugin_user_drop (plugin_user_t * user, buffer_t * message)
{
  user_t *u;

  if (!user)
    return 0;

  u = ((plugin_private_t *) user->private)->parent;

  if (u->state == PROTO_STATE_VIRTUAL)
    return 0;

  return ((plugin_private_t *) user->private)->proto->user_drop (u, message);
}

unsigned int plugin_user_kick (plugin_user_t * user, buffer_t * message)
{
  struct timeval now;
  user_t *u;

  if (!user)
    return 0;

  u = ((plugin_private_t *) user->private)->parent;

  if (u->state == PROTO_STATE_VIRTUAL)
    return 0;

  gettimeofday (&now, NULL);
  banlist_add (&softbanlist, u->ipaddress, message, now.tv_sec + config.defaultKickPeriod);
  banlist_nick_add (&nickbanlist, u->nick, message, now.tv_sec + config.defaultKickPeriod);

  return ((plugin_private_t *) user->private)->proto->user_redirect (u, message);
}

unsigned int plugin_user_banip (plugin_user_t * user, buffer_t * message, unsigned long period)
{
  struct timeval now;
  user_t *u;

  if (!user)
    return 0;

  u = ((plugin_private_t *) user->private)->parent;

  if (u->state == PROTO_STATE_VIRTUAL)
    return 0;

  gettimeofday (&now, NULL);
  banlist_add (&softbanlist, u->ipaddress, message, period ? now.tv_sec + period : 0);
  return ((plugin_private_t *) user->private)->proto->user_redirect (u, message);
}

unsigned int plugin_user_unban (plugin_user_t * user)
{
  struct timeval now;
  user_t *u;

  if (!user)
    return 0;

  u = ((plugin_private_t *) user->private)->parent;

  if (u->state == PROTO_STATE_VIRTUAL)
    return 0;

  gettimeofday (&now, NULL);
  banlist_nick_del_bynick (&nickbanlist, u->nick);
  return 0;
}

unsigned int plugin_user_zombie (plugin_user_t * user)
{
  user_t *u;

  if (!user)
    return 0;

  u = ((plugin_private_t *) user->private)->parent;

  user->flags |= PROTO_FLAG_ZOMBIE;
  u->flags |= PROTO_FLAG_ZOMBIE;

  return 0;
}

unsigned int plugin_unban (unsigned char *nick)
{
  if (!nick)
    return 0;

  return banlist_nick_del_bynick (&nickbanlist, nick);
}

unsigned int plugin_ban_ip (unsigned long ip, buffer_t * message, unsigned long period)
{
  struct timeval now;

  gettimeofday (&now, NULL);
  banlist_add (&softbanlist, ip, message, period ? now.tv_sec + period : 0);
  return 0;
}

unsigned int plugin_unban_ip (unsigned long ip)
{
  return banlist_del_byip (&softbanlist, ip);
}

unsigned int plugin_ban_nick (unsigned char *nick, buffer_t * message, unsigned long period)
{
  struct timeval now;

  gettimeofday (&now, NULL);
  banlist_nick_add (&nickbanlist, nick, message, period ? now.tv_sec + period : 0);
  return 0;
}

unsigned int plugin_unban_nick (unsigned char *nick)
{
  banlist_nick_del_bynick (&nickbanlist, nick);
  return 0;
}

unsigned int plugin_unban_ip_hard (unsigned long ip)
{
  banlist_del_byip (&hardbanlist, ip);
  return 0;
}

unsigned int plugin_ban_ip_hard (unsigned long ip, buffer_t * message, unsigned long period)
{
  struct timeval now;

  gettimeofday (&now, NULL);
  banlist_add (&hardbanlist, ip, message, period ? now.tv_sec + period : 0);
  return 0;
}

unsigned int plugin_user_banip_hard (plugin_user_t * user, buffer_t * message, unsigned long period)
{
  struct timeval now;
  user_t *u;

  if (!user)
    return 0;

  u = ((plugin_private_t *) user->private)->parent;

  if (u->state == PROTO_STATE_VIRTUAL)
    return 0;

  gettimeofday (&now, NULL);
  banlist_add (&hardbanlist, u->ipaddress, message, period ? now.tv_sec + period : 0);
  return ((plugin_private_t *) user->private)->proto->user_redirect (u, message);
}

unsigned int plugin_user_bannick (plugin_user_t * user, buffer_t * message, unsigned long period)
{
  struct timeval now;
  user_t *u;

  if (!user)
    return 0;

  u = ((plugin_private_t *) user->private)->parent;

  if (u->state == PROTO_STATE_VIRTUAL)
    return 0;

  gettimeofday (&now, NULL);
  banlist_nick_add (&nickbanlist, u->nick, message, period ? now.tv_sec + period : 0);
  return ((plugin_private_t *) user->private)->proto->user_redirect (u, message);
}

unsigned int plugin_user_ban (plugin_user_t * user, buffer_t * message, unsigned long period)
{
  struct timeval now;
  user_t *u;

  if (!user)
    return 0;

  u = ((plugin_private_t *) user->private)->parent;

  if (u->state == PROTO_STATE_VIRTUAL)
    return 0;

  gettimeofday (&now, NULL);
  banlist_nick_add (&nickbanlist, u->nick, message, period ? now.tv_sec + period : 0);
  banlist_add (&softbanlist, u->ipaddress, message, period ? now.tv_sec + period : 0);
  return ((plugin_private_t *) user->private)->proto->user_redirect (u, message);
}

unsigned int plugin_user_findnickban (buffer_t * buf, unsigned char *nick)
{
  struct timeval now;
  banlist_nick_entry_t *ne;

  ne = banlist_nick_find (&nickbanlist, nick);
  if (!ne)
    return 0;

  gettimeofday (&now, NULL);
  if (ne->expire) {
    return bf_printf (buf, "Found nick ban for %s for %lus because: %.*s", ne->nick,
		      ne->expire - now.tv_sec, bf_used (ne->message), ne->message->s);
  } else {
    return bf_printf (buf, "Found permanent nick ban for %s because: %.*s", ne->nick,
		      bf_used (ne->message), ne->message->s);
  }
}

unsigned int plugin_user_findipban (buffer_t * buf, unsigned long ip)
{
  struct timeval now;
  struct in_addr ipa;
  banlist_entry_t *ie;

  ie = banlist_find (&softbanlist, ip);
  if (!ie)
    return 0;

  gettimeofday (&now, NULL);
  ipa.s_addr = ip;
  if (ie->expire) {
    return bf_printf (buf, "Found IP ban for %s for %lus because: %.*s", inet_ntoa (ipa),
		      ie->expire - now.tv_sec, bf_used (ie->message), ie->message->s);
  } else {
    return bf_printf (buf, "Found permanent ban for %s because: %.*s", inet_ntoa (ipa),
		      bf_used (ie->message), ie->message->s);
  }
}

/******************************* UTILITIES: USER MANAGEMENT **************************************/

unsigned int plugin_user_redirect (plugin_user_t * user, buffer_t * message)
{
  user_t *u;

  if (!user)
    return 0;

  u = ((plugin_private_t *) user->private)->parent;

  if (u->state == PROTO_STATE_VIRTUAL)
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

  if (u->state == PROTO_STATE_VIRTUAL)
    return 0;

  if (!destination || !*destination)
    return ((plugin_private_t *) user->private)->proto->user_redirect (u, message);

  return ((plugin_private_t *) user->private)->proto->user_forcemove (u, destination, message);
}

/******************************* UTILITIES: USER MANAGEMENT **************************************/

unsigned int plugin_user_say (plugin_user_t * src, buffer_t * message)
{
  user_t *u;

  if (src) {
    u = ((plugin_private_t *) src->private)->parent;
  } else {
    u = HubSec;
  }

  /* delete trailing \n */
  if (bf_used (message) && (message->s[bf_used (message) - 1] == '\n')) {
    message->s[bf_used (message) - 1] = '\0';
    message->e--;
  }
  /* too much trouble otherwise FIXME huh ??? */
  ASSERT (u->state == PROTO_STATE_VIRTUAL);
  return ((plugin_private_t *) u->plugin_priv)->proto->chat_main (u, message);
}

unsigned int plugin_user_sayto (plugin_user_t * src, plugin_user_t * target, buffer_t * message)
{
  user_t *u, *t;

  if (src) {
    u = ((plugin_private_t *) src->private)->parent;
  } else {
    u = HubSec;
  }

  /* delete trailing \n */
  if (bf_used (message) && (message->s[bf_used (message) - 1] == '\n')) {
    message->s[bf_used (message) - 1] = '\0';
    message->e--;
  }

  t = ((plugin_private_t *) target->private)->parent;

  if (t->state == PROTO_STATE_VIRTUAL)
    return 0;

  /* too much trouble otherwise FIXME huh ??? */
  ASSERT (u->state == PROTO_STATE_VIRTUAL);

  return ((plugin_private_t *) u->plugin_priv)->proto->chat_send (u, t, message);
}

unsigned int plugin_user_priv (plugin_user_t * src, plugin_user_t * target, plugin_user_t * user,
			       buffer_t * message, int direct)
{
  user_t *u, *t, *s;

  if (src) {
    u = ((plugin_private_t *) src->private)->parent;
  } else {
    u = HubSec;
  }

  /* delete trailing \n */
  if (bf_used (message) && (message->s[bf_used (message) - 1] == '\n')) {
    message->s[bf_used (message) - 1] = '\0';
    message->e--;
  }

  t = ((plugin_private_t *) target->private)->parent;
  if (user) {
    s = ((plugin_private_t *) user->private)->parent;
  } else {
    s = HubSec;
  }

  /* too much trouble otherwise */
  ASSERT (u->state == PROTO_STATE_VIRTUAL);
  return direct ? ((plugin_private_t *) target->private)->proto->chat_priv_direct (u, t, s, message)
    : ((plugin_private_t *) target->private)->proto->chat_priv (u, t, s, message);
}

unsigned int plugin_user_printf (plugin_user_t * user, const char *format, ...)
{
  va_list ap;
  buffer_t *buf;

  buf = bf_alloc (1024);

  va_start (ap, format);
  bf_vprintf (buf, format, ap);
  va_end (ap);

  plugin_user_sayto (NULL, user, buf);

  bf_free (buf);

  return 0;
}

/******************************* ROBOTS EVENTS *******************************************/
/* FIXME */
extern proto_t nmdc_proto;
plugin_user_t *plugin_robot_add (unsigned char *name, unsigned char *description,
				 plugin_event_handler_t * handler)
{
  user_t *u;
  plugin_private_t *priv;

  u = nmdc_proto.robot_add (name, description);
  plugin_new_user ((plugin_private_t **) & u->plugin_priv, u, &nmdc_proto);

  priv = ((plugin_private_t *) u->plugin_priv);
  priv->handler = handler;

  return &priv->user;
}



int plugin_robot_remove (plugin_user_t * robot)
{
  user_t *u;

  u = ((plugin_private_t *) robot->private)->parent;

  /* does the plugin_del_user */
  ((plugin_private_t *) robot->private)->proto->robot_del (u);

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

int plugin_ignore (plugin_t * plugin, unsigned long event)
{
  plugin_event_request_t *request;

  for (request = manager->eventhandles[event].next; request != &manager->eventhandles[event];
       request = request->next)
    if (request->plugin == plugin)
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

plugin_t *plugin_register (const char *name)
{
  plugin_t *plugin;

  plugin = malloc (sizeof (plugin_t));
  if (!plugin)
    return NULL;

  memset (plugin, 0, sizeof (plugin_t));
  strncpy ((char *) plugin->name, name, PLUGIN_NAME_LENGTH);

  plugin->id = pluginIDs++;

  /* link into dllink list */
  plugin->next = manager->plugins.next;
  plugin->prev = &manager->plugins;
  plugin->next->prev = plugin;
  manager->plugins.next = plugin;

  return plugin;
}

int plugin_unregister (plugin_t * plugin)
{
  plugin_private_t *p;

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

unsigned long plugin_user_event (plugin_user_t * user, unsigned long event, void *token)
{
  return plugin_send_event (((plugin_private_t *) user->private), event, token);
}

unsigned long plugin_send_event (plugin_private_t * priv, unsigned long event, void *token)
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

unsigned long plugin_update_user (user_t * u)
{
  if ((((plugin_private_t *) u->plugin_priv)->user.share == u->share) &&
      (((plugin_private_t *) u->plugin_priv)->user.active == u->active) &&
      (((plugin_private_t *) u->plugin_priv)->user.slots == u->slots) &&
      (((plugin_private_t *) u->plugin_priv)->user.hubs[0] == u->hubs[0]) &&
      (((plugin_private_t *) u->plugin_priv)->user.hubs[1] == u->hubs[1]) &&
      (((plugin_private_t *) u->plugin_priv)->user.hubs[2] == u->hubs[2]) &&
      (((plugin_private_t *) u->plugin_priv)->user.ipaddress == u->ipaddress) &&
      (((plugin_private_t *) u->plugin_priv)->user.op == u->op) &&
      (((plugin_private_t *) u->plugin_priv)->user.flags == u->flags) &&
      (((plugin_private_t *) u->plugin_priv)->user.rights == u->rights))
    return 0;

  ((plugin_private_t *) u->plugin_priv)->user.share = u->share;
  ((plugin_private_t *) u->plugin_priv)->user.active = u->active;
  ((plugin_private_t *) u->plugin_priv)->user.slots = u->slots;
  ((plugin_private_t *) u->plugin_priv)->user.hubs[0] = u->hubs[0];
  ((plugin_private_t *) u->plugin_priv)->user.hubs[1] = u->hubs[1];
  ((plugin_private_t *) u->plugin_priv)->user.hubs[2] = u->hubs[2];
  ((plugin_private_t *) u->plugin_priv)->user.ipaddress = u->ipaddress;
  ((plugin_private_t *) u->plugin_priv)->user.op = u->op;
  ((plugin_private_t *) u->plugin_priv)->user.flags = u->flags;
  ((plugin_private_t *) u->plugin_priv)->user.rights = u->rights;

  return plugin_send_event (((plugin_private_t *) u->plugin_priv), PLUGIN_EVENT_UPDATE, NULL);
}


unsigned long plugin_new_user (plugin_private_t ** priv, user_t * u, proto_t * p)
{

  ASSERT (!*priv);

  *priv = malloc (sizeof (plugin_private_t) + (sizeof (void *) * manager->num));
  memset (*priv, 0, sizeof (plugin_private_t));

  strncpy (((plugin_private_t *) u->plugin_priv)->user.nick, u->nick, NICKLENGTH);
  strncpy (((plugin_private_t *) u->plugin_priv)->user.client, u->client, 64);
  strncpy (((plugin_private_t *) u->plugin_priv)->user.versionstring, u->versionstring, 64);
  ((plugin_private_t *) u->plugin_priv)->user.version = u->version;

  (*priv)->parent = u;
  (*priv)->proto = p;
  (*priv)->user.private = *priv;
  (*priv)->store = (void *) (((char *) *priv) + sizeof (plugin_private_t));

  return plugin_update_user (u);
}


unsigned long plugin_del_user (plugin_private_t ** priv)
{
  ASSERT (*priv);
  free (*priv);
  *priv = NULL;

  return 0;
}


/******************************* INIT *******************************************/

unsigned int plugin_config_save (buffer_t * output)
{
  int retval = 0;

  retval = config_save (ConfigFile);
  if (retval)
    bf_printf (output, "Error saving configuration to %s: %s\n", ConfigFile, strerror (retval));

  retval = accounts_save (AccountsFile);
  if (retval)
    bf_printf (output, "Error saving configuration to %s: %s\n", AccountsFile, strerror (retval));

  retval = banlist_save (&hardbanlist, HardBanFile);
  if (retval)
    bf_printf (output, "Error saving configuration to %s: %s\n", HardBanFile, strerror (retval));

  retval = banlist_save (&softbanlist, SoftBanFile);
  if (retval)
    bf_printf (output, "Error saving configuration to %s: %s\n", SoftBanFile, strerror (retval));

  retval = banlist_nick_save (&nickbanlist, NickBanFile);
  if (retval)
    bf_printf (output, "Error saving configuration to %s: %s\n", NickBanFile, strerror (retval));


  plugin_send_event (NULL, PLUGIN_EVENT_SAVE, output);

  return 0;
}

unsigned int plugin_config_load ()
{
  config_load (ConfigFile);
  accounts_load (AccountsFile);
  banlist_load (&hardbanlist, HardBanFile);
  banlist_load (&softbanlist, SoftBanFile);
  banlist_nick_load (&nickbanlist, NickBanFile);

  plugin_send_event (NULL, PLUGIN_EVENT_LOAD, NULL);

  return 0;
}

/******************************* INIT *******************************************/

int plugin_init ()
{
  int i;

  pluginIDs = 0;

  manager = malloc (sizeof (plugin_manager_t));
  memset (manager, 0, sizeof (plugin_manager_t));

  /* init dl lists */
  manager->plugins.next = &manager->plugins;
  manager->plugins.prev = &manager->plugins;
  manager->privates.next = &manager->privates;
  manager->privates.prev = &manager->privates;
  for (i = 0; i < PLUGIN_EVENT_NUMBER; i++) {
    manager->eventhandles[i].next = &manager->eventhandles[i];
    manager->eventhandles[i].prev = &manager->eventhandles[i];
  }
  manager->num = PLUGIN_MAX_PLUGINS;

  ConfigFile = strdup (DEFAULT_SAVEFILE);
  HardBanFile = strdup (DEFAULT_HARDBANFILE);
  SoftBanFile = strdup (DEFAULT_SOFTBANFILE);
  NickBanFile = strdup (DEFAULT_NICKBANFILE);
  AccountsFile = strdup (DEFAULT_ACCOUNTSFILE);

  return 0;
}
