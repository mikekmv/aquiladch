/*
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)    
 *  (C) Copyright 2006 Toma "ZeXx86" Jedrzejek (admin@infern0.tk)
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.
 *
 *  Thanks to Toma "ZeXx86" Jedrzejek (admin@infern0.tk) for original implementation
 */

#include "plugin_int.h"
#include <sys/types.h>
#include <string.h>
#include <assert.h>

#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif

#include <stdlib.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <math.h>

#include "utils.h"
#include "commands.h"


const unsigned char *pi_lua_eventnames[] = {
  "EventLogin",
  "EventSearch",
  "EventChat",
  "EventPMOut",
  "EventPMIn",
  "EventLogout",
  "EventKick",
  "EventBan",
  "EventInfoUpdate",
  "EventSearchResult",
  "EventUpdate",
  "EventRedirect",
  "EventPreLogin",
  "EventCacheFlush",
  "EventLoad",
  "EventSave",
  "EventConfig",
  NULL
};


plugin_t *plugin_lua = NULL;

unsigned int breakmainchat = 0;

unsigned int lua_ctx_cnt, lua_ctx_peak;

typedef struct lua_context {
  struct lua_context *next, *prev;

  lua_State *l;
  unsigned char *name;
  unsigned long long eventmap;
} lua_context_t;

lua_context_t lua_list;

int lua_arrival (const char *command, unsigned int args, const char *nick, const char *data);
unsigned int plugin_lua_close (char name[256]);


/******************************* LUA INTERFACE *******************************************/

/******************************************************************************************
 *  LUA Function Configuration handling
 */

int pi_lua_getconfig (lua_State * lua)
{
  config_element_t *elem;

  char *name = (char *) luaL_checkstring (lua, 1);

  elem = config_find (name);
  if (!elem) {
    // push error message
    lua_pushstring (lua, "Unknown config value.");
    // flag error, this function never returns.
    lua_error (lua);
  }

  switch (elem->type) {
    case CFG_ELEM_LONG:
      lua_pushnumber (lua, *elem->val.v_long);
      break;
    case CFG_ELEM_ULONG:
      lua_pushnumber (lua, *elem->val.v_ulong);
      break;
    case CFG_ELEM_ULONGLONG:
      lua_pushnumber (lua, *elem->val.v_ulonglong);
      break;
    case CFG_ELEM_INT:
      lua_pushnumber (lua, *elem->val.v_int);
      break;
    case CFG_ELEM_UINT:
      lua_pushnumber (lua, *elem->val.v_uint);
      break;
    case CFG_ELEM_DOUBLE:
      lua_pushnumber (lua, *elem->val.v_double);
      break;

    case CFG_ELEM_STRING:
      lua_pushstring (lua, *elem->val.v_string);
      break;

    case CFG_ELEM_IP:
      {
	struct in_addr in;

	in.s_addr = *elem->val.v_ip;
	lua_pushstring (lua, inet_ntoa (in));
	break;
      }
    case CFG_ELEM_BYTESIZE:
      lua_pushstring (lua, format_size (*elem->val.v_ulonglong));
      break;

    case CFG_ELEM_CAP:
    case CFG_ELEM_PTR:
      lua_pushstring (lua, "Element Type not supported.");
      lua_error (lua);
      break;
  };

  return 1;
}

int pi_lua_setconfig (lua_State * lua)
{
  config_element_t *elem;

  char *name = (char *) luaL_checkstring (lua, 1);
  char *value = (char *) luaL_checkstring (lua, 2);

  elem = config_find (name);
  if (!elem) {
    // push error message
    lua_pushstring (lua, "Unknown config value.");
    // flag error, this function never returns.
    lua_error (lua);
  }

  switch (elem->type) {
    case CFG_ELEM_LONG:
      sscanf (value, "%ld", elem->val.v_long);
      break;
    case CFG_ELEM_ULONG:
      sscanf (value, "%lu", elem->val.v_ulong);
      break;
    case CFG_ELEM_ULONGLONG:
      sscanf (value, "%Lu", elem->val.v_ulonglong);
      break;
    case CFG_ELEM_INT:
      sscanf (value, "%d", elem->val.v_int);
      break;
    case CFG_ELEM_UINT:
      sscanf (value, "%u", elem->val.v_uint);
      break;
    case CFG_ELEM_DOUBLE:
      sscanf (value, "%lf", elem->val.v_double);
      break;
    case CFG_ELEM_STRING:
      if (*elem->val.v_string)
	free (*elem->val.v_string);
      *elem->val.v_string = strdup (value);
      break;
    case CFG_ELEM_IP:
      {
	struct in_addr ia;

	if (!inet_aton (value, &ia)) {
	  lua_pushstring (lua, "Not a valid IP address.\n");
	  lua_error (lua);
	}
	*elem->val.v_ip = ia.s_addr;
      }
      break;
    case CFG_ELEM_BYTESIZE:
      *elem->val.v_ulonglong = parse_size (value);
      break;

    case CFG_ELEM_PTR:
    case CFG_ELEM_CAP:
    default:
      lua_pushstring (lua, "Element Type not supported.");
      lua_error (lua);
  }

  return 1;
}


/******************************************************************************************
 *  LUA Function IO Handling
 */
int pi_lua_user_printf (lua_State * lua)
{
  plugin_user_t *user;

  // read first param/argument with local Lua buffer
  const char *nick = luaL_checkstring (lua, 1);
  const char *format = luaL_checkstring (lua, 2);

  user = plugin_user_find ((char *) nick);

  if (user)
    plugin_user_printf (user, format);

  return 0;
}

int pi_lua_user_say (lua_State * lua)
{
  plugin_user_t *user;
  buffer_t *buf;

  // read first param/argument with local Lua buffer
  const char *nick = luaL_checkstring (lua, 1);
  const char *format = luaL_checkstring (lua, 2);

  user = plugin_user_find ((char *) nick);

  if (!user)
    return 0;

  buf = bf_alloc (10240);

  bf_printf (buf, format);

  plugin_user_say (user, buf);

  bf_free (buf);

  return 0;
}

int lua_bot_say (lua_State * lua)
{
  buffer_t *buf;

  // read first param/argument with local Lua buffer
  const char *format = luaL_checkstring (lua, 1);

  buf = bf_alloc (10240);

  bf_printf (buf, format);

  plugin_user_say (NULL, buf);

  bf_free (buf);

  return 0;
}

int pi_lua_user_sayto (lua_State * lua)
{
  plugin_user_t *src, *target;
  buffer_t *buf;

  // read first param/argument with local Lua buffer
  const char *targetnick = luaL_checkstring (lua, 1);
  const char *srcnick = luaL_checkstring (lua, 2);
  const char *format = luaL_checkstring (lua, 3);

  src = plugin_user_find ((char *) srcnick);
  target = plugin_user_find ((char *) targetnick);

  if (!src || !target)
    return 0;

  buf = bf_alloc (10240);

  bf_printf (buf, format);

  plugin_user_sayto (src, target, buf);

  bf_free (buf);

  return 0;
}


int lua_bot_privto (lua_State * lua)
{
  plugin_user_t *tgt;
  buffer_t *buf;

  // read first param/argument with local Lua buffer
  const char *targetnick = luaL_checkstring (lua, 1);
  const char *format = luaL_checkstring (lua, 2);

  tgt = plugin_user_find ((char *) targetnick);

  if (!tgt)
    return 0;

  buf = bf_alloc (10240);

  bf_printf (buf, format);

  plugin_user_priv (NULL, tgt, NULL, buf, 1);

  bf_free (buf);

  return 0;
}

int pi_lua_user_privto (lua_State * lua)
{
  plugin_user_t *tgt, *user;
  buffer_t *buf;

  // read first param/argument with local Lua buffer
  const char *targetnick = luaL_checkstring (lua, 1);
  const char *srcnick = luaL_checkstring (lua, 2);
  const char *format = luaL_checkstring (lua, 3);

  tgt = plugin_user_find ((char *) targetnick);
  user = plugin_user_find ((char *) srcnick);

  if (!tgt)
    return 0;

  buf = bf_alloc (10240);

  bf_printf (buf, format);

  if (user)
    plugin_user_priv (user, tgt, user, buf, 1);
  else
    plugin_user_priv (NULL, tgt, NULL, buf, 1);

  bf_free (buf);

  return 0;
}


int pi_lua_user_privall (lua_State * lua)
{
  plugin_user_t *tgt = NULL, *user;
  buffer_t *buf;

  // read first param/argument with local Lua buffer
  const char *srcnick = luaL_checkstring (lua, 1);
  const char *format = luaL_checkstring (lua, 2);

  user = plugin_user_find ((char *) srcnick);
  if (!user)
    return 0;

  buf = bf_alloc (10240);

  bf_printf (buf, format);

  while (plugin_user_next (&tgt)) {
    plugin_user_priv (user, tgt, user, buf, 1);
  }

  bf_free (buf);

  return 0;
}

int lua_bot_privall (lua_State * lua)
{
  plugin_user_t *tgt = NULL;
  buffer_t *buf;

  // read first param/argument with local Lua buffer
  const char *format = luaL_checkstring (lua, 1);

  buf = bf_alloc (10240);

  bf_printf (buf, format);

  while (plugin_user_next (&tgt))
    plugin_user_priv (NULL, tgt, NULL, buf, 1);

  bf_free (buf);

  return 0;
}

/******************************************************************************************
 *  LUA Function User data handling
 */


extern long users_total;
int lua_getuserstotal (lua_State * lua)
{
  lua_pushnumber (lua, users_total);

  return 1;
}

int lua_getuserip (lua_State * lua)
{
  const char *nick = luaL_checkstring (lua, 1);
  plugin_user_t *user;

  user = plugin_user_find ((char *) nick);

  if (!user) {
    lua_pushnil (lua);
  } else {
    struct in_addr in;

    in.s_addr = user->ipaddress;
    lua_pushstring (lua, inet_ntoa (in));
  }

  return 1;
}

int lua_getusershare (lua_State * lua)
{
  const char *nick = luaL_checkstring (lua, 1);
  plugin_user_t *user;

  user = plugin_user_find ((char *) nick);

  if (!user) {
    lua_pushnil (lua);
  } else {
    lua_pushstring (lua, format_size (user->share));
  }

  return 1;
}

int lua_getuserclient (lua_State * lua)
{
  const char *nick = luaL_checkstring (lua, 1);
  plugin_user_t *user;

  user = plugin_user_find ((char *) nick);

  if (!user) {
    lua_pushnil (lua);
  } else {
    lua_pushstring (lua, user->client);
  }
  return 1;
}

int lua_getuserclientversion (lua_State * lua)
{
  const char *nick = luaL_checkstring (lua, 1);
  plugin_user_t *user;

  user = plugin_user_find ((char *) nick);

  if (!user) {
    lua_pushnil (lua);
  } else {
    lua_pushstring (lua, user->versionstring);
  }

  return 1;
}

int pi_lua_userisop (lua_State * lua)
{
  char *nick = (char *) luaL_checkstring (lua, 1);
  plugin_user_t *user;

  user = plugin_user_find (nick);

  if (!user) {
    lua_pushnil (lua);
  } else {
    lua_pushboolean (lua, user->op);
  }

  return 1;
}

/******************************************************************************************
 *  LUA Functions Bot handling
 */


int lua_addbot (lua_State * lua)
{
  plugin_user_t *u;
  const char *nick = luaL_checkstring (lua, 1);
  const char *description = luaL_checkstring (lua, 2);

  u = plugin_robot_add ((char *) nick, (char *) description, NULL);

  lua_pushboolean (lua, (u != NULL));

  return 1;
}

int lua_delbot (lua_State * lua)
{
  const char *nick = luaL_checkstring (lua, 1);
  plugin_user_t *u;

  u = plugin_user_find ((char *) nick);

  if (u)
    plugin_robot_remove (u);

  lua_pushboolean (lua, (u != NULL));

  return 1;
}

/******************************************************************************************
 *  LUA Functions String utilities
 */


/* search string */
int lua_strsearch (lua_State * lua)
{
  const char *str = luaL_checkstring (lua, 1);
  const char *str2 = luaL_checkstring (lua, 2);

  const char *buf = strstr (str, str2);

  if (buf != NULL) {
    lua_pushstring (lua, buf);
  } else {
    lua_pushnumber (lua, 0);
  }

  return 1;
}

/******************************************************************************************
 *  LUA Functions IO handling
 */

/******************************************************************************************
 *  LUA Functions User handling
 */

/* disconnect user */
int lua_disconnectbyname (lua_State * lua)
{
  const char *nick = luaL_checkstring (lua, 1);

  plugin_user_t *user;

  user = plugin_user_find ((char *) nick);

  if (user)
    plugin_user_drop (user, bf_buffer (""));

  return 0;
}

/* kick user */
int lua_kickbyname (lua_State * lua)
{
  buffer_t *buf;
  const char *nick = luaL_checkstring (lua, 1);
  const char *str = luaL_checkstring (lua, 2);

  plugin_user_t *user;

  user = plugin_user_find ((char *) nick);

  if (!user)
    return 0;


  /* rebuild reason */
  buf = bf_alloc (1024);
  *buf->e = '\0';

  bf_printf (buf, "You were kicked because: %s", str);

  plugin_user_kick (user, buf);

  return 0;
}


/* banip user */
int lua_banip (lua_State * lua)
{
  buffer_t *buf;
  const char *nick = luaL_checkstring (lua, 1);
  const char *str = luaL_checkstring (lua, 2);
  const char *str2 = luaL_checkstring (lua, 3);

  unsigned long period = 0;

  plugin_user_t *user;

  user = plugin_user_find ((char *) nick);

  if (!user)
    return 0;


  /* rebuild reason */
  buf = bf_alloc (1024);
  *buf->e = '\0';
  bf_printf (buf, str);

  period = time_parse ((char *) str2);
  plugin_user_banip (user, buf, period);

  return 0;
}

/* bannick user */
int lua_bannick (lua_State * lua)
{
  buffer_t *buf;
  const char *nick = luaL_checkstring (lua, 1);
  const char *str = luaL_checkstring (lua, 2);
  const char *str2 = luaL_checkstring (lua, 3);

  unsigned long period = 0;

  plugin_user_t *user;

  user = plugin_user_find ((char *) nick);

  if (!user)
    return 0;


  /* rebuild reason */
  buf = bf_alloc (1024);
  *buf->e = '\0';
  bf_printf (buf, str);

  period = time_parse ((char *) str2);
  plugin_user_bannick (user, buf, period);

  return 0;
}

/* ban user */
int lua_ban (lua_State * lua)
{
  buffer_t *buf;
  const char *nick = luaL_checkstring (lua, 1);
  const char *str = luaL_checkstring (lua, 2);
  const char *str2 = luaL_checkstring (lua, 3);

  unsigned long period = 0;

  plugin_user_t *user;

  user = plugin_user_find ((char *) nick);

  if (!user)
    return 0;

  /* rebuild reason */
  buf = bf_alloc (1024);
  *buf->e = '\0';
  bf_printf (buf, str);

  period = time_parse ((char *) str2);
  plugin_user_ban (user, buf, period);

  return 0;
}

/* baniphard user */
int lua_baniphard (lua_State * lua)
{
  buffer_t *buf;

  const char *nick = luaL_checkstring (lua, 1);
  const char *str = luaL_checkstring (lua, 2);
  const char *str2 = luaL_checkstring (lua, 3);

  unsigned long period = 0;

  plugin_user_t *user;

  user = plugin_user_find ((char *) nick);

  if (!user)
    return 0;

  /* rebuild reason */
  buf = bf_alloc (1024);
  *buf->e = '\0';
  bf_printf (buf, str);

  period = time_parse ((char *) str2);
  plugin_user_banip_hard (user, buf, period);

  return 0;
}


/* unbanip user */
int lua_unbanip (lua_State * lua)
{
  struct in_addr ia;

  char *value = (char *) luaL_checkstring (lua, 1);

  if (!inet_aton (value, &ia)) {
    lua_pushstring (lua, "Not a valid IP address.\n");
    lua_error (lua);
  }

  plugin_unban_ip (ia.s_addr);

  return 0;
}

/* unbannick user */
int lua_unbannick (lua_State * lua)
{
  const char *nick = luaL_checkstring (lua, 1);

  plugin_unban_nick ((char *) nick);

  return 0;
}

/* unban user */
int lua_unban (lua_State * lua)
{
  const char *nick = luaL_checkstring (lua, 1);

  plugin_unban ((char *) nick);

  return 0;
}

/* unbaniphard user */
int lua_unbaniphard (lua_State * lua)
{
  struct in_addr ia;

  char *value = (char *) luaL_checkstring (lua, 1);

  if (!inet_aton (value, &ia)) {
    lua_pushstring (lua, "Not a valid IP address.\n");
    lua_error (lua);
  }

  plugin_unban_ip_hard (ia.s_addr);

  return 0;
}

/* gag user */
int lua_gag (lua_State * lua)
{
  const char *nick = luaL_checkstring (lua, 1);

  plugin_user_t *user;

  user = plugin_user_find ((char *) nick);

  if (!user)
    return 0;

  plugin_user_zombie (user);

  return 0;
}

/* ungag user */
int lua_ungag (lua_State * lua)
{
  const char *nick = luaL_checkstring (lua, 1);

  plugin_user_t *user;

  user = plugin_user_find ((char *) nick);

  if (!user)
    return 0;

  plugin_user_unzombie (user);

  return 0;
}

/* get a user group */
int lua_getusergroup (lua_State * lua)
{
  const char *nick = luaL_checkstring (lua, 1);

  plugin_user_t *user;

  user = plugin_user_find ((char *) nick);

  if (!user)
    return 0;

  lua_pushnumber (lua, user->rights);

  return 0;
}

/* forcemove user */
int lua_forcemove (lua_State * lua)
{
  const char *nick = luaL_checkstring (lua, 1);
  const char *str = luaL_checkstring (lua, 2);
  const char *str2 = luaL_checkstring (lua, 3);

  buffer_t *buf;

  plugin_user_t *user;

  user = plugin_user_find ((char *) nick);

  if (!user)
    return 0;

  /* rebuild reason */
  buf = bf_alloc (1024);
  *buf->e = '\0';
  bf_printf (buf, str2);

  plugin_user_forcemove (user, (char *) str, buf);

  return 0;
}

/******************************************************************************************
 *  LUA Functions Custom Command Handling
 */

typedef struct pi_lua_command_context {
  struct pi_lua_command_context *next, *prev;

  unsigned char *name;
  lua_State *lua;
} pi_lua_command_context_t;

pi_lua_command_context_t pi_lua_command_list;

unsigned long handler_luacommand (plugin_user_t * user, buffer_t * output, void *priv,
				  unsigned int argc, unsigned char **argv)
{
  unsigned int i, result;
  pi_lua_command_context_t *ctx;

  /* find command */
  for (ctx = pi_lua_command_list.next; ctx != &pi_lua_command_list; ctx = ctx->next)
    if (!strcmp (ctx->name, argv[0]))
      break;

  /* registered command that was deleted? */
  ASSERT (ctx->name);

  lua_settop (ctx->lua, 0);

  /* specify function to call */
  /* lua 5.1 
     lua_getfield (ctx->lua, LUA_GLOBALSINDEX, command);
   */

  /* lua 5.0 */
  lua_pushstring (ctx->lua, ctx->name);
  lua_gettable (ctx->lua, LUA_GLOBALSINDEX);

  /* push arguments */
  lua_pushstring (ctx->lua, user->nick);
  for (i = 1; i < argc; i++)
    lua_pushstring (ctx->lua, argv[i]);

  /* call funtion. */
  result = lua_pcall (ctx->lua, argc, 1, 0);
  if (result) {
    char *error = (char *) luaL_checkstring (ctx->lua, 1);
    buffer_t *buf;

    DPRINTF ("LUA ERROR: %s\n", error);

    buf = bf_alloc (32 + strlen (error) + strlen (ctx->name));

    bf_printf (buf, "LUA ERROR ('%s'): %s\n", ctx->name, error);

    plugin_report (buf);

    bf_free (buf);
  }

  return 0;
}

/* lua reg command */
int lua_cmdreg (lua_State * lua)
{
  pi_lua_command_context_t *ctx;

  const char *cmd = luaL_checkstring (lua, 1);
  const char *rights = luaL_checkstring (lua, 2);	// FIXME
  const char *desc = luaL_checkstring (lua, 3);

  lua_pushstring (lua, cmd);
  lua_gettable (lua, LUA_GLOBALSINDEX);
  if (lua_isnil (lua, -1)) {
    lua_remove (lua, -1);
    lua_pushstring (lua, "No such LUA function.");
    return lua_error (lua);
  }
  lua_remove (lua, -1);

  if (command_register ((char *) cmd, &handler_luacommand, 0, (char *) desc)) {
    lua_pushstring (lua, "Command failed to register.");
    return lua_error (lua);
  }

  /* alloc and init */
  ctx = malloc (sizeof (pi_lua_command_context_t));
  if (!ctx) {
    command_unregister ((char *) cmd);
    lua_pushstring (lua, "Could not allocate memory.");
    return lua_error (lua);
  }
  ctx->name = strdup (cmd);
  ctx->lua = lua;

  /* link in list */
  ctx->next = &pi_lua_command_list;
  ctx->prev = pi_lua_command_list.prev;
  ctx->next->prev = ctx;
  ctx->prev->next = ctx;

  lua_pushboolean (lua, 1);

  return 1;
}

/* lua del command */
int lua_cmddel (lua_State * lua)
{
  pi_lua_command_context_t *ctx;
  const char *cmd = luaL_checkstring (lua, 1);

  for (ctx = pi_lua_command_list.next; ctx != &pi_lua_command_list; ctx = ctx->next) {
    if ((lua != ctx->lua) || (strcmp (ctx->name, cmd)))
      continue;

    ctx->prev->next = ctx->next;
    ctx->next->prev = ctx->prev;

    command_unregister ((char *) cmd);

    free (ctx->name);
    free (ctx);

    break;
  }

  return 0;
}

/* clean out all command of this script */
int lua_cmdclean (lua_State * lua)
{
  pi_lua_command_context_t *ctx;

  for (ctx = pi_lua_command_list.next; ctx != &pi_lua_command_list; ctx = ctx->next) {
    if (lua != ctx->lua)
      continue;

    ctx->prev->next = ctx->next;
    ctx->next->prev = ctx->prev;

    command_unregister (ctx->name);

    free (ctx->name);
    free (ctx);

    ctx = pi_lua_command_list.next;
  }

  return 0;
}

/******************************************************************************************
 *  LUA Function registration
 */

int luaregister (lua_State * l)
{
  lua_register (l, "SendToNick", pi_lua_user_printf);
  lua_register (l, "SendBotToAll", lua_bot_say);
  lua_register (l, "SendToAll", pi_lua_user_say);
  lua_register (l, "SendNickToNick", pi_lua_user_sayto);
  lua_register (l, "SendPmToNick", pi_lua_user_privto);
  lua_register (l, "SendBotPmToNick", lua_bot_privto);
  lua_register (l, "SendPmToAll", pi_lua_user_privall);
  lua_register (l, "SendBotPmToAll", lua_bot_privall);

  lua_register (l, "GetActualUsersTotal", lua_getuserstotal);
  lua_register (l, "GetUserIP", lua_getuserip);
  lua_register (l, "GetUserShare", lua_getusershare);
  lua_register (l, "GetUserGroup", lua_getusergroup);
  lua_register (l, "GetUserClient", lua_getuserclient);
  lua_register (l, "GetUserClientVer", lua_getuserclientversion);
  lua_register (l, "UserIsOP", pi_lua_userisop);

  lua_register (l, "AddBot", lua_addbot);
  lua_register (l, "DelBot", lua_delbot);
  lua_register (l, "RegCommand", lua_cmdreg);
  lua_register (l, "DelCommand", lua_cmddel);

  lua_register (l, "SearchString", lua_strsearch);

  lua_register (l, "DisconnectByName", lua_disconnectbyname);
  lua_register (l, "KickByName", lua_kickbyname);
  lua_register (l, "BanIP", lua_banip);
  lua_register (l, "BanNick", lua_bannick);
  lua_register (l, "Ban", lua_ban);
  lua_register (l, "BanIPHard", lua_baniphard);
  lua_register (l, "UnBanIP", lua_unbanip);
  lua_register (l, "UnBanNick", lua_unbannick);
  lua_register (l, "UnBan", lua_unban);
  lua_register (l, "UnBanIPHard", lua_unbaniphard);
  lua_register (l, "ForceMove", lua_forcemove);

  lua_register (l, "Gag", lua_gag);
  lua_register (l, "UnGag", lua_ungag);

  lua_register (l, "SetConfig", pi_lua_setconfig);
  lua_register (l, "GetConfig", pi_lua_getconfig);

  return 1;
}

/***************************************************************************************/

/******************************************************************************************
 * Lua libraries that we are going to load.
 */
static const luaL_reg lualibs[] = {
  {"base", luaopen_base},
  {"table", luaopen_table},
  {"io", luaopen_io},
  {"string", luaopen_string},
  {"math", luaopen_math},
  {"debug", luaopen_debug},
  {"loadlib", luaopen_loadlib},
  {NULL, NULL}
};

/*
 * Loads the above mentioned libraries.
 */
static void openlualibs (lua_State * l)
{
  const luaL_reg *lib;

  for (lib = lualibs; lib->func != NULL; lib++) {
    lib->func (l);		/* Open the library */
    /* 
     * Flush the stack, by setting the top to 0, in order to
     * ignore any result given by the library load function.
     */
    lua_settop (l, 0);
  }
}

/******************************************************************************************
 *  LUA scrpit handling utilities
 */

unsigned int pi_lua_load (buffer_t * output, unsigned char *name)
{
  int result, i;
  lua_State *l;
  lua_context_t *ctx;

  /*
   * All Lua contexts are held in this structure, we work with it almost
   * all the time.
   */

  l = lua_open ();
  openlualibs (l);		/* Load Lua libraries */
  luaregister (l);		/* register lua commands */

  // load the file
  result = luaL_loadfile (l, name);
  if (result)
    goto error;

  result = lua_pcall (l, 0, LUA_MULTRET, 0);
  if (result) {
    char *error = (char *) luaL_checkstring (l, 1);

    bf_printf (output, "LUA ERROR: %s\n", error);

    goto error;
  }
  // alloc and init context
  ctx = malloc (sizeof (lua_context_t));
  ctx->l = l;
  ctx->name = strdup (name);
  ctx->eventmap = 0;

  // add into list  
  ctx->next = &lua_list;
  ctx->prev = lua_list.prev;
  ctx->prev->next = ctx;
  ctx->next->prev = ctx;

  lua_ctx_cnt++;
  if (lua_ctx_peak > lua_ctx_cnt)
    lua_ctx_peak = lua_ctx_cnt;

  /* determine eventhandlers */
  for (i = 0; pi_lua_eventnames[i] != NULL; i++) {
    lua_pushstring (ctx->l, pi_lua_eventnames[i]);
    lua_gettable (ctx->l, LUA_GLOBALSINDEX);
    if (!lua_isnil (ctx->l, -1)) {
      ctx->eventmap |= (1 << i);
    }
    lua_remove (ctx->l, -1);
  }

  /* reset stack */
  lua_settop (l, 0);

  return 1;

error:
  lua_close (l);
  return 0;
}

unsigned int pi_lua_close (unsigned char *name)
{
  lua_context_t *ctx;

  for (ctx = lua_list.next; ctx != &lua_list; ctx = ctx->next) {
    if (strcmp (name, ctx->name))
      continue;

    lua_cmdclean (ctx->l);

    lua_close (ctx->l);
    free (ctx->name);

    ctx->next->prev = ctx->prev;
    ctx->prev->next = ctx->next;

    free (ctx);

    lua_ctx_cnt--;

    return 1;
  }

  return 0;
}

/******************************************************************************************
 *  LUA command handlers
 */

unsigned long handler_luastat (plugin_user_t * user, buffer_t * output, void *priv,
			       unsigned int argc, unsigned char **argv)
{
  lua_context_t *ctx;

  bf_printf (output, "Lua stats\nVersion: " LUA_VERSION "\nScripts count/peak: %d/%d",
	     lua_ctx_cnt, lua_ctx_peak);

  if (!lua_ctx_cnt) {
    bf_printf (output, "\nNo LUA scripts running.\n");
    return 1;
  }

  bf_printf (output, "\nRunning LUA scripts:\n");

  for (ctx = lua_list.next; ctx != &lua_list; ctx = ctx->next) {
    bf_printf (output, " %s\n", ctx->name);
  }

  return 1;
}

unsigned long handler_luaload (plugin_user_t * user, buffer_t * output, void *priv,
			       unsigned int argc, unsigned char **argv)
{


  if (argc != 2) {
    bf_printf (output, "Usage: %s <script>", argv[0]);
    return 0;
  }

  if (pi_lua_load (output, argv[1])) {
    bf_printf (output, "Lua script '%s' loaded.\n", argv[1]);
  } else {
    bf_printf (output, "Lua script '%s' failed to load.\n", argv[1]);
  }

  return 0;
}

unsigned long handler_luaclose (plugin_user_t * user, buffer_t * output, void *priv,
				unsigned int argc, unsigned char **argv)
{

  if (argc != 2) {
    bf_printf (output, "Usage: %s <script>", argv[0]);
    return 0;
  }

  pi_lua_close (argv[1]);

  bf_printf (output, "Lua script '%s' unloaded.\n", argv[1]);

  return 0;
}


/******************************************************************************************
 * lua function - event 
 */

unsigned long pi_lua_event_handler (plugin_user_t * user, buffer_t * output,
				    unsigned long event, buffer_t * token)
{
  int result = PLUGIN_RETVAL_CONTINUE;
  lua_context_t *ctx;

  for (ctx = lua_list.next; (ctx != &lua_list) && (!result); ctx = ctx->next) {
    /* script doesn't have event handler */
    if (!(ctx->eventmap & (1 << event)))
      continue;

    /* clear stack */
    lua_settop (ctx->l, 0);

    /* specify function to call */
    /* lua 5.1 
       lua_getfield (ctx->l, LUA_GLOBALSINDEX, command);
     */

    /* lua 5.0 */
    lua_pushstring (ctx->l, pi_lua_eventnames[event]);
    lua_gettable (ctx->l, LUA_GLOBALSINDEX);

    /* push arguments */
    lua_pushstring (ctx->l, user->nick);
    lua_pushstring (ctx->l, (token ? token->s : (unsigned char *) ""));

    /* call funtion. */
    result = lua_pcall (ctx->l, 2, 1, 0);
    if (result) {
      char *error = (char *) luaL_checkstring (ctx->l, 1);
      buffer_t *buf;

      DPRINTF ("LUA ERROR: %s\n", error);

      buf = bf_alloc (32 + strlen (error) + strlen (ctx->name));

      bf_printf (buf, "LUA ERROR ('%s'): %s\n", ctx->name, error);

      plugin_report (buf);

      bf_free (buf);
    }

    /* retrieve return value */
    if (lua_isboolean (ctx->l, -1)) {
      result = lua_toboolean (ctx->l, -1);
      lua_remove (ctx->l, -1);
    }
  }

  return result;
}

/*
unsigned long pi_lua_event_load (plugin_user_t * user, buffer_t * output, void *dummy,
				 unsigned long event, buffer_t * token)
{
  int j;

  for (j = 0; j < 25; j++)
    lua_script_id[j].l = NULL;	// Null the lua state pointer

  return PLUGIN_RETVAL_CONTINUE;
}
*/

/******************************* INIT *******************************************/

int pi_lua_init ()
{
  lua_list.next = &lua_list;
  lua_list.prev = &lua_list;
  lua_list.l = NULL;
  lua_list.name = NULL;
  lua_ctx_cnt = 0;
  lua_ctx_peak = 0;
  breakmainchat = 0;

  pi_lua_command_list.next = &pi_lua_command_list;
  pi_lua_command_list.prev = &pi_lua_command_list;
  pi_lua_command_list.name = NULL;
  pi_lua_command_list.lua = NULL;

  plugin_lua = plugin_register ("lua");

  //plugin_request (plugin_lua, PLUGIN_EVENT_LOAD, (plugin_event_handler_t *) & pi_lua_event_load);

  /* lua event handlers */
  plugin_request (plugin_lua, PLUGIN_EVENT_LOGIN,
		  (plugin_event_handler_t *) & pi_lua_event_handler);
  plugin_request (plugin_lua, PLUGIN_EVENT_LOGOUT,
		  (plugin_event_handler_t *) & pi_lua_event_handler);
  plugin_request (plugin_lua, PLUGIN_EVENT_CHAT, (plugin_event_handler_t *) & pi_lua_event_handler);
  plugin_request (plugin_lua, PLUGIN_EVENT_SEARCH,
		  (plugin_event_handler_t *) & pi_lua_event_handler);
  plugin_request (plugin_lua, PLUGIN_EVENT_PM_IN,
		  (plugin_event_handler_t *) & pi_lua_event_handler);
  plugin_request (plugin_lua, PLUGIN_EVENT_PRELOGIN,
		  (plugin_event_handler_t *) & pi_lua_event_handler);
  plugin_request (plugin_lua, PLUGIN_EVENT_INFOUPDATE,
		  (plugin_event_handler_t *) & pi_lua_event_handler);

  command_register ("luastat", &handler_luastat, CAP_CONFIG, "Show lua stats.");
  command_register ("luaload", &handler_luaload, CAP_CONFIG, "Load a lua script.");
  command_register ("luaremove", &handler_luaclose, CAP_CONFIG, "Remove a lua script.");

  return 0;
}
