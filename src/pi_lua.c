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
 *  Thanks to Tomas "ZeXx86" Jedrzejek (admin@infern0.tk) for original implementation
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
#include "user.h"
#include "cap.h"

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


unsigned char *pi_lua_savefile;

plugin_t *plugin_lua = NULL;

unsigned int lua_ctx_cnt, lua_ctx_peak;

typedef struct lua_context {
  struct lua_context *next, *prev;

  lua_State *l;
  unsigned char *name;
  unsigned long long eventmap;
} lua_context_t;

lua_context_t lua_list;

unsigned int plugin_lua_close (unsigned char *name);

/******************************* LUA INTERFACE *******************************************/

/******************************************************************************************
 *   Utilities
 */

unsigned int parserights (unsigned char *caps, unsigned long *cap, unsigned long *ncap)
{
  unsigned int j;
  unsigned char *c, *tmp;

  tmp = strdup (caps);
  c = strtok (tmp, " ,");
  while (c) {
    for (j = 0; Capabilities[j].name; j++) {
      if (!strcasecmp (Capabilities[j].name, c)) {
	if (c[0] != '-') {
	  *cap |= Capabilities[j].cap;
	  *ncap &= ~Capabilities[j].cap;
	} else {
	  *ncap |= Capabilities[j].cap;
	  *cap &= ~Capabilities[j].cap;
	}
	break;
      }
    };
    c = strtok (NULL, " ,");
  }
  free (tmp);

  return 0;
}

/******************************************************************************************
 *  LUA Function Configuration handling
 */

int pi_lua_getconfig (lua_State * lua)
{
  config_element_t *elem;

  unsigned char *name = (unsigned char *) luaL_checkstring (lua, 1);

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

  unsigned char *name = (unsigned char *) luaL_checkstring (lua, 1);
  unsigned char *value = (unsigned char *) luaL_checkstring (lua, 2);

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
extern long users_total;
int pi_lua_getuserstotal (lua_State * lua)
{
  lua_pushnumber (lua, users_total);

  return 1;
}

int pi_lua_getuserip (lua_State * lua)
{
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  plugin_user_t *user;

  user = plugin_user_find (nick);

  if (!user) {
    lua_pushnil (lua);
  } else {
    struct in_addr in;

    in.s_addr = user->ipaddress;
    lua_pushstring (lua, inet_ntoa (in));
  }

  return 1;
}

int pi_lua_getuserclient (lua_State * lua)
{
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  plugin_user_t *user;

  user = plugin_user_find (nick);

  if (!user) {
    lua_pushnil (lua);
  } else {
    lua_pushstring (lua, user->client);
  }

  return 1;
}

int pi_lua_getuserclientversion (lua_State * lua)
{
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  plugin_user_t *user;

  user = plugin_user_find (nick);

  if (!user) {
    lua_pushnil (lua);
  } else {
    lua_pushstring (lua, user->versionstring);
  }

  return 1;
}

int pi_lua_getusershare (lua_State * lua)
{
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  plugin_user_t *user;

  user = plugin_user_find (nick);

  if (!user) {
    lua_pushnil (lua);
  } else {
    lua_pushstring (lua, format_size (user->share));
  }

  return 1;
}

int pi_lua_getusersharenum (lua_State * lua)
{
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  plugin_user_t *user;

  user = plugin_user_find (nick);

  if (!user) {
    lua_pushnil (lua);
  } else {
    lua_pushnumber (lua, user->share);
  }

  return 1;
}

int pi_lua_getuserslots (lua_State * lua)
{
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  plugin_user_t *user;

  user = plugin_user_find (nick);

  if (!user) {
    lua_pushnil (lua);
  } else {
    lua_pushnumber (lua, user->slots);
  }

  return 1;
}

int pi_lua_getuserhubs (lua_State * lua)
{
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  plugin_user_t *user;

  user = plugin_user_find (nick);

  if (!user) {
    lua_pushnil (lua);
    lua_pushnil (lua);
    lua_pushnil (lua);
  } else {
    lua_pushnumber (lua, user->hubs[0]);
    lua_pushnumber (lua, user->hubs[1]);
    lua_pushnumber (lua, user->hubs[2]);
  }

  return 3;
}


int pi_lua_userisop (lua_State * lua)
{
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  plugin_user_t *user;

  user = plugin_user_find (nick);

  if (!user) {
    lua_pushnil (lua);
  } else {
    lua_pushboolean (lua, user->op);
  }

  return 1;
}

int pi_lua_userisactive (lua_State * lua)
{
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  plugin_user_t *user;

  user = plugin_user_find (nick);

  if (!user) {
    lua_pushnil (lua);
  } else {
    lua_pushboolean (lua, user->active);
  }

  return 1;
}

int pi_lua_userisregistered (lua_State * lua)
{
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  plugin_user_t *user;

  user = plugin_user_find (nick);

  if (!user) {
    lua_pushnil (lua);
  } else {
    lua_pushboolean (lua, user->flags & 2);
  }

  return 1;
}

int pi_lua_useriszombie (lua_State * lua)
{
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  plugin_user_t *user;

  user = plugin_user_find (nick);

  if (!user) {
    lua_pushnil (lua);
  } else {
    lua_pushboolean (lua, user->flags & 4);
  }

  return 1;
}


int pi_lua_getuserrights (lua_State * lua)
{
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  plugin_user_t *user;
  buffer_t *b;

  user = plugin_user_find (nick);

  if (!user) {
    lua_pushnil (lua);
  } else {
    b = bf_alloc (10240);
    command_flags_print ((command_flag_t *) Capabilities, b, user->rights);
    lua_pushstring (lua, b->s);
    bf_free (b);
  }

  return 1;
}


/******************************************************************************************
 *  LUA Functions Bot handling
 */


int pi_lua_addbot (lua_State * lua)
{
  plugin_user_t *u;
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  unsigned char *description = (unsigned char *) luaL_checkstring (lua, 2);

  u = plugin_robot_add (nick, description, NULL);

  lua_pushboolean (lua, (u != NULL));

  return 1;
}

int pi_lua_delbot (lua_State * lua)
{
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  plugin_user_t *u;

  u = plugin_user_find (nick);

  if (u)
    plugin_robot_remove (u);

  lua_pushboolean (lua, (u != NULL));

  return 1;
}

/******************************************************************************************
 *  LUA Functions User handling
 */

int pi_lua_user_kick (lua_State * lua)
{
  buffer_t *b;
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  unsigned char *message = (unsigned char *) luaL_checkstring (lua, 2);
  plugin_user_t *u;

  u = plugin_user_find (nick);

  if (!u) {
    lua_pushboolean (lua, 0);
    return 1;
  }

  b = bf_alloc (10240);
  bf_printf (b, "%s", message);

  plugin_user_kick (u, b);

  bf_free (b);

  lua_pushboolean (lua, 1);

  return 1;
}

int pi_lua_user_drop (lua_State * lua)
{
  buffer_t *b;
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  unsigned char *message = (unsigned char *) luaL_checkstring (lua, 2);
  plugin_user_t *u;

  u = plugin_user_find (nick);

  if (!u) {
    lua_pushboolean (lua, 0);
    return 1;
  }

  b = bf_alloc (10240);
  bf_printf (b, "%s", message);

  plugin_user_drop (u, b);

  bf_free (b);

  lua_pushboolean (lua, 1);

  return 1;
}

int pi_lua_user_ban (lua_State * lua)
{
  buffer_t *b;
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  unsigned char *periodstring = (unsigned char *) luaL_checkstring (lua, 2);
  unsigned char *message = (unsigned char *) luaL_checkstring (lua, 3);
  plugin_user_t *u;
  unsigned long period;

  u = plugin_user_find (nick);

  if (!u) {
    lua_pushboolean (lua, 0);
    return 1;
  }

  period = time_parse (periodstring);

  b = bf_alloc (10240);
  bf_printf (b, "%s", message);

  plugin_user_ban (u, b, period);

  bf_free (b);

  lua_pushboolean (lua, 1);

  return 1;
}

int pi_lua_user_bannick (lua_State * lua)
{
  buffer_t *b;
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  unsigned char *periodstring = (unsigned char *) luaL_checkstring (lua, 2);
  unsigned char *message = (unsigned char *) luaL_checkstring (lua, 3);
  plugin_user_t *u;
  unsigned long period;

  u = plugin_user_find (nick);

  if (!u) {
    lua_pushboolean (lua, 0);
    return 1;
  }

  period = time_parse (periodstring);

  b = bf_alloc (10240);
  bf_printf (b, "%s", message);

  plugin_user_bannick (u, b, period);

  bf_free (b);

  lua_pushboolean (lua, 1);

  return 1;
}

int pi_lua_user_banip (lua_State * lua)
{
  buffer_t *b;
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  unsigned char *periodstring = (unsigned char *) luaL_checkstring (lua, 2);
  unsigned char *message = (unsigned char *) luaL_checkstring (lua, 3);
  plugin_user_t *u;
  unsigned long period;

  u = plugin_user_find (nick);

  if (!u) {
    lua_pushboolean (lua, 0);
    return 1;
  }

  period = time_parse (periodstring);

  b = bf_alloc (10240);
  bf_printf (b, "%s", message);

  plugin_user_banip (u, b, period);

  bf_free (b);

  lua_pushboolean (lua, 1);

  return 1;
}

int pi_lua_user_banip_hard (lua_State * lua)
{
  buffer_t *b;
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  unsigned char *periodstring = (unsigned char *) luaL_checkstring (lua, 2);
  unsigned char *message = (unsigned char *) luaL_checkstring (lua, 3);
  plugin_user_t *u;
  unsigned long period;

  u = plugin_user_find (nick);

  if (!u) {
    lua_pushboolean (lua, 0);
    return 1;
  }

  period = time_parse (periodstring);

  b = bf_alloc (10240);
  bf_printf (b, "%s", message);

  plugin_user_banip_hard (u, b, period);

  bf_free (b);

  lua_pushboolean (lua, 1);

  return 1;
}


int pi_lua_banip (lua_State * lua)
{
  buffer_t *b;
  unsigned char *ip = (unsigned char *) luaL_checkstring (lua, 1);
  unsigned char *periodstring = (unsigned char *) luaL_checkstring (lua, 2);
  unsigned char *message = (unsigned char *) luaL_checkstring (lua, 3);
  unsigned long period;
  struct in_addr ia;

  period = time_parse (periodstring);


  if (!inet_aton (ip, &ia)) {
    lua_pushstring (lua, "Not a valid IP address.\n");
    lua_error (lua);
  }

  b = bf_alloc (10240);
  bf_printf (b, "%s", message);

  plugin_ban_ip (ia.s_addr, b, period);

  bf_free (b);

  lua_pushboolean (lua, 1);

  return 1;
}

int pi_lua_banip_hard (lua_State * lua)
{
  buffer_t *b;
  unsigned char *ip = (unsigned char *) luaL_checkstring (lua, 1);
  unsigned char *periodstring = (unsigned char *) luaL_checkstring (lua, 2);
  unsigned char *message = (unsigned char *) luaL_checkstring (lua, 3);
  unsigned long period;
  struct in_addr ia;

  period = time_parse (periodstring);


  if (!inet_aton (ip, &ia)) {
    lua_pushstring (lua, "Not a valid IP address.\n");
    lua_error (lua);
  }

  b = bf_alloc (10240);
  bf_printf (b, "%s", message);

  plugin_ban_ip_hard (ia.s_addr, b, period);

  bf_free (b);

  lua_pushboolean (lua, 1);

  return 1;
}

int pi_lua_unban (lua_State * lua)
{
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);

  lua_pushboolean (lua, plugin_unban (nick));

  return 1;
}

int pi_lua_unbannick (lua_State * lua)
{
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);

  lua_pushboolean (lua, plugin_unban_nick (nick));

  return 1;
}

int pi_lua_unbanip (lua_State * lua)
{
  unsigned char *ip = (unsigned char *) luaL_checkstring (lua, 1);
  struct in_addr ia;

  if (!inet_aton (ip, &ia)) {
    lua_pushstring (lua, "Not a valid IP address.\n");
    lua_error (lua);
  }

  lua_pushboolean (lua, plugin_unban_ip (ia.s_addr));

  return 1;
}

int pi_lua_unbanip_hard (lua_State * lua)
{
  unsigned char *ip = (unsigned char *) luaL_checkstring (lua, 1);
  struct in_addr ia;

  if (!inet_aton (ip, &ia)) {
    lua_pushstring (lua, "Not a valid IP address.\n");
    lua_error (lua);
  }

  lua_pushboolean (lua, plugin_unban_ip_hard (ia.s_addr));

  return 1;
}

int pi_lua_user_zombie (lua_State * lua)
{
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  plugin_user_t *u;

  u = plugin_user_find (nick);

  if (!u) {
    lua_pushboolean (lua, 0);
    return 1;
  }

  lua_pushboolean (lua, plugin_user_zombie (u));

  return 1;
}

int pi_lua_user_unzombie (lua_State * lua)
{
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  plugin_user_t *u;

  u = plugin_user_find (nick);

  if (!u) {
    lua_pushboolean (lua, 0);
    return 1;
  }

  lua_pushboolean (lua, plugin_user_unzombie (u));

  return 1;
}

int pi_lua_findnickban (lua_State * lua)
{
  buffer_t *b;
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);

  b = bf_alloc (10240);

  if (plugin_user_findnickban (b, nick)) {
    lua_pushstring (lua, b->s);
  } else {
    lua_pushnil (lua);
  }

  bf_free (b);

  return 1;
}

int pi_lua_findipban (lua_State * lua)
{
  buffer_t *b;
  unsigned char *ip = (unsigned char *) luaL_checkstring (lua, 1);
  struct in_addr ia;

  if (!inet_aton (ip, &ia)) {
    lua_pushstring (lua, "Not a valid IP address.\n");
    lua_error (lua);
  }

  b = bf_alloc (10240);

  if (plugin_user_findipban (b, ia.s_addr)) {
    lua_pushstring (lua, b->s);
  } else {
    lua_pushnil (lua);
  }

  bf_free (b);

  return 1;
}

int pi_lua_report (lua_State * lua)
{
  buffer_t *b;
  unsigned char *message = (unsigned char *) luaL_checkstring (lua, 1);

  b = bf_alloc (10240);
  bf_printf (b, "%s", message);

  plugin_report (b);

  bf_free (b);

  lua_pushboolean (lua, 1);

  return 1;
}

/******************************************************************************************
 *  LUA message functions
 */

int pi_lua_sendtoall (lua_State * lua)
{
  buffer_t *b;
  plugin_user_t *u;
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  unsigned char *message = (unsigned char *) luaL_checkstring (lua, 2);

  u = plugin_user_find (nick);

  if (!u) {
    lua_pushboolean (lua, 0);
    return 1;
  }

  b = bf_alloc (10240);
  bf_printf (b, "%s", message);

  lua_pushboolean (lua, plugin_user_say (u, b));

  bf_free (b);

  return 1;
}

int pi_lua_sendtonick (lua_State * lua)
{
  buffer_t *b;
  plugin_user_t *s, *d;
  unsigned char *src = (unsigned char *) luaL_checkstring (lua, 1);
  unsigned char *dest = (unsigned char *) luaL_checkstring (lua, 2);
  unsigned char *message = (unsigned char *) luaL_checkstring (lua, 3);

  s = plugin_user_find (src);
  /* s can be NULL, wil become hubsec */

  d = plugin_user_find (dest);
  if (!d) {
    lua_pushboolean (lua, 0);
    return 1;
  }

  b = bf_alloc (10240);
  bf_printf (b, "%s", message);

  lua_pushboolean (lua, plugin_user_sayto (s, d, b));

  bf_free (b);

  return 1;
}

int pi_lua_sendpmtoall (lua_State * lua)
{
  unsigned int i;
  buffer_t *b;
  plugin_user_t *tgt = NULL, *u;
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  unsigned char *message = (unsigned char *) luaL_checkstring (lua, 2);

  u = plugin_user_find (nick);

  b = bf_alloc (10240);
  bf_printf (b, "%s", message);

  /* send to all users */
  i = 0;
  while (plugin_user_next (&tgt)) {
    /* weird is i set direct to 1 it doesn't work. */
    plugin_user_priv (u, tgt, u, b, 0);
    i++;
  }
  lua_pushnumber (lua, i);

  bf_free (b);

  return 1;
}

int pi_lua_sendpmtonick (lua_State * lua)
{
  buffer_t *b;
  plugin_user_t *s, *d;
  unsigned char *src = (unsigned char *) luaL_checkstring (lua, 1);
  unsigned char *dest = (unsigned char *) luaL_checkstring (lua, 2);
  unsigned char *message = (unsigned char *) luaL_checkstring (lua, 3);

  s = plugin_user_find (src);
  /* s can be NULL, wil become hubsec */

  d = plugin_user_find (dest);
  if (!d) {
    lua_pushboolean (lua, 0);
    return 1;
  }

  b = bf_alloc (10240);
  bf_printf (b, "%s", message);

  lua_pushboolean (lua, plugin_user_priv (s, d, s, b, 1));

  bf_free (b);

  return 1;
}

/******************************************************************************************
 *  LUA Account Command Handling
 */

int pi_lua_account_create (lua_State * lua)
{
  unsigned int retval = 0;
  account_type_t *grp;
  account_t *acc;
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  unsigned char *group = (unsigned char *) luaL_checkstring (lua, 2);

  grp = account_type_find (group);
  if (!grp)
    goto leave;

  acc = account_add (grp, nick);
  if (!acc)
    goto leave;

  retval = 1;
leave:
  lua_pushboolean (lua, retval);
  return 1;
}

int pi_lua_account_delete (lua_State * lua)
{
  unsigned int retval = 0;
  account_t *acc;
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);

  acc = account_find (nick);
  if (!acc)
    goto leave;

  account_del (acc);

  retval = 1;
leave:
  lua_pushboolean (lua, retval);
  return 1;
}

int pi_lua_account_passwd (lua_State * lua)
{
  unsigned int retval = 0;
  account_t *acc;
  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  unsigned char *passwd = (unsigned char *) luaL_checkstring (lua, 2);

  acc = account_find (nick);
  if (!acc)
    goto leave;

  account_pwd_set (acc, passwd);

  retval = 1;
leave:
  lua_pushboolean (lua, retval);
  return 1;
}

int pi_lua_account_pwgen (lua_State * lua)
{
  unsigned int i;
  account_t *acc;
  unsigned char passwd[64];

  unsigned char *nick = (unsigned char *) luaL_checkstring (lua, 1);
  unsigned int pwlen = luaL_checknumber (lua, 2);

  if ((pwlen < 4) || (pwlen > 50))
    pwlen = 12;

  passwd[0] = 0;
  acc = account_find (nick);
  if (!acc)
    goto leave;

  for (i = 0; i < pwlen; i++) {
    passwd[i] = (33 + (random () % 90));
  }
  passwd[i] = '\0';

  account_pwd_set (acc, passwd);

leave:
  lua_pushstring (lua, passwd);
  return 1;
}

int pi_lua_group_create (lua_State * lua)
{
  unsigned int retval = 0;
  unsigned long caps = 0, ncaps = 0;
  account_type_t *grp;
  unsigned char *group = (unsigned char *) luaL_checkstring (lua, 1);
  unsigned char *rights = (unsigned char *) luaL_checkstring (lua, 2);

  parserights (rights, &caps, &ncaps);

  grp = account_type_add (group, caps);
  if (!grp)
    goto leave;

  retval = 1;
leave:
  lua_pushboolean (lua, retval);
  return 1;
}

int pi_lua_group_inuse (lua_State * lua)
{
  unsigned int retval = 0;
  account_type_t *grp;

  unsigned char *group = (unsigned char *) luaL_checkstring (lua, 1);

  grp = account_type_find (group);
  if (!grp)
    goto leave;

  retval = (grp->refcnt != 0);

leave:
  lua_pushboolean (lua, retval);
  return 1;
}

int pi_lua_group_delete (lua_State * lua)
{
  unsigned int retval = 0;
  account_type_t *grp;

  unsigned char *group = (unsigned char *) luaL_checkstring (lua, 1);

  grp = account_type_find (group);
  if (!grp)
    goto leave;

  if (grp->refcnt)
    goto leave;

  account_type_del (grp);
  retval = 1;
leave:
  lua_pushboolean (lua, retval);
  return 1;
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
    unsigned char *error = (unsigned char *) luaL_checkstring (ctx->lua, 1);
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
int pi_lua_cmdreg (lua_State * lua)
{
  pi_lua_command_context_t *ctx;
  unsigned long caps = 0, ncaps = 0;
  unsigned char *cmd = (unsigned char *) luaL_checkstring (lua, 1);
  unsigned char *rights = (unsigned char *) luaL_checkstring (lua, 2);	// FIXME
  unsigned char *desc = (unsigned char *) luaL_checkstring (lua, 3);

  lua_pushstring (lua, cmd);
  lua_gettable (lua, LUA_GLOBALSINDEX);
  if (lua_isnil (lua, -1)) {
    lua_remove (lua, -1);
    lua_pushstring (lua, "No such LUA function.");
    return lua_error (lua);
  }
  lua_remove (lua, -1);

  parserights (rights, &caps, &ncaps);

  if (command_register (cmd, &handler_luacommand, caps, desc)) {
    lua_pushstring (lua, "Command failed to register.");
    return lua_error (lua);
  }

  /* alloc and init */
  ctx = malloc (sizeof (pi_lua_command_context_t));
  if (!ctx) {
    command_unregister (cmd);
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
int pi_lua_cmddel (lua_State * lua)
{
  pi_lua_command_context_t *ctx;
  unsigned char *cmd = (unsigned char *) luaL_checkstring (lua, 1);

  for (ctx = pi_lua_command_list.next; ctx != &pi_lua_command_list; ctx = ctx->next) {
    if ((lua != ctx->lua) || (strcmp (ctx->name, cmd)))
      continue;

    ctx->prev->next = ctx->next;
    ctx->next->prev = ctx->prev;

    command_unregister (cmd);

    free (ctx->name);
    free (ctx);

    break;
  }

  return 0;
}

/* clean out all command of this script */
int pi_lua_cmdclean (lua_State * lua)
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

typedef struct pi_lua_symboltable_element {
  const unsigned char *name;
  int (*func) (lua_State * lua);
} pi_lua_symboltable_element_t;

pi_lua_symboltable_element_t pi_lua_symboltable[] = {
  /* all user info related functions */
  {"GetUserIP", pi_lua_getuserip,},
  {"GetUserShare", pi_lua_getusershare,},
  {"GetUserShareNum", pi_lua_getusersharenum,},
  {"GetUserClient", pi_lua_getuserclient,},
  {"GetUserClientVersion", pi_lua_getuserclientversion,},
  {"GetUserSlots", pi_lua_getuserslots,},
  {"GetUserHubs", pi_lua_getuserhubs,},
  {"GetUserRights", pi_lua_getuserrights,},

  {"UserIsOP", pi_lua_userisop,},
  {"UserIsActive", pi_lua_userisactive,},
  {"UserIsRegistered", pi_lua_userisactive,},
  {"UserIsZombie", pi_lua_userisactive,},

  /* kick and ban functions */
  {"UserKick", pi_lua_user_kick,},
  {"UserDrop", pi_lua_user_drop,},
  {"UserBan", pi_lua_user_ban,},
  {"UserBanNick", pi_lua_user_bannick,},
  {"UserBanIP", pi_lua_user_banip,},
  {"UserBanIPHard", pi_lua_user_banip_hard,},
  {"BanIP", pi_lua_banip,},
  {"BanIPHard", pi_lua_banip_hard,},
  {"UnBan", pi_lua_unban,},
  {"UnBanNick", pi_lua_unbannick,},
  {"UnBanIP", pi_lua_unbanip,},
  {"UnBanIPHard", pi_lua_unbanip_hard,},
  {"Zombie", pi_lua_user_zombie,},
  {"UnZombie", pi_lua_user_unzombie,},

  {"FindNickBan", pi_lua_findnickban,},
  {"FindIPBan", pi_lua_findipban,},

  {"Report", pi_lua_report,},

  /* hub message functions */
  {"ChatToAll", pi_lua_sendtoall,},
  {"ChatToNick", pi_lua_sendtonick,},
  {"PMToAll", pi_lua_sendpmtoall,},
  {"PMToNick", pi_lua_sendpmtonick,},

  /* account management */
  {"GroupCreate", pi_lua_group_create,},
  {"GroupInUse", pi_lua_group_inuse,},
  {"GroupDelete", pi_lua_group_inuse,},
  {"AccountCreate", pi_lua_account_create,},
  {"AccountDelete", pi_lua_account_delete,},
  {"AccountPasswd", pi_lua_account_passwd,},
  {"AccountPwGen", pi_lua_account_pwgen,},

  /* hubinfo stat related info */
  {"GetActualUsersTotal", pi_lua_getuserstotal,},

  /* robot functions */
  {"AddBot", pi_lua_addbot,},
  {"DelBot", pi_lua_delbot,},

  /* lua created command functions */
  {"RegCommand", pi_lua_cmdreg,},
  {"DelCommand", pi_lua_cmddel,},

  /* config functions */
  {"SetConfig", pi_lua_setconfig,},
  {"GetConfig", pi_lua_getconfig,},

  {NULL, NULL}
};

int pi_lua_register_functions (lua_State * l)
{
  pi_lua_symboltable_element_t *cmd;

  for (cmd = pi_lua_symboltable; cmd->name; cmd++)
    lua_register (l, cmd->name, cmd->func);

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
  pi_lua_register_functions (l);	/* register lua commands */

  // load the file
  result = luaL_loadfile (l, name);
  if (result)
    goto error;

  result = lua_pcall (l, 0, LUA_MULTRET, 0);
  if (result) {
    unsigned char *error = (unsigned char *) luaL_checkstring (l, 1);

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

    pi_lua_cmdclean (ctx->l);

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
      unsigned char *error = (unsigned char *) luaL_checkstring (ctx->l, 1);
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


unsigned long pi_lua_event_save (plugin_user_t * user, buffer_t * output, void *dummy,
				 unsigned long event, buffer_t * token)
{
  FILE *fp;
  lua_context_t *ctx;

  fp = fopen (pi_lua_savefile, "w+");
  if (!fp)
    return PLUGIN_RETVAL_CONTINUE;

  for (ctx = lua_list.next; (ctx != &lua_list); ctx = ctx->next)
    fprintf (fp, "%s\n", ctx->name);

  fclose (fp);

  return PLUGIN_RETVAL_CONTINUE;
}

unsigned long pi_lua_event_load (plugin_user_t * user, buffer_t * output, void *dummy,
				 unsigned long event, buffer_t * token)
{
  unsigned int i;
  FILE *fp;
  unsigned char buffer[10240];

  fp = fopen (pi_lua_savefile, "r+");
  if (!fp)
    return PLUGIN_RETVAL_CONTINUE;

  fgets (buffer, sizeof (buffer), fp);
  while (!feof (fp)) {
    for (i = 0; buffer[i] && buffer[i] != '\n' && (i < sizeof (buffer)); i++);
    if (i == sizeof (buffer))
      break;
    if (buffer[i] == '\n')
      buffer[i] = '\0';

    pi_lua_load (output, buffer);
    fgets (buffer, sizeof (buffer), fp);
  }

  fclose (fp);

  return PLUGIN_RETVAL_CONTINUE;
}


/******************************* INIT *******************************************/

int pi_lua_init ()
{
  int i;

  pi_lua_savefile = strdup ("lua.conf");

  lua_list.next = &lua_list;
  lua_list.prev = &lua_list;
  lua_list.l = NULL;
  lua_list.name = NULL;
  lua_ctx_cnt = 0;
  lua_ctx_peak = 0;

  pi_lua_command_list.next = &pi_lua_command_list;
  pi_lua_command_list.prev = &pi_lua_command_list;
  pi_lua_command_list.name = NULL;
  pi_lua_command_list.lua = NULL;

  plugin_lua = plugin_register ("lua");

  plugin_request (plugin_lua, PLUGIN_EVENT_LOAD, (plugin_event_handler_t *) & pi_lua_event_load);
  plugin_request (plugin_lua, PLUGIN_EVENT_SAVE, (plugin_event_handler_t *) & pi_lua_event_save);

  /* lua event handlers */
  for (i = 0; pi_lua_eventnames[i] != NULL; i++)
    plugin_request (plugin_lua, i, (plugin_event_handler_t *) & pi_lua_event_handler);

  command_register ("luastat", &handler_luastat, CAP_CONFIG, "Show lua stats.");
  command_register ("luaload", &handler_luaload, CAP_CONFIG, "Load a lua script.");
  command_register ("luaremove", &handler_luaclose, CAP_CONFIG, "Remove a lua script.");

  return 0;
}