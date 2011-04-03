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
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "../config.h"
#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif

#include "aqtime.h"
#include "user.h"
#include "core_config.h"
#include "plugin_int.h"
#include "utils.h"

#include "hashlist_func.h"

#include "nmdc_utils.h"
#include "nmdc_token.h"
#include "nmdc_local.h"
#include "nmdc_protocol.h"

/******************************************************************************\
**                                                                            **
**                                  DEFINES                                   **
**                                                                            **
\******************************************************************************/

#define SKIPTOCHAR(var, end, ch)	for (; *var && (*var != ch) && (var < end); *var++)

/******************************************************************************\
**                                                                            **
**                            GLOBAL VARIABLES                                **
**                                                                            **
\******************************************************************************/

/******************************************************************************\
**                                                                            **
**                    PROTOCOL HANDLING PER STATE                             **
**                                                                            **
\******************************************************************************/

/********************
 *  State INIT
 */

/* define in main.c :S */
extern struct timeval boottime;

int proto_nmdc_state_init (nmdc_user_t * u, token_t * tkn)
{
  unsigned int i;
  int retval = 0;
  buffer_t *output;
  struct timeval tnow;
  banlist_entry_t *ban;

  timersub (&now, &boottime, &tnow);

  output = bf_alloc (2048);
  output->s[0] = '\0';

  bf_strcat (output, "$Lock EXTENDEDPROTOCOL" LOCK "[[");
  /* generate LOCKLENGTH characters between 32(' ') and 122('z') */
  for (i = 0; i < LOCKLENGTH; i++) {
    u->lock[i] = (random () % 90) + 33;
  }

  bf_strncat (output, u->lock, LOCKLENGTH);
  bf_strncat (output, "]] Pk=Aquila|", 13);
  bf_printf (output, _("<%s> This hub is running %s Version %s (Uptime %s.%.3lu).|"),
	     HubSec->user.nick, HUBSOFT_NAME, AQUILA_VERSION, time_print (tnow.tv_sec),
	     tnow.tv_usec / 1000);

  /* check for a reconnect ban */
  if (!cloning && (ban = banlist_find_bynet (&reconnectbanlist, u->user.ipaddress, 0xFFFFFFFF))) {
    bf_printf (output, _("<%s> Do not reconnect too fast, time remaining: %s|"), HubSec->user.nick,
	       time_print (ban->expire - now.tv_sec));
    retval = server_write (u->user.parent, output);
    proto_nmdc_user_drop (u, NULL);
    return retval;
  }

  retval = server_write (u->user.parent, output);

  if (u->state == PROTO_STATE_DISCONNECTED)
    return retval;

  u->state = PROTO_STATE_SENDLOCK;
  server_settimeout (u->user.parent, PROTO_TIMEOUT_SENDLOCK);

  bf_free (output);

  return retval;
}

/********************
 *  State SENDLOCK
 */

int proto_nmdc_state_sendlock (nmdc_user_t * u, token_t * tkn)
{
  int retval = 0;
  unsigned char *k, *l;
  buffer_t *output;

  output = bf_alloc (2048);
  output->s[0] = '\0';


  switch (tkn->type) {
    case TOKEN_SUPPORTS:
      k = tkn->argument;

      for (k = tkn->argument; *k; k = l) {
	while (*k && (*k == ' '))
	  k++;
	if (!*k)
	  break;
	l = k + 1;
	while (*l && (*l != ' '))
	  l++;

	/* check length too? */
	switch (*k) {
	  case 'N':
	    if (!strncmp (k, "NoGetINFO", 9)) {
	      u->supports |= NMDC_SUPPORTS_NoGetINFO;
	      continue;
	    }
	    if (!strncmp (k, "NoHello", 7)) {
	      u->supports |= NMDC_SUPPORTS_NoHello;
	      continue;
	    }
	    break;
	  case 'U':
	    if (!strncmp (k, "UserIP2", 7)) {
	      u->supports |= NMDC_SUPPORTS_UserIP2;
	      continue;
	    }
	    if (!strncmp (k, "UserCommand", 11)) {
	      u->supports |= NMDC_SUPPORTS_UserCommand;
	      continue;
	    }
	    break;
	  case 'Q':
	    if (!strncmp (k, "QuickList", 9)) {
	      u->supports |= NMDC_SUPPORTS_QuickList;
	      continue;
	    }
	    break;
	  case 'T':
	    if (!strncmp (k, "TTHSearch", 9)) {
	      u->supports |= NMDC_SUPPORTS_TTHSearch;
	      continue;
	    }
	    break;
	  case 'Z':
	    if (!strncmp (k, "ZPipe", 5)) {
	      u->supports |= NMDC_SUPPORTS_ZPipe;
	      /* if thisis the first zpipe user, we need to do a cache rebuild to build the zpipe buffer */
	      if (!cache.ZpipeSupporters++)
		cache.needrebuild = 1;
	      continue;
	    }
	    if (!strncmp (k, "ZLine", 5)) {
	      u->supports |= NMDC_SUPPORTS_ZLine;
	      /* if thisis the first zline user, we need to do a cache rebuild to build the zline buffer */
	      if (!cache.ZlineSupporters++)
		cache.needrebuild = 1;
	      continue;
	    }
	    break;
	  default:
	    DPRINTF ("Unknown Support flag %.*s\n", (int) (l - k), k);
	}
      }
      bf_strcat (output, "$Supports NoGetINFO NoHello BotINFO"
#ifdef ZLINES
		 " ZLine"
#endif
		 "|");

      server_settimeout (u->user.parent, PROTO_TIMEOUT_SENDLOCK);
      retval = server_write (u->user.parent, output);
      break;
    case TOKEN_KEY:
      {
	unsigned int i, j;
	unsigned char c;

	j = nmdc_string_unescape (tkn->argument, strlen (tkn->argument));
	/* validate length */
	if (j != keylen)
	  goto broken_key;

	/* xor the lock from the key */
	for (i = 0; i < LOCKLENGTH; i++) {
	  c = ((u->lock[i] << 4) & 240) | ((u->lock[i] >> 4) & 15);
	  tkn->argument[keyoffset + i] ^= c;
	  tkn->argument[keyoffset + i + 1] ^= c;
	}

	/* compare extracted key with stored key */
	if (memcmp (tkn->argument, key, keylen))
	  goto broken_key;

	u->state = PROTO_STATE_WAITNICK;
	server_settimeout (u->user.parent, PROTO_TIMEOUT_WAITNICK);

	break;
      broken_key:
	DPRINTF ("ILLEGAL KEY\n");
	nmdc_stats.brokenkey++;
	server_disconnect_user (u->user.parent, _("Protocol violation: Bad key."));
	retval = -1;
	break;
      }
  }

  bf_free (output);

  return retval;
}

/********************
 *  State WAITNICK
 */

int proto_nmdc_state_waitnick (nmdc_user_t * u, token_t * tkn)
{
  int retval = 0;
  buffer_t *output;
  banlist_entry_t *ban;
  nmdc_user_t *existing_user;
  account_t *a;

  if (tkn->type != TOKEN_VALIDATENICK)
    return 0;

  output = bf_alloc (2048);
  output->s[0] = '\0';

  /* nick already used ? */
  do {
    strncpy (u->user.nick, tkn->argument, NICKLENGTH);
    u->user.nick[NICKLENGTH - 1] = 0;

    existing_user = (nmdc_user_t *) hash_find_nick (&hashlist, u->user.nick, strlen (u->user.nick));

    /* the existing user is a bot or chatroom? deny user. */
    if (existing_user && (existing_user->state == PROTO_STATE_VIRTUAL)) {
      bf_strcat (output, "$ValidateDenide ");
      bf_strcat (output, u->user.nick);
      bf_strcat (output, "|");
      retval = server_write (u->user.parent, output);
      proto_nmdc_user_redirect (u, bf_buffer (__ ("Your nickname is already in use.")));
      nmdc_stats.usednick++;
      retval = -1;
      break;
    }

    bf_strcat (output, "$HubName ");
    bf_strcat (output, config.HubName);
    bf_strcat (output, "|");

    /* does the user have an account ? */
    if ((a = account_find (u->user.nick))) {
      if (a->passwd[0]) {
	/* ask for users password */
	u->user.flags |= PROTO_FLAG_REGISTERED;

	bf_strcat (output, "$GetPass|");
	u->state = PROTO_STATE_WAITPASS;
	server_settimeout (u->user.parent, PROTO_TIMEOUT_WAITPASS);
	retval = server_write (u->user.parent, output);
	break;
      } else {
	/* assign CAP_SHARE and CAP_TAG anyway */
	u->user.rights =
	  config.DefaultRights | ((a->rights | a->classp->rights) & (CAP_SHARE | CAP_TAG));
	proto_nmdc_user_say_string (HubSec, output,
				    __
				    ("Your account priviliges will not be awarded until you set a password."));
      }
    }

    /* check for a reconnect ban */
    if ((ban = banlist_find_bynick (&reconnectbanlist, u->user.nick))) {
      bf_printf (output, _("<%s> Do not reconnect too fast, time remaining: %s|"),
		 HubSec->user.nick, time_print (ban->expire - now.tv_sec));

      retval = server_write (u->user.parent, output);
      proto_nmdc_user_drop (u, NULL);
      retval = -1;
      break;
    }

    /* if user exists */
    if (existing_user) {
      if (existing_user->user.ipaddress != u->user.ipaddress) {
	bf_strcat (output, "$ValidateDenide ");
	bf_strcat (output, u->user.nick);
	bf_strcat (output, "|");
	retval = server_write (u->user.parent, output);
	proto_nmdc_user_redirect (u, bf_buffer (__ ("Your nickname is already in use.")));
	nmdc_stats.usednick++;
	retval = -1;
	break;
      } else {
	proto_nmdc_user_say_string (HubSec, output,
				    __ ("Another instance of you is connecting, bye!"));
	server_write (existing_user->user.parent, output);
	server_disconnect_user (existing_user->user.parent, __ ("Reconnecting."));
	*output->s = '\0';
	output->e = output->s;
      }
    }

    /* check for cloning */
    if ((!cloning) && (existing_user = (nmdc_user_t *) hash_find_ip (&hashlist, u->user.ipaddress))) {
      proto_nmdc_user_redirect (u,
				bf_buffer
				(__
				 ("Cloning is not allowed. Another user is already logged in from this IP address.")));
      retval = -1;
      break;
    }

    /* add a reconnect ban for the user to prevent quick relogin. */
    if (config.ReconnectBantime) {
      banlist_add (&reconnectbanlist, HubSec->user.nick, u->user.nick, u->user.ipaddress,
		   0xFFFFFFFF, bf_buffer (__ ("Do not reconnect too fast.")),
		   now.tv_sec + config.ReconnectBantime);
    }

    /* prepare buffer */
    bf_strcat (output, "$Hello ");
    bf_strcat (output, u->user.nick);
    bf_strcat (output, "|");

    /* soft ban ? */
    ban = banlist_find_byip (&softbanlist, u->user.ipaddress);
    if (ban) {
      DPRINTF ("** Refused user %s. IP Banned because %.*s\n", u->user.nick,
	       (unsigned int) bf_used (ban->message), ban->message->s);
      bf_printf (output, _("<%s> You have been banned by %s because: "), HubSec->user.nick,
		 ban->op);
      bf_strncat (output, ban->message->s, bf_used (ban->message));
      bf_strcat (output, "|");
      if (ban->expire) {
	bf_printf (output, _("<%s> Time remaining %s|"), HubSec->user.nick,
		   time_print (ban->expire - now.tv_sec));
      }
      if (defaultbanmessage && strlen (defaultbanmessage)) {
	bf_printf (output, "<%s> %s|", HubSec->user.nick, defaultbanmessage);
      }
      retval = server_write (u->user.parent, output);
      proto_nmdc_user_forcemove (u, config.KickBanRedirect, bf_buffer (__ ("Banned.")));
      nmdc_stats.forcemove--;
      nmdc_stats.banned++;
      retval = -1;
      nmdc_stats.softban++;
      break;
    }

    /* nickban ? */
    ban = banlist_find_bynick (&softbanlist, u->user.nick);
    if (ban) {
      DPRINTF ("** Refused user %s. Nick Banned because %.*s\n", u->user.nick,
	       (unsigned int) bf_used (ban->message), ban->message->s);
      bf_printf (output, _("<%s> You have been banned by %s because: "), HubSec->user.nick,
		 ban->op);
      bf_strncat (output, ban->message->s, bf_used (ban->message));
      bf_strcat (output, "|");
      if (ban->expire) {
	bf_printf (output, _("<%s> Time remaining %s|"), HubSec->user.nick,
		   time_print (ban->expire - now.tv_sec));
      }
      if (defaultbanmessage && strlen (defaultbanmessage)) {
	bf_printf (output, "<%s> %s|", HubSec->user.nick, defaultbanmessage);
      }
      retval = server_write (u->user.parent, output);
      proto_nmdc_user_forcemove (u, config.KickBanRedirect, bf_buffer (__ ("Banned.")));
      nmdc_stats.forcemove--;
      nmdc_stats.banned++;
      retval = -1;
      nmdc_stats.nickban++;
      break;
    }


    if (!u->user.rights)
      u->user.rights = config.DefaultRights;

    /* success! */
    BF_VERIFY (output);
    retval = server_write (u->user.parent, output);
    u->state = PROTO_STATE_HELLO;
    server_settimeout (u->user.parent, PROTO_TIMEOUT_HELLO);

    /* DPRINTF (" - User %s greeted.\n", u->user.nick); */
  }
  while (0);

  bf_free (output);

  return retval;
}

/********************
 *  State WAITPASS
 */


int proto_nmdc_state_waitpass (nmdc_user_t * u, token_t * tkn)
{
  int retval = 0;
  account_t *a;
  account_type_t *t;
  buffer_t *output;
  nmdc_user_t *existing_user;
  banlist_entry_t *ban;

  if (tkn->type != TOKEN_MYPASS)
    return 0;


  output = bf_alloc (2048);
  output->s[0] = '\0';
  do {
    /* check password */
    a = account_find (u->user.nick);
    if (!account_pwd_check (a, tkn->argument)) {
      if ((ban = banlist_find_bynick (&softbanlist, u->user.nick)))
	goto banned;

      proto_nmdc_user_say (HubSec, output, bf_buffer (__ ("Bad password.")));
      bf_strcat (output, "$BadPass|");
      retval = server_write (u->user.parent, output);
      server_disconnect_user (u->user.parent, __ ("Bad password"));
      retval = -1;
      nmdc_stats.badpasswd++;
      /* check password guessing attempts */
      if (++a->badpw >= config.PasswdRetry) {
	banlist_add (&softbanlist, HubSec->user.nick, u->user.nick, u->user.ipaddress, 0xFFFFFFFF,
		     bf_buffer (__ ("Password retry overflow.")),
		     now.tv_sec + config.PasswdBantime);
	a->badpw = 0;
      }

      break;
    }
    /* reset password failure counter */
    a->badpw = 0;

    t = a->classp;

    /* check if users exists, if so, redirect old */
    if ((existing_user =
	 (nmdc_user_t *) hash_find_nick (&hashlist, u->user.nick, strlen (u->user.nick)))) {
      proto_nmdc_user_say_string (HubSec, output,
				  __ ("Another instance of you is connecting, bye!"));
      server_write (existing_user->user.parent, output);
      server_disconnect_user (existing_user->user.parent, __ ("Reconnecting."));
      *output->s = '\0';
      output->e = output->s;
    }

    /* assign rights */
    if (a->passwd[0]) {
      u->user.rights = a->rights | t->rights;
      u->op = ((u->user.rights & CAP_KEY) != 0);
    };

    /* nickban ? not for owner offcourse */
    ban = banlist_find_bynick (&softbanlist, u->user.nick);
    if (ban && (!(u->user.rights & CAP_OWNER))) {
      goto banned;
      break;
    }

    time (&a->lastlogin);
    /* welcome user */
    if (u->op)
      bf_printf (output, "$LogedIn %s|", u->user.nick);
    bf_strcat (output, "$Hello ");
    bf_strcat (output, u->user.nick);
    bf_strcat (output, "|");

    if (!a->passwd[0])
      proto_nmdc_user_say (HubSec, output,
			   bf_buffer
			   (__
			    ("Your account priviliges will not be awarded until you set a password. Use !passwd or !pwgen.")));

    u->state = PROTO_STATE_HELLO;
    server_settimeout (u->user.parent, PROTO_TIMEOUT_HELLO);

    retval = server_write (u->user.parent, output);

    /* DPRINTF (" - User %s greeted.\n", u->user.nick); */
  } while (0);

  bf_free (output);

  return retval;

banned:
  DPRINTF ("** Refused user %s. Nick Banned because %.*s\n", u->user.nick,
	   (unsigned int) bf_used (ban->message), ban->message->s);
  bf_printf (output, _("<%s> You have been banned by %s because: "), HubSec->user.nick, ban->op);
  bf_strncat (output, ban->message->s, bf_used (ban->message));
  bf_strcat (output, "|");
  if (ban->expire) {
    bf_printf (output, _("<%s> Time remaining %s|"), HubSec->user.nick,
	       time_print (ban->expire - now.tv_sec));
  }
  if (defaultbanmessage && strlen (defaultbanmessage)) {
    bf_printf (output, "<%s> %s|", HubSec->user.nick, defaultbanmessage);
  }
  retval = server_write (u->user.parent, output);
  proto_nmdc_user_forcemove (u, config.KickBanRedirect, bf_buffer (__ ("Banned.")));
  nmdc_stats.forcemove--;
  nmdc_stats.banned++;
  nmdc_stats.nickban++;

  bf_free (output);

  return -1;
}

/********************
 *  State HELLO
 */


int proto_nmdc_state_hello (nmdc_user_t * u, token_t * tkn, buffer_t * b)
{
  int retval = 0;
  buffer_t *output;
  nmdc_user_t *existing_user;

  if (tkn->type == TOKEN_GETNICKLIST) {
    u->user.flags |= NMDC_FLAG_DELAYEDNICKLIST;
    server_settimeout (u->user.parent, PROTO_TIMEOUT_HELLO);
    return 0;
  }

  if (tkn->type != TOKEN_MYINFO) {
    return 0;
  }

  output = bf_alloc (2048);
  output->s[0] = '\0';

  do {
    /* $MyINFO $ALL Jove yes... i cannot type. I can Dream though...<DCGUI V:0.3.3,M:A,H:1,S:5>$ $DSL.$email$0$ */

    /* check again if user exists */
    if ((existing_user =
	 (nmdc_user_t *) hash_find_nick (&hashlist, u->user.nick, strlen (u->user.nick)))) {
      if (existing_user->user.ipaddress != u->user.ipaddress) {
	proto_nmdc_user_redirect (u, bf_buffer (__ ("Your nickname is already in use.")));
	nmdc_stats.usednick++;
	retval = -1;
	break;
      } else {
	proto_nmdc_user_say_string (HubSec, output,
				    __ ("Another instance of you is connecting, bye!"));
	server_write (existing_user->user.parent, output);
	server_disconnect_user (existing_user->user.parent, __ ("Reconnecting"));
      }
      existing_user = NULL;
    }

    /* now check if user is in the cachelist, if ip address changed, check the sharesize */
    if ((existing_user =
	 (nmdc_user_t *) hash_find_nick (&cachehashlist, u->user.nick, strlen (u->user.nick)))
	&& ((u->user.ipaddress != existing_user->user.ipaddress)
	    && (u->user.share != existing_user->user.share)))
      existing_user = NULL;

    /* should not happen */
    if (u->MyINFO)
      bf_free (u->MyINFO);

    /* create backup */
    u->MyINFO = rebuild_myinfo (u, b);
    if (!u->MyINFO || (u->user.active < 0)) {
      /* we cannot pass a user without a valid u->MyINFO field. */
      if (u->MyINFO && (u->user.active < 0)) {
	if (u->user.rights & CAP_TAG) {
	  DPRINTF ("  Warning: CAP_TAG overrides bad myinfo");
	  proto_nmdc_user_say (HubSec, output,
			       bf_buffer (__ ("WARNING: You should use a client that uses tags!")));
	  u->user.active = 0;
	} else {
	  proto_nmdc_user_redirect (u,
				    bf_buffer
				    (__
				     ("This hub requires tags, please upgrade to a client that supports them.")));
	  retval = -1;
	  nmdc_stats.notags++;
	  break;
	}
      } else {
	DPRINTF ("  User %s refused due to bad MyINFO.\n", u->user.nick);
	proto_nmdc_user_redirect (u,
				  bf_buffer (__
					     ("Your login was refused, your MyINFO seems corrupt.")));
	retval = -1;
	nmdc_stats.badmyinfo++;
	break;
      }
    }
    ASSERT (u->MyINFO);

    /* allocate plugin private stuff */
    plugin_new_user (nmdc_pluginmanager, (plugin_private_t **) & u->user.plugin_priv, &u->user);

    /* send the login event before we announce the new user to the hub so plugins can redirect those users */
    if (plugin_send_event
	(nmdc_pluginmanager, u->user.plugin_priv, PLUGIN_EVENT_PRELOGIN,
	 u->MyINFO) != PLUGIN_RETVAL_CONTINUE) {
      proto_nmdc_user_redirect (u, bf_buffer (__ ("Your login was refused.")));
      retval = -1;
      nmdc_stats.preloginevent++;
      break;
    }

    /* restore some values */
    if (existing_user) {
      /* restore rates */
      u->rate_warnings = existing_user->rate_warnings;
      u->rate_chat = existing_user->rate_chat;
      u->rate_search = existing_user->rate_search;
      u->rate_myinfo = existing_user->rate_myinfo;
      u->rate_myinfoop = existing_user->rate_myinfoop;
      u->rate_getnicklist = existing_user->rate_getnicklist;
      u->rate_getinfo = existing_user->rate_getinfo;
      u->rate_downloads = existing_user->rate_downloads;
      u->rate_psresults_in = existing_user->rate_psresults_in;
      u->rate_psresults_out = existing_user->rate_psresults_out;

      /* restore the tthlist if old user has one, otherwise keep new */
      if (u->tthlist && existing_user->tthlist)
	free (u->tthlist);
      if (existing_user->tthlist) {
	u->tthlist = existing_user->tthlist;
	existing_user->tthlist = NULL;
      }

      /* queue the old userentry for deletion */
      proto_nmdc_user_cachelist_invalidate (existing_user);

      nmdc_stats.logincached++;
    }

    DPRINTF (" - User %s has %s shared and is %s\n", u->user.nick, format_size (u->user.share),
	     u->user.active ? "active" : "passive");

    u->state = PROTO_STATE_ONLINE;
    u->user.flags |= PROTO_FLAG_ONLINE;
    server_settimeout (u->user.parent, PROTO_TIMEOUT_ONLINE);
    time (&u->user.joinstamp);

    /* add user to nicklist cache */
    nicklistcache_adduser (u);

    /* from now on, user is reachable */
    hash_adduser (&hashlist, &u->user);

    /* send it to the users */
    if ((!existing_user) || (existing_user->user.share != u->user.share)) {
      cache_queue (cache.myinfo, u, u->MyINFO);
    };

    /* ops get the full tag immediately */
    cache_queue (cache.myinfoupdateop, u, b);

    /* if this new user is an OP send an updated OpList */
    if (u->op)
      nicklistcache_sendoplist (u);

    /* send the nicklist if it was requested before the MyINFO arrived 
     * if not, credit user with 1 getnicklist token.
     */
    if (u->user.flags & NMDC_FLAG_DELAYEDNICKLIST) {
      u->user.flags &= ~NMDC_FLAG_DELAYEDNICKLIST;
      nicklistcache_sendnicklist (u);
    } else {
      u->rate_getnicklist.tokens = 1;
    }

    if (u->state != PROTO_STATE_ONLINE)
      break;

    /* send the login event */
    if (plugin_send_event (nmdc_pluginmanager, u->user.plugin_priv, PLUGIN_EVENT_LOGIN, u->MyINFO)
	!= PLUGIN_RETVAL_CONTINUE) {
      proto_nmdc_user_redirect (u, bf_buffer (__ ("Your login was refused.")));
      retval = -1;
      nmdc_stats.loginevent++;
      break;
    }
  }
  while (0);

  bf_free (output);

  return retval;
}

/********************
 *  State ONLINE
 */

int proto_nmdc_state_online_chat (nmdc_user_t * u, token_t * tkn, buffer_t * output, buffer_t * b)
{
  int retval = 0;
  int i;
  string_list_entry_t *le;

  do {
    if (!(u->user.rights & CAP_CHAT)) {
      proto_nmdc_user_warn (u, &now, __ ("You are not allowed to chat."));
      break;
    }

    /* check quota */
    if ((!(u->user.rights & CAP_SPAM)) && (!get_token (&rates.chat, &u->rate_chat, now.tv_sec))) {
      proto_nmdc_user_warn (u, &now, __ ("Think before you talk and don't spam."));
      nmdc_stats.chatoverflow++;
      retval = proto_nmdc_violation (u, &now, "Chat");
      break;
    }

    /* drop all chat message that are too long */
    if ((bf_size (b) > chatmaxlength) && (!(u->user.rights & CAP_SPAM))) {
      proto_nmdc_user_warn (u, &now, __ ("Your chat message was too long."));
      nmdc_stats.chattoolong++;
      break;
    }

    /* verify the nick */
    if (strncasecmp (u->user.nick, b->s + 1, strlen (u->user.nick))) {
      nmdc_stats.chatfakenick++;
      break;
    }

    /* verify the closing > */
    if (b->s[strlen (u->user.nick) + 1] != '>') {
      nmdc_stats.chatfakenick++;
      break;
    }

    /* drop any trailing spaces */
    while (b->e[-1] == ' ')
      b->e--;
    *b->e = '\0';

    /* drop empty strings */
    if ((b->s + strlen (u->user.nick) + 2) == b->e)
      break;

    /* call plugin first. it can force us to drop the message */
    if (plugin_send_event (nmdc_pluginmanager, u->user.plugin_priv, PLUGIN_EVENT_CHAT, b) !=
	PLUGIN_RETVAL_CONTINUE) {
      nmdc_stats.chatevent++;
      break;
    }

    /* allocate buffer of max size with some extras. */
    buffer_t *buf = bf_alloc (cache.chat.length + cache.chat.messages.count + b->size);

    /* send back to user : keep in order */
    le = cache.chat.messages.first;

    /* skip all previous send messages */
    for (i = u->ChatCnt; i && le; le = le->next)
      if (le->user == &u->user)
	i--;

    /* rest of add cached data, skip markers */
    for (; le; le = le->next) {
      if (!bf_used (le->data))
	continue;
      bf_strncat (buf, le->data->s, bf_used (le->data));
      bf_strcat (buf, "|");
    }
    /* add string to send */
    bf_strncat (buf, b->s, bf_used (b));
    bf_strcat (buf, "|");
    retval = server_write (u->user.parent, buf);

    bf_free (buf);

    /* mark user as "special" */
    u->ChatCnt++;
    u->CacheException++;

    if (!(u->user.flags & PROTO_FLAG_ZOMBIE)) {
      cache_queue (cache.chat, u, b);
    } else {
      /* add empty chat buffer as marker */
      buffer_t *mark = bf_alloc (1);

      cache_queue (cache.chat, u, mark);
      bf_free (mark);
    }
  } while (0);

  return retval;
}

int proto_nmdc_state_online_myinfo (nmdc_user_t * u, token_t * tkn, buffer_t * output, buffer_t * b)
{
  int retval = 0;
  buffer_t *new;

  do {
    /* build and generate the tag */
    new = rebuild_myinfo (u, b);
    if (!new || (u->user.active < 0)) {
      if (new && (u->user.active < 0)) {
	if (u->user.rights & CAP_TAG) {
	  DPRINTF ("  Warning: CAP_TAG overrides bad myinfo");
	  proto_nmdc_user_say (HubSec, output,
			       bf_buffer (__ ("WARNING: You should use a client that uses tags!")));
	  retval = server_write (u->user.parent, output);
	  u->user.active = 0;
	  ASSERT (new);
	  goto accept_anyway;
	} else {
	  proto_nmdc_user_redirect (u,
				    bf_buffer
				    (__
				     ("This hub requires tags, please upgrade to a client that supports them.")));
	  retval = -1;
	  nmdc_stats.notags++;
	}
	if (new)
	  bf_free (new);
	break;
      }

      proto_nmdc_user_redirect (u, bf_buffer (__ ("Broken MyINFO, get lost.")));
      retval = -1;
      nmdc_stats.badmyinfo++;
      break;
    }
  accept_anyway:

    /* update plugin info */
    plugin_update_user (nmdc_pluginmanager, &u->user);

    /* send new info event */
    if (plugin_send_event (nmdc_pluginmanager, u->user.plugin_priv, PLUGIN_EVENT_INFOUPDATE, b) !=
	PLUGIN_RETVAL_CONTINUE) {
      nmdc_stats.myinfoevent++;
      proto_nmdc_user_redirect (u,
				bf_buffer
				(__
				 ("Sorry, you no longer satisfy the necessary requirements for this hub.")));
      retval = -1;
      bf_free (new);
      break;
    }


    if (u->MyINFO)
      bf_free (u->MyINFO);
    u->MyINFO = new;

    /* ops get the full tag immediately */
    if (get_token (&rates.myinfoop, &u->rate_myinfoop, now.tv_sec)) {
      cache_purge (cache.myinfoupdateop, u);
      cache_queue (cache.myinfoupdateop, u, b);
    } else {
      string_list_entry_t *entry;

      /* if entry in the stringlist, replace it */
      if (entry = string_list_find (&cache.myinfoupdateop.messages, u)) {
	cache.myinfoupdateop.length -= bf_used (entry->data);
	string_list_del (&cache.myinfoupdateop.messages, entry);

	cache_queue (cache.myinfoupdateop, u, u->MyINFO);
      }
    }

    /* check quota */
    if (!get_token (&rates.myinfo, &u->rate_myinfo, now.tv_sec)) {
      string_list_entry_t *entry;

      /* if no entry in the stringlist yet, exit */
      if (!(entry = string_list_find (&cache.myinfoupdate.messages, &u->user))) {
	nmdc_stats.myinfooverflow++;
	/* retval = proto_nmdc_violation (u, &now, "MyINFO"); */
	break;
      }

      /* if there is an entry, replace it with the more recent one. 
       * don't use cache_purge, we already have the pointer and there is max 1.
       */
      cache.myinfoupdate.length -= bf_used (entry->data);
      string_list_del (&cache.myinfoupdate.messages, entry);

      cache_queue (cache.myinfoupdate, u, u->MyINFO);

      break;
    }

    /* queue the tag for al users */
    cache_purge (cache.myinfoupdate, u);
    cache_queue (cache.myinfoupdate, u, u->MyINFO);

    /* update the tag */
    nicklistcache_updateuser (u, new);
  } while (0);

  return retval;

}

int proto_nmdc_state_online_search (nmdc_user_t * u, token_t * tkn, buffer_t * output, buffer_t * b)
{
  int retval = 0;
  tth_t tth;
  tth_list_entry_t *e;
  unsigned char *c = NULL, *n;
  unsigned long deadline;
  struct in_addr addr;

  deadline = now.tv_sec - researchperiod;
  do {

    if (!(u->user.rights & CAP_SEARCH)) {
      /* this is really annoying 
         proto_nmdc_user_warn (u, &now, "You are not allowed to search."); 
       */
      break;
    }

    /* check quota */
    if (!get_token (u->user.active ? &rates.asearch : &rates.psearch, &u->rate_search, now.tv_sec)
	&& (!(u->user.rights & CAP_NOSRCHLIMIT))) {
      if (u->user.active) {
	proto_nmdc_user_warn (u, &now, __ ("Active searches are limited to %u every %u seconds."),
			      rates.asearch.refill, rates.asearch.period);
      } else {
	proto_nmdc_user_warn (u, &now, __ ("Passive searches are limited to %u every %u seconds."),
			      rates.psearch.refill, rates.psearch.period);
      }
      retval = proto_nmdc_violation (u, &now, "Search");
      nmdc_stats.searchoverflow++;
      break;
    }

    /* drop all seach message that are too long */
    if ((bf_size (b) > searchmaxlength) && (!(u->user.rights & CAP_SPAM))) {
      proto_nmdc_user_warn (u, &now, __ ("Your search message is too long."));
      nmdc_stats.searchtoolong++;
      break;
    }

    /* verify nick, mode and IP */
    c = tkn->argument;
    if (*c == 'H') {
      /* verify and skip Hub: */
      if (strncasecmp (c, "Hub:", 4)) {
	nmdc_stats.searchcorrupt++;
	break;
      }
      c += 4;

      /* check mode */
      if (u->user.active) {
	proto_nmdc_user_warn (u, &now, __ ("You claim to be active. Passive search DENIED."));
	nmdc_stats.searchcorrupt++;
	break;
      }
      /* verify nick */
      n = c;
      SKIPTOCHAR (c, b->e, ' ');
      *c = 0;
      if (strcasecmp (n, u->user.nick)) {
	nmdc_stats.searchcorrupt++;
	break;
      }
      *c = ' ';
    } else {
      /* check mode */
      if (!u->user.active) {
	proto_nmdc_user_warn (u, &now, __ ("You claim to be passive. Active search DENIED."));
	nmdc_stats.searchcorrupt++;
	break;
      }

      /* verify IP */
      if (!((u->user.rights & CAP_LOCALLAN) && (ISLOCAL (u->user.ipaddress)))) {
	n = c;
	SKIPTOCHAR (c, b->e, ':');

	*c = 0;
	if (!inet_aton (n, &addr)) {
	  *c = ':';
	  break;
	}

	if (u->user.ipaddress != addr.s_addr) {
	  addr.s_addr = u->user.ipaddress;
	  proto_nmdc_user_warn (u, &now,
				__ ("Your client uses IP %s for searching, while you have IP %s\n"),
				n, inet_ntoa (addr));
	  nmdc_stats.searchcorrupt++;
	  *c = ':';
	  break;
	}
	*c = ':';
      }
    }

    /* CAP_NOSRCHLIMIT avoids research option */
    if (!(u->user.rights & CAP_NOSRCHLIMIT)) {
      if (u->tthlist && tth_harvest (&tth, tkn->argument)) {
	nmdc_stats.searchtth++;
	if ((e = tth_list_check (u->tthlist, &tth, researchperiod))) {
	  if ((now.tv_sec - e->stamp) < researchmininterval) {
	    /* search dropped because researched too quickly */
	    proto_nmdc_user_warn (u, &now, __ ("Do not repeat searches within %d seconds."),
				  researchmininterval);
	    u->rate_search.tokens++;
	    nmdc_stats.researchdrop++;
	    break;
	  }
	  if (deadline < e->stamp) {
	    cache_queue (cache.aresearch, u, b);
	    if (u->user.active) {
	      cache_queue (cache.presearch, u, b);
	    }
	    nmdc_stats.researchmatch++;
	    e->stamp = now.tv_sec;
	    break;
	  }
	} else {
	  tth_list_add (u->tthlist, &tth, now.tv_sec);
	}
      } else
	nmdc_stats.searchnormal++;
    }

    if (plugin_send_event (nmdc_pluginmanager, u->user.plugin_priv, PLUGIN_EVENT_SEARCH, b) !=
	PLUGIN_RETVAL_CONTINUE) {
      nmdc_stats.searchevent++;
      break;
    }

    /* if there is still a search cached from this user, delete it. */
    cache_purge (cache.asearch, u);
    if (u->user.active)
      cache_purge (cache.psearch, u);

    /* mark user as "special" */
    u->SearchCnt++;
    u->CacheException++;

    cache_queue (cache.asearch, u, b);
    if (u->user.active) {
      cache_queue (cache.psearch, u, b);
    }
  } while (0);

  return retval;
}

int proto_nmdc_state_online_sr (nmdc_user_t * u, token_t * tkn, buffer_t * output, buffer_t * b)
{
  int retval = 0;
  int l;
  unsigned char *c, *n;
  nmdc_user_t *t;

  do {
    /* check quota */
    if (!get_token (&rates.psresults_out, &u->rate_psresults_out, now.tv_sec)) {
      nmdc_stats.sroverflow++;
      break;
    }

    /* drop all seach message that are too long */
    if ((bf_size (b) > srmaxlength) && (!(u->user.rights & CAP_SPAM))) {
      nmdc_stats.srtoolong++;
      break;
    }

    if (plugin_send_event (nmdc_pluginmanager, u->user.plugin_priv, PLUGIN_EVENT_SR, b) !=
	PLUGIN_RETVAL_CONTINUE) {
      nmdc_stats.srevent++;
      break;
    }

    c = tkn->argument;
    n = tkn->argument;

    /* find end of nick */
    SKIPTOCHAR (c, b->e, ' ');
    l = c - n;

    if ((!*c) || strncmp (n, u->user.nick, l)) {
      nmdc_stats.srfakesource++;
      break;
    }

    c = b->e;
    c--;			/* point to last valid character */
    while ((*c != 5) && (c > b->s))
      --c;
    /* no \5 found */
    if (*c != 5) {
      ++nmdc_stats.srnodest;
      break;
    }
    *c++ = '\0';
    l = b->e - c;
    b->e = c - 1;

    if (l > NICKLENGTH) {
      ++nmdc_stats.srnodest;
      break;
    }

    /* find target */
    t = (nmdc_user_t *) hash_find_nick (&hashlist, c, l);
    if (!t) {
      ++nmdc_stats.srnodest;
      break;
    }

    if (!(t->user.rights & CAP_SEARCH))
      break;

    /* check quota */
    if (!get_token (&rates.psresults_in, &t->rate_psresults_in, now.tv_sec)) {
      nmdc_stats.sroverflow++;
      break;
    }

    /* search result for a robot?? */
    if (t->state == PROTO_STATE_VIRTUAL) {
      ++nmdc_stats.srrobot;
      break;
    }

    if (t->state != PROTO_STATE_ONLINE) {
      ++nmdc_stats.srnodest;
      break;
    }

    /* queue search result with the correct user */
    cache_queue (t->results, u, b);
    cache_count (results, t);
    t->ResultCnt++;
    t->CacheException++;
  } while (0);

  return retval;
}


int proto_nmdc_state_online_getinfo (nmdc_user_t * u, token_t * tkn, buffer_t * output,
				     buffer_t * b)
{
  int retval = 0;
  int l;
  unsigned char *c, *n;
  nmdc_user_t *t;

  do {
    /* check quota */
    if ((u->supports & NMDC_SUPPORTS_NoGetINFO)
	&& (!get_token (&rates.getinfo, &u->rate_getinfo, now.tv_sec)))
      break;

    c = tkn->argument;
    n = tkn->argument;

    /* find end of nick */
    SKIPTOCHAR (c, b->e, ' ');
    l = c - n;

    /* find target */
    t = (nmdc_user_t *) hash_find_nick (&hashlist, n, l);
    if (!t)
      break;

    cache_queue (u->privatemessages, u, t->MyINFO);
    cache_count (privatemessages, u);
    u->MessageCnt++;
    u->CacheException++;
  } while (0);

  return retval;
}

int proto_nmdc_state_online_ctm (nmdc_user_t * u, token_t * tkn, buffer_t * output, buffer_t * b)
{
  int retval = 0;
  int l;
  unsigned char *c, *n;
  nmdc_user_t *t;
  struct in_addr addr;

  do {
    /* this means a passive user cannot download from a user that isn't allowed to dl */
    if (!(u->user.rights & CAP_DL)) {
      proto_nmdc_user_warn (u, &now, __ ("You are not allowed to download."));
      break;
    }

    /* check quota */
    if (!get_token (&rates.downloads, &u->rate_downloads, now.tv_sec)) {
      nmdc_stats.ctmoverflow++;
      break;
    }

    c = tkn->argument;
    n = tkn->argument;

    /* find end of nick */
    SKIPTOCHAR (c, b->e, ' ');
    l = c - n;

    /* find target */
    t = (nmdc_user_t *) hash_find_nick (&hashlist, n, l);
    if (!t) {
      *c = '\0';
      DPRINTF ("CTM: cannot find target %s\n", n);
      nmdc_stats.ctmbadtarget++;
      break;
    }

    if ((t->user.rights & CAP_SHAREBLOCK) && t->user.active) {
      proto_nmdc_user_warn (u, &now, __ ("You cannot download from %s."), t->user.nick);
      break;
    }

    if (!((u->user.rights & CAP_LOCALLAN) && (ISLOCAL (u->user.ipaddress)))) {
      n = ++c;
      SKIPTOCHAR (c, b->e, ':');
      if (*c != ':')
	break;
      l = c - n;

      /* convert address */
      *c = 0;
      if (!inet_aton (n, &addr)) {
	break;
      }

      /* must be identical */
      if (u->user.ipaddress != addr.s_addr) {
	addr.s_addr = u->user.ipaddress;
	proto_nmdc_user_warn (u, &now,
			      __ ("Your client uses IP %s for downloading, while you have IP %s\n"),
			      n, inet_ntoa (addr));
	*c = ':';
	break;
      }
      *c = ':';
    }

    if (t->state == PROTO_STATE_ONLINE) {
      /* queue search result with the correct user
       * \0 termination should not be necessary
       */
      cache_queue (t->privatemessages, u, b);
      cache_count (privatemessages, t);
      t->MessageCnt++;
      t->CacheException++;
    };
  } while (0);

  return retval;
}

int proto_nmdc_state_online_rctm (nmdc_user_t * u, token_t * tkn, buffer_t * output, buffer_t * b)
{
  int retval = 0;
  int l;
  unsigned char *c, *n;
  nmdc_user_t *t;

  do {
    if (!(u->user.rights & CAP_DL)) {
      proto_nmdc_user_warn (u, &now, __ ("You are not allowed to download."));
      break;
    }

    /* check quota */
    if (!get_token (&rates.downloads, &u->rate_downloads, now.tv_sec)) {
      nmdc_stats.rctmoverflow++;
      break;
    }

    /* check target */
    n = c = tkn->argument;
    SKIPTOCHAR (c, b->e, ' ');
    l = c - n;

    if (strncasecmp (u->user.nick, tkn->argument, l)) {
      DPRINTF ("RCTM: FAKED source, user %s\n", u->user.nick);
      nmdc_stats.rctmbadsource++;
      break;
    }

    /* find end of nick, start at the end. */
    c = b->e - 1;
    n = b->e;
    if (!*c) {
      c--;
      n--;
    }
    for (; *c && (*c != ' '); c--);
    l = n - ++c;

    /* find target */
    t = (nmdc_user_t *) hash_find_nick (&hashlist, c, l);
    if (!t) {
      DPRINTF ("RCTM: cannot find target %s (%d)\n", c, l);
      nmdc_stats.rctmbadtarget++;
      break;
    }

    if (t->user.rights & CAP_SHAREBLOCK) {
      proto_nmdc_user_warn (u, &now, __ ("You cannot download from %s."), t->user.nick);
      break;
    }

    if (t->state == PROTO_STATE_ONLINE) {
      /* don't penitalize users for serving passive users: */
      if (t->rate_downloads.tokens <= rates.downloads.burst)
	t->rate_downloads.tokens++;

      /* queue search result with the correct user */
      cache_queue (t->privatemessages, u, b);
      cache_count (privatemessages, t);
      t->MessageCnt++;
      t->CacheException++;
    }

  } while (0);

  return retval;
}

int proto_nmdc_state_online_to (nmdc_user_t * u, token_t * tkn, buffer_t * output, buffer_t * b)
{
  int retval = 0;
  int l;
  unsigned char *c, *n;
  nmdc_user_t *t;

  do {

    if (!(u->user.rights & (CAP_PM | CAP_PMOP))) {
      proto_nmdc_user_warn (u, &now, __ ("You are not allowed to send private messages."));
      break;
    }

    if (u->user.flags & PROTO_FLAG_ZOMBIE)
      break;

    /* check quota */
    if ((!(u->user.rights & CAP_SPAM)) && (!get_token (&rates.chat, &u->rate_chat, now.tv_sec))) {
      proto_nmdc_user_warn (u, &now, __ ("Don't send private messages so fast."));
      nmdc_stats.pmoverflow++;
      retval = proto_nmdc_violation (u, &now, "PM");
      break;
    }


    c = tkn->argument;
    n = tkn->argument;

    /* find end of nick */
    SKIPTOCHAR (c, b->e, ' ');
    l = c - n;

    /* find target */
    t = (nmdc_user_t *) hash_find_nick (&hashlist, n, l);
    if (!t) {
      nmdc_stats.pmbadtarget++;
      break;
    }

    /* find end of From: */
    for (c++; *c && (*c != ' '); c++);

    /* find end of from Nick */
    n = ++c;
    for (c++; *c && (*c != ' '); c++);
    l = c - n;

    if (strncmp (u->user.nick, n, l)) {
      bf_printf (output, _("Bad From: nick. No faking."));
      retval = server_write (u->user.parent, output);
      server_disconnect_user (u->user.parent, __ ("Attempted PM faking."));
      nmdc_stats.pmbadsource++;
      retval = -1;
      break;
    };

    /* find $ */
    for (c++; *c && (*c != '$'); c++);
    c++;
    if (*c != '<')
      break;
    c++;

    /* find end of from Nick */
    n = c;
    for (c++; *c && (*c != '>'); c++);
    l = c - n;

    if (strncmp (u->user.nick, n, l)) {
      bf_printf (output, _("Bad display nick. No faking."));
      retval = server_write (u->user.parent, output);
      server_disconnect_user (u->user.parent, __ ("Attempted display nick faking"));
      nmdc_stats.pmbadsource++;
      retval = -1;
      break;
    };

    if (plugin_send_event (nmdc_pluginmanager, u->user.plugin_priv, PLUGIN_EVENT_PM_OUT, b) !=
	PLUGIN_RETVAL_CONTINUE) {
      nmdc_stats.pmoutevent++;
      break;
    }

    /* do not send if only PMOP and target is not an OP */
    if ((!(u->user.rights & CAP_PM)) && (!t->op)) {
      proto_nmdc_user_warn (u, &now,
			    __ ("Sorry, you can only send private messages to operators."));
      break;
    }
    if (plugin_send_event (nmdc_pluginmanager, t->user.plugin_priv, PLUGIN_EVENT_PM_IN, b) !=
	PLUGIN_RETVAL_CONTINUE) {
      nmdc_stats.pminevent++;
      break;
    }

    /* only pm to online users. */
    if (t->state == PROTO_STATE_ONLINE) {
      /* queue search result with the correct user 
       * \0 termination should not be necessary
       */
      cache_queue (t->privatemessages, u, b);
      cache_count (privatemessages, t);
      t->MessageCnt++;
      t->CacheException++;
    }
  } while (0);

  return retval;
}

int proto_nmdc_state_online_opforcemove (nmdc_user_t * u, token_t * tkn, buffer_t * output,
					 buffer_t * b)
{
  int retval = 0;
  unsigned char *c, *who, *where, *why;
  nmdc_user_t *target;

  /* unsigned int port; */
  do {
    if (!(u->user.rights & CAP_REDIRECT)) {
      proto_nmdc_user_warn (u, &now, __ ("You are not allowed to redirect users."));
      break;
    }

    /* parse argments. */
    c = tkn->argument;

    /* find first "$Who:" token */
    SKIPTOCHAR (c, b->e, '$');
    if (!*c++)
      break;

    if (strncmp (c, "Who", 3))
      break;
    if (!*c++)
      break;

    SKIPTOCHAR (c, b->e, ':');
    if (!*c)
      break;
    who = ++c;

    /* terminate string */
    SKIPTOCHAR (c, b->e, '$');
    if (!*c)
      break;
    *c = '\0';

    /* find user */
    target = (nmdc_user_t *) hash_find_nick (&hashlist, who, c - who);
    if (!target)
      break;

    /* check if this users doesn't have more rights */
    if (target->op && (target->user.rights & ~u->user.rights))
      break;

    /* find "Where:" token */
    if (strncmp (++c, "Where", 5))
      break;
    SKIPTOCHAR (c, b->e, ':');
    if (!*c)
      break;
    where = ++c;

    /* terminate */
    SKIPTOCHAR (c, b->e, '$');
    if (!*c)
      break;
    *c++ = '\0';

    /* find "Msg:" token */
    if (strncmp (c, "Msg", 3))
      break;
    SKIPTOCHAR (c, b->e, ':');
    if (!*c)
      break;
    why = ++c;

    /* check for port in where
       c = where;
       SKIPTOCHAR (c, why, ':');
       if (!*c) {
       c++;
       if (!sscanf (c, "%u", &port)) {
       DPRINTF ("BAD PORT in forcemove %s\n", c);
       break;
       }
       if ((port < 1024) && (port != 411)) {
       struct timeval now;
       gettimeofday (&now, NULL);
       proto_nmdc_user_warn (u, &now, "You are not allow to redirect users to that port.\n");
       break;
       }
       }
     */

    c = output->s;
    bf_printf (output, "%s", why);
    if (plugin_send_event (nmdc_pluginmanager, u->user.plugin_priv, PLUGIN_EVENT_REDIRECT, output)
	!= PLUGIN_RETVAL_CONTINUE) {
      proto_nmdc_user_warn (u, &now, __ ("Redirect refused.\n"));
      output->s = c;
      break;
    }
    output->s = c;

    /* move user.
     * this check will allow us to forcemove users that are not yet online.
     * while not forcemoving robots.
     */
    if (target->state != PROTO_STATE_VIRTUAL)
      retval = proto_nmdc_user_forcemove (target, where, bf_buffer (why));

    if (u == target)
      u = NULL;
  } while (0);

  return retval;
}

int proto_nmdc_state_online_kick (nmdc_user_t * u, token_t * tkn, buffer_t * output, buffer_t * b)
{
  int retval = 0;
  unsigned char *n, *c;
  nmdc_user_t *target;

  do {
    if (!(u->user.rights & CAP_KICK)) {
      proto_nmdc_user_warn (u, &now, __ ("You are not allowed to kick users."));
      break;
    }

    c = tkn->argument;
    while (*c && *c == ' ')
      c++;
    n = c;
    while (*c && *c != ' ')
      c++;
    if (*c)
      *c = '\0';

    target = (nmdc_user_t *) hash_find_nick (&hashlist, n, c - n);
    if (!target)
      break;
    /* don't kick robots. */
    if (target->state == PROTO_STATE_VIRTUAL)
      break;

    if (~u->user.rights & target->user.rights)
      break;

    c = output->s;
    bf_printf (output, _("You were kicked."));
    banlist_add (&softbanlist, u->user.nick, target->user.nick, target->user.ipaddress, 0xFFFFFFFF,
		 output, now.tv_sec + config.defaultKickPeriod);

    if (plugin_send_event (nmdc_pluginmanager, u->user.plugin_priv, PLUGIN_EVENT_REDIRECT, NULL) !=
	PLUGIN_RETVAL_CONTINUE) {
      proto_nmdc_user_warn (u, &now, __ ("Redirect refused.\n"));
      output->s = c;
      break;
    }

    retval = proto_nmdc_user_forcemove (target, config.KickBanRedirect, output);
    output->s = c;
  } while (0);

  return retval;
}

int proto_nmdc_state_online_botinfo (nmdc_user_t * u, token_t * tkn, buffer_t * output,
				     buffer_t * b)
{
  int retval = 0;
  config_element_t *share, *slot, *hub, *users, *owner;

  do {
    /* we reuse the warning rate here. Should not affect pingers (since they don't do much) and
     * prevents the need for yet another leaky bucket. */
    if ((!(u->user.rights & CAP_SPAM))
	&& (!get_token (&rates.warnings, &u->rate_warnings, now.tv_sec))) {
      proto_nmdc_user_warn (u, &now, __ ("Think before you ask HubINFO and don't spam.\n"));
      retval = proto_nmdc_violation (u, &now, "HubINFO");
      break;
    }

    share = config_find ("sharemin.unregistered");
    slot = config_find ("slot.unregistered.min");
    hub = config_find ("hub.unregistered.max");
    users = config_find ("userlimit.unregistered");
    owner = config_find ("hubowner");

    bf_printf (output, "$HubINFO %s$%s:%d$%s$%lu$%llu$%lu$%lu$%s$%s|",
	       config.HubName,
	       config.Hostname,
	       config.ListenPort,
	       config.HubDesc,
	       users ? *users->val.v_ulong : 100,
	       share ? *share->val.v_ulonglong : 0,
	       slot ? *slot->val.v_ulong : 0,
	       hub ? *hub->val.v_ulong : 100,
	       HUBSOFT_NAME, owner ? *owner->val.v_string : (unsigned char *) "Unknown");

    retval = server_write (u->user.parent, output);

    nmdc_stats.botinfo++;
  } while (0);

  return retval;
}

int proto_nmdc_state_online (nmdc_user_t * u, token_t * tkn, buffer_t * b)
{
  int retval = 0;

  /* user protocol handling */
  buffer_t *output;

  output = bf_alloc (2048);
  output->s[0] = '\0';
  switch (tkn->type) {
    case TOKEN_CHAT:
      retval = proto_nmdc_state_online_chat (u, tkn, output, b);
      break;
    case TOKEN_MYINFO:
      retval = proto_nmdc_state_online_myinfo (u, tkn, output, b);
      break;
    case TOKEN_SEARCH:
      retval = proto_nmdc_state_online_search (u, tkn, output, b);
      break;
    case TOKEN_SR:
      retval = proto_nmdc_state_online_sr (u, tkn, output, b);
      break;
    case TOKEN_GETNICKLIST:
      /* check quota */
      if (!get_token (&rates.getnicklist, &u->rate_getnicklist, now.tv_sec)) {
	proto_nmdc_user_warn (u, &now, __ ("Userlist request denied. Maximum 1 reload per %ds."),
			      rates.getnicklist.period);
	retval = proto_nmdc_violation (u, &now, "GetNickList");
	break;
      }

      nicklistcache_sendnicklist (u);
      break;
    case TOKEN_GETINFO:
      retval = proto_nmdc_state_online_getinfo (u, tkn, output, b);
      break;
    case TOKEN_CONNECTTOME:
      retval = proto_nmdc_state_online_ctm (u, tkn, output, b);
      break;
    case TOKEN_REVCONNECTOTME:
      retval = proto_nmdc_state_online_rctm (u, tkn, output, b);
      break;
    case TOKEN_TO:
      retval = proto_nmdc_state_online_to (u, tkn, output, b);
      break;
    case TOKEN_QUIT:
      break;
    case TOKEN_OPFORCEMOVE:
      retval = proto_nmdc_state_online_opforcemove (u, tkn, output, b);
      break;
    case TOKEN_KICK:
      retval = proto_nmdc_state_online_kick (u, tkn, output, b);
      break;
    case TOKEN_BOTINFO:
      retval = proto_nmdc_state_online_botinfo (u, tkn, output, b);
      break;
  }

  if (u && (u->state != PROTO_STATE_DISCONNECTED))
    server_settimeout (u->user.parent, PROTO_TIMEOUT_ONLINE);

  bf_free (output);

  return retval;
}


/******************************************************************************\
**                                                                            **
**                             PROTOCOL HANDLING                              **
**                                                                            **
\******************************************************************************/
int proto_nmdc_handle_token (nmdc_user_t * u, buffer_t * b)
{
  token_t tkn;

  /* this functions is called without token to initialize the connection */
  if (!b) {
    if (u->state != PROTO_STATE_INIT)
      return 0;

    return proto_nmdc_state_init (u, &tkn);
  }

  /* parse token. if it is unknown just reset the timeout and leave */
  if (token_parse (&tkn, b->s) == TOKEN_UNIDENTIFIED) {
    if (u->state == PROTO_STATE_ONLINE)
      server_settimeout (u->user.parent, PROTO_TIMEOUT_ONLINE);
    return 0;
  }

  /* handle token depending on state */
  switch (u->state) {
    case PROTO_STATE_INIT:	/* initial creation state */
      return proto_nmdc_state_init (u, &tkn);
    case PROTO_STATE_SENDLOCK:	/* waiting for user $Key */
      return proto_nmdc_state_sendlock (u, &tkn);
    case PROTO_STATE_WAITNICK:	/* waiting for user $ValidateNick */
      return proto_nmdc_state_waitnick (u, &tkn);
    case PROTO_STATE_WAITPASS:	/* waiting for user $Passwd */
      return proto_nmdc_state_waitpass (u, &tkn);
    case PROTO_STATE_HELLO:	/* waiting for user $MyInfo */
      return proto_nmdc_state_hello (u, &tkn, b);
    case PROTO_STATE_ONLINE:
      return proto_nmdc_state_online (u, &tkn, b);
    case PROTO_STATE_DISCONNECTED:
      /* not supposed to happen !! */
      ASSERT (0);
  }

  return 0;
}


/******************************************************************************\
**                                                                            **
**                             MAIN CACHE HANDLING                            **
**                                                                            **
\******************************************************************************/

unsigned int proto_nmdc_build_buffer (buffer_t * buffer, nmdc_user_t * u, unsigned int as,
				      unsigned int ps, unsigned int ch, unsigned int pm,
				      unsigned int res)
{
  buffer_t *b = NULL;
  unsigned long l;
  unsigned char *t, *e;
  string_list_entry_t *le;

  ASSERT ((u->ChatCnt + u->SearchCnt + u->ResultCnt + u->MessageCnt) == u->CacheException);
  t = buffer->e;
  e = buffer->buffer + buffer->size;
  if (ch) {
    /* skip all chat messages until the last send message of the user */
    le = cache.chat.messages.first;
    if (u->ChatCnt) {
      for (le = cache.chat.messages.first; le && u->ChatCnt; le = le->next)
	if (le->user == &u->user) {
	  u->ChatCnt--;
	  u->CacheException--;
	}
    };
    /* append the other chat messages */
    for (; le; le = le->next) {
      /* data and length */
      b = le->data;
      l = bf_used (b);
      if (!l)
	continue;

      memcpy (t, b->s, l);
      t += l;
      *t++ = '|';
    };
    u->CacheException -= u->ChatCnt;
    u->ChatCnt = 0;
  };

  if (u->user.active) {
    if (as) {
      for (le = cache.asearch.messages.first; le; le = le->next) {
	if (le->user == &u->user)
	  continue;

	/* data and length */
	b = le->data;
	l = bf_used (b);

	/* copy data */
	memcpy (t, b->s, l);
	t += l;
	*t++ = '|';
      }
      u->CacheException -= u->SearchCnt;
      u->SearchCnt = 0;
    }
  } else {
    /* complete buffers */
    if (ps) {
      for (le = cache.psearch.messages.first; le; le = le->next) {
	if (le->user == &u->user)
	  continue;

	/* data and length */
	b = le->data;
	l = bf_used (b);

	/* copy data */
	memcpy (t, b->s, l);
	t += l;
	*t++ = '|';
      }
      u->CacheException -= u->SearchCnt;
      u->SearchCnt = 0;
    }
  }
  /* add passive results */
  if (res && u->ResultCnt) {
    ASSERT (u->ResultCnt == u->results.messages.count);
    for (le = u->results.messages.first; le; le = le->next) {

      /* data and length */
      b = le->data;
      l = bf_used (b);

      /* copy data */
      memcpy (t, b->s, l);
      t += l;
      *t++ = '|';
      u->ResultCnt--;
      u->CacheException--;
    }
    ASSERT (!u->ResultCnt);
    cache_clear ((u->results));
  }
  /* add messages results */
  if (pm && u->MessageCnt) {
    ASSERT (u->MessageCnt == u->privatemessages.messages.count);
    for (le = u->privatemessages.messages.first; le; le = le->next) {
      /* data and length */
      b = le->data;
      l = bf_used (b);

      /* copy data */
      memcpy (t, b->s, l);
      t += l;
      *t++ = '|';
      u->MessageCnt--;
      u->CacheException--;
    }
    ASSERT (!u->MessageCnt);
    cache_clear ((u->privatemessages));
  }
  ASSERT (t <= e);
  ASSERT (t >= buffer->e);
  buffer->e = t;
  return bf_used (buffer);
}

inline int proto_nmdc_add_element (cache_element_t * elem, buffer_t * buf, buffer_t * buf2,
				   unsigned long now)
{
  register buffer_t *b;
  register unsigned char *t = NULL;
  register string_list_entry_t *le;
  register unsigned int l;

  if (elem->messages.count && get_token (&elem->timertype, &elem->timer, now)) {
    t = buf->e;
    for (le = elem->messages.first; le; le = le->next) {
      /* data and length */
      b = le->data;
      l = bf_used (b);

      /* copy data */
      memcpy (t, b->s, l);
      t += l;
      *t++ = '|';
    }
    if (buf2) {
      memcpy (buf2->e, buf->e, t - buf->e);
      buf2->e += t - buf->e;
      BF_VERIFY (buf2);
    }

    buf->e = t;
    BF_VERIFY (buf);
    return 1;
  }

  return 0;
}

void proto_nmdc_flush_cache ()
{
  buffer_t *b;
  nmdc_user_t *u, *n;
  unsigned int as = 0, ps = 0, ch = 0, pm = 0, mi = 0, miu = 0, res = 0, miuo = 0, ars = 0, prs = 0;
  unsigned long deadline;

  unsigned long t, r;

  unsigned long l, asl, psl;
  buffer_t *buf_passive, *buf_active, *buf_exception;

  unsigned long lop, aslop, pslop;
  buffer_t *buf_passive_op, *buf_active_op, *buf_exception_op;

  buffer_t *buf_aresearch, *buf_presearch;

#ifdef ZLINES
  buffer_t *buf_zlinepassive = NULL, *buf_zlineactive = NULL;
  buffer_t *buf_zpipepassive = NULL, *buf_zpipeactive = NULL;
  buffer_t *buf_zlinepassive_op = NULL, *buf_zlineactive_op = NULL;
  buffer_t *buf_zpipepassive_op = NULL, *buf_zpipeactive_op = NULL;
#endif

  /*
   * generate necessary buffers 
   */

  /* calculate lengths: always worst case. */
  l = cache.chat.length + cache.chat.messages.count;
  l += cache.myinfo.length + cache.myinfo.messages.count;
  lop = l;
  l += cache.myinfoupdate.length + cache.myinfoupdate.messages.count;
  lop += cache.myinfoupdateop.length + cache.myinfoupdateop.messages.count;

  t = cache.psearch.length + cache.psearch.messages.count;
  psl = l + t;
  pslop = lop + t;

  t = cache.asearch.length + cache.asearch.messages.count;
  asl = l + t;
  aslop = lop + t;

  /* allocate buffers */
  buf_passive = bf_alloc (psl);
  buf_active = bf_alloc (asl);
  buf_passive_op = bf_alloc (pslop);
  buf_active_op = bf_alloc (aslop);

  t =
    cache.results.length + cache.results.messages.count + cache.privatemessages.length +
    cache.privatemessages.messages.count;
  buf_exception = bf_alloc (((asl > psl) ? asl : psl) + t);
  buf_exception_op = bf_alloc (((aslop > pslop) ? aslop : pslop) + t);

  buf_aresearch = bf_alloc (cache.aresearch.length + cache.aresearch.messages.count);
  buf_presearch = bf_alloc (cache.presearch.length + cache.presearch.messages.count);

  deadline = now.tv_sec - researchperiod;

  /* operator buffer */
  // miuo = proto_nmdc_add_element (&cache.myinfoupdateop, buf_op, now.tv_sec);

  /* Exception buffer */
  mi = proto_nmdc_add_element (&cache.myinfo, buf_exception, buf_passive, now.tv_sec);
  miu = proto_nmdc_add_element (&cache.myinfoupdate, buf_exception, buf_passive, now.tv_sec);
  miuo =
    proto_nmdc_add_element (&cache.myinfoupdateop, buf_exception_op, buf_passive_op, now.tv_sec);

  ASSERT (bf_used (buf_exception) <= (l - cache.chat.length + cache.chat.messages.count));

  /* add chat messages */
  ch = proto_nmdc_add_element (&cache.chat, buf_passive, buf_passive_op, now.tv_sec);
  ASSERT (bf_used (buf_passive) <= l);
  ASSERT (bf_used (buf_passive_op) <= lop);

  /* copy identical part to active buffer too */
  t = bf_used (buf_passive);
  if (t) {
    memcpy (buf_active->s, buf_passive->s, t);
    buf_active->e += t;
  }
  t = bf_used (buf_passive_op);
  if (t) {
    memcpy (buf_active_op->s, buf_passive_op->s, t);
    buf_active_op->e += t;
  }

  /* at the end, add the passive and active search messages */
  ps = proto_nmdc_add_element (&cache.psearch, buf_passive, buf_passive_op, now.tv_sec);
  as = proto_nmdc_add_element (&cache.asearch, buf_active, buf_active_op, now.tv_sec);

  ASSERT (bf_used (buf_passive) <= psl);
  ASSERT (bf_used (buf_active) <= asl);

  /* check to see if we need to send pms */
  if (cache.privatemessages.messages.count
      && get_token (&cache.privatemessages.timertype, &cache.privatemessages.timer, now.tv_sec))
    pm = 1;

  /* check to see if we need to send results */
  if (cache.results.messages.count
      && get_token (&cache.results.timertype, &cache.results.timer, now.tv_sec))
    res = 1;

  ars = proto_nmdc_add_element (&cache.aresearch, buf_aresearch, NULL, now.tv_sec);
  prs = proto_nmdc_add_element (&cache.presearch, buf_presearch, NULL, now.tv_sec);

  BF_VERIFY (buf_active);
  BF_VERIFY (buf_passive);
  BF_VERIFY (buf_exception);
  BF_VERIFY (buf_active_op);
  BF_VERIFY (buf_passive_op);
  BF_VERIFY (buf_exception_op);
  BF_VERIFY (buf_aresearch);
  BF_VERIFY (buf_presearch);

#ifdef ZLINES
  if ((cache.ZlineSupporters > 0) || (cache.ZpipeSupporters > 0)) {
    zline (buf_passive, cache.ZpipeSupporters ? &buf_zpipepassive : NULL,
	   cache.ZlineSupporters ? &buf_zlinepassive : NULL);
    zline (buf_active, cache.ZpipeSupporters ? &buf_zpipeactive : NULL,
	   cache.ZlineSupporters ? &buf_zlineactive : NULL);
    zline (buf_passive_op, cache.ZpipeSupporters ? &buf_zpipepassive_op : NULL,
	   cache.ZlineSupporters ? &buf_zlinepassive_op : NULL);
    zline (buf_active_op, cache.ZpipeSupporters ? &buf_zpipeactive_op : NULL,
	   cache.ZlineSupporters ? &buf_zlineactive_op : NULL);
  }

  BF_VERIFY (buf_zpipeactive);
  BF_VERIFY (buf_zlineactive);
  BF_VERIFY (buf_zpipepassive);
  BF_VERIFY (buf_zlinepassive);
  BF_VERIFY (buf_zpipeactive_op);
  BF_VERIFY (buf_zlineactive_op);
  BF_VERIFY (buf_zpipepassive_op);
  BF_VERIFY (buf_zlinepassive_op);
#endif

  if (mi)
    nmdc_stats.cache_myinfo += cache.myinfo.length + cache.myinfo.messages.count;
  if (miu)
    nmdc_stats.cache_myinfoupdate += cache.myinfoupdate.length + cache.myinfoupdate.messages.count;
  if (ch)
    nmdc_stats.cache_chat += cache.chat.length + cache.chat.messages.count;
  if (as)
    nmdc_stats.cache_asearch += cache.asearch.length + cache.asearch.messages.count;
  if (ps)
    nmdc_stats.cache_psearch += cache.psearch.length + cache.psearch.messages.count;
  if (pm)
    nmdc_stats.cache_messages +=
      cache.privatemessages.length + cache.privatemessages.messages.count;
  if (res)
    nmdc_stats.cache_results += cache.results.length + cache.results.messages.count;

  DPRINTF
    ("//////// %10lu //////// Cache Flush \\\\\\\\\\\\\\\\\\\\\\ %7lu \\\\\\\\\\\\\\\\\\\\\\\n"
     " Chat %d (%lu), MyINFO %d (%lu), MyINFOupdate %d (%lu), as %d (%lu), ps %d (%lu), res %d\n",
     now.tv_sec, now.tv_usec,
     ch, cache.chat.length + cache.chat.messages.count, mi,
     cache.myinfo.length + cache.myinfo.messages.count, miu,
     cache.myinfoupdate.length + cache.myinfoupdate.messages.count, as,
     cache.asearch.length + cache.asearch.messages.count, ps,
     cache.psearch.length + cache.psearch.messages.count, res);

  /*
   * write out buffers 
   */

  if (userlist) {
    for (u = userlist; u; u = n) {
      n = (nmdc_user_t *) u->user.next;

#ifdef DEBUG
      if (u->state == PROTO_STATE_VIRTUAL)
	continue;

      ASSERT (u->user.parent);
#endif

      if (u->state != PROTO_STATE_ONLINE)
	continue;

      ASSERT ((u->ChatCnt + u->SearchCnt + u->ResultCnt + u->MessageCnt) == u->CacheException);

      /* get buffer -- only create exception buffer if really, really necessary */
      if (u->CacheException && ((u->SearchCnt && (u->user.active ? as : ps)) || (u->ChatCnt && ch)
				|| (u->ResultCnt && res)
				|| (u->MessageCnt && pm))) {
	/* we need to copy this buffer cuz it could be buffered during write
	   and it is changed at every call to proto_nmdc_build_buffer */
	if (u->op) {
	  b = bf_copy (buf_exception_op, buf_exception_op->size - bf_used (buf_exception_op));
	} else {
	  b = bf_copy (buf_exception, buf_exception->size - bf_used (buf_exception));
	}
	proto_nmdc_build_buffer (b, u, as, ps, ch, pm, res);
	BF_VERIFY (b);

	DPRINTF (" Exception (%p): res (%lu, %lu) [%d], pm (%lu, %lu), buf (%lu / %lu / %lu)\n", u,
		 cache.results.length + cache.results.messages.count,
		 u->results.length +
		 u->results.messages.count, u->ResultCnt,
		 cache.privatemessages.length + cache.privatemessages.messages.count,
		 u->privatemessages.length +
		 u->privatemessages.messages.count,
		 bf_used (buf_exception), bf_used (b), buf_exception->size);

	if (bf_used (b))
	  if (server_write (u->user.parent, b) > 0)
	    server_settimeout (u->user.parent, PROTO_TIMEOUT_ONLINE);

	bf_free (b);
      } else {
#ifdef ZLINES
	if (u->op) {
	  if (u->supports & NMDC_SUPPORTS_ZPipe) {
	    b = (u->user.active ? buf_zpipeactive_op : buf_zpipepassive_op);
	  } else if (u->supports & NMDC_SUPPORTS_ZLine) {
	    b = (u->user.active ? buf_zlineactive_op : buf_zlinepassive_op);
	  } else {
	    b = (u->user.active ? buf_active_op : buf_passive_op);
	  }
	} else {
	  if (u->supports & NMDC_SUPPORTS_ZPipe) {
	    b = (u->user.active ? buf_zpipeactive : buf_zpipepassive);
	  } else if (u->supports & NMDC_SUPPORTS_ZLine) {
	    b = (u->user.active ? buf_zlineactive : buf_zlinepassive);
	  } else {
	    b = (u->user.active ? buf_active : buf_passive);
	  }
	}
#else
	if (u->op) {
	  b = (u->user.active ? buf_active_op : buf_passive_op);
	} else {
	  b = (u->user.active ? buf_active : buf_passive);
	}
#endif
	if (bf_used (b))
	  if (server_write (u->user.parent, b) > 0)
	    server_settimeout (u->user.parent, PROTO_TIMEOUT_ONLINE);
      }

      /* write out researches to recent clients */
      if (ars || (u->user.active && prs)) {
	if (u->user.joinstamp > deadline) {
	  server_write (u->user.parent, (u->user.active ? buf_aresearch : buf_presearch));
	}
      }
    };
  }
#ifdef ZLINES
  if (buf_zpipepassive && (buf_zpipepassive != buf_passive))
    bf_free (buf_zpipepassive);
  if (buf_zpipeactive && (buf_zpipeactive != buf_active))
    bf_free (buf_zpipeactive);
  if (buf_zlinepassive && (buf_zlinepassive != buf_passive))
    bf_free (buf_zlinepassive);
  if (buf_zlineactive && (buf_zlineactive != buf_active))
    bf_free (buf_zlineactive);
  if (buf_zpipepassive_op && (buf_zpipepassive_op != buf_passive_op))
    bf_free (buf_zpipepassive_op);
  if (buf_zpipeactive_op && (buf_zpipeactive_op != buf_active_op))
    bf_free (buf_zpipeactive_op);
  if (buf_zlinepassive_op && (buf_zlinepassive_op != buf_passive_op))
    bf_free (buf_zlinepassive_op);
  if (buf_zlineactive_op && (buf_zlineactive_op != buf_active_op))
    bf_free (buf_zlineactive_op);
#endif

  bf_free (buf_passive);
  bf_free (buf_active);
  bf_free (buf_exception);
  bf_free (buf_passive_op);
  bf_free (buf_active_op);
  bf_free (buf_exception_op);
  bf_free (buf_aresearch);
  bf_free (buf_presearch);

  if (ch)
    cache_clear (cache.chat);
  if (mi)
    cache_clear (cache.myinfo);
  if (miu)
    cache_clear (cache.myinfoupdate);
  if (miuo)
    cache_clear (cache.myinfoupdateop);
  if (ps)
    cache_clear (cache.psearch);
  if (as)
    cache_clear (cache.asearch);
  if (prs)
    cache_clear (cache.presearch);
  if (ars)
    cache_clear (cache.aresearch);
  if (res)
    cache_clearcount (cache.results);
  if (pm)
    cache_clearcount (cache.privatemessages);

  DPRINTF
    ("\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\ end cache flush /////////////////////////////\n");

  proto_nmdc_user_cachelist_clear ();

  plugin_send_event (nmdc_pluginmanager, NULL, PLUGIN_EVENT_CACHEFLUSH, NULL);
}
