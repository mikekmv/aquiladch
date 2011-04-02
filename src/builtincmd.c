/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>

#include "../config.h"
#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif
#include <arpa/inet.h>


#include "defaults.h"
#include "builtincmd.h"
#include "plugin.h"
#include "commands.h"
#include "utils.h"


unsigned int MinPwdLength;
unsigned long AutoSaveInterval;
unsigned char *ReportTarget;
unsigned long KickMaxBanTime;
unsigned int KickNoBanMayBan;

struct timeval savetime;

/* say command */
unsigned long handler_say (plugin_user_t * user, buffer_t * output, void *priv, unsigned int argc,
			   unsigned char **argv)
{
  return 0;
}

/* say command */
unsigned long handler_shutdown (plugin_user_t * user, buffer_t * output, void *priv,
				unsigned int argc, unsigned char **argv)
{
  exit (0);
  return 0;
}

/* say command */
unsigned long handler_version (plugin_user_t * user, buffer_t * output, void *priv,
			       unsigned int argc, unsigned char **argv)
{
  bf_printf (output, "This is " HUBSOFT_NAME " Version " AQUILA_VERSION);
  return 0;
}

unsigned long handler_massall (plugin_user_t * user, buffer_t * output, void *priv,
			       unsigned int argc, unsigned char **argv)
{
  unsigned int i;
  buffer_t *buf;
  plugin_user_t *tgt = NULL;

  if (argc < 2) {
    bf_printf (output, "Usage: %s <message>", argv[0]);
    return 0;
  }

  /* build message */
  buf = bf_alloc (10240);
  *buf->e = '\0';
  for (i = 1; i < argc; i++)
    bf_printf (buf, " %s", argv[i]);
  if (*buf->s == ' ')
    buf->s++;

  /* send to all users */
  i = 0;
  while (plugin_user_next (&tgt)) {
    plugin_user_priv (NULL, tgt, user, buf, 1);
    i++;
  }
  bf_free (buf);

  /* return result */
  bf_printf (output, "Message sent to %lu users.", i);

  return 0;
}

unsigned long handler_report (plugin_user_t * user, buffer_t * output, void *priv,
			      unsigned int argc, unsigned char **argv)
{
  unsigned int i;
  buffer_t *buf;
  plugin_user_t *tgt = NULL;
  config_element_t *reporttarget;
  struct in_addr source, target;

  if (argc < 2) {
    bf_printf (output, "Usage: %s <nick> <reason>", argv[0]);
    return 0;
  }

  tgt = plugin_user_find (argv[1]);
  if (tgt) {
    target.s_addr = tgt->ipaddress;
  } else {
    target.s_addr = 0;
  }
  source.s_addr = user->ipaddress;

  /* build message */
  buf = bf_alloc (10240);
  *buf->e = '\0';
  bf_printf (buf, "User %s (%s) reports ", user->nick, inet_ntoa (source));
  bf_printf (buf, "%s (%s): ", argv[1], target.s_addr ? inet_ntoa (target) : "not logged in");
  for (i = 2; i < argc; i++)
    bf_printf (buf, " %s", argv[i]);
  if (*buf->s == ' ')
    buf->s++;

  /* find config value */
  reporttarget = config_find ("ReportTarget");
  if (!reporttarget) {
    bf_printf (output, "Report could not be sent: not configured properly.");
    return 0;
  }

  /* extract user */
  tgt = plugin_user_find (*reporttarget->val.v_string);
  if (!tgt) {
    bf_printf (output, "Report could not be sent, %s not found.",
	       **reporttarget->val.v_string ? *reporttarget->val.
	       v_string : (unsigned char *) "target");
    return 0;
  }

  /* send message */
  plugin_user_priv (NULL, tgt, NULL, buf, 0);

  /* return result */
  bf_printf (output, "Report sent to %s.", tgt->nick);

  return 0;
}

unsigned long handler_myip (plugin_user_t * user, buffer_t * output, void *priv, unsigned int argc,
			    unsigned char **argv)
{
  struct in_addr ip;

  ip.s_addr = user->ipaddress;

  bf_printf (output, "Your IP is %s\n", inet_ntoa (ip));

  return 0;
}


unsigned long handler_kick (plugin_user_t * user, buffer_t * output, void *priv, unsigned int argc,
			    unsigned char **argv)
{
  unsigned int i = 0;
  buffer_t *buf;
  plugin_user_t *target;
  struct in_addr ip;
  unsigned char *c;

  if (argc < 2) {
    bf_printf (output, "Usage: %s <nick> <reason>", argv[0]);
    return 0;
  };

  /* rebuild reason */
  buf = bf_alloc (1024);
  *buf->e = '\0';
  bf_printf (buf, "You were kicked because: ");
  for (i = 2; i < argc; i++)
    bf_printf (buf, " %s", argv[i]);

  /* find target */
  target = plugin_user_find (argv[1]);
  if (!target) {
    bf_printf (output, "User %s not found.\n", argv[1]);
    goto leave;
  }

  /* kick the user */
  ip.s_addr = target->ipaddress;
  bf_printf (output, "Kicked user %s (ip %s) because: %.*s", target->nick, inet_ntoa (ip),
	     bf_used (buf), buf->s);

  /* permban the user if the kick string contains _BAN_. */
  if ((c = strcasestr (buf->s, "_BAN_"))) {
    unsigned long total = 0;

    if (KickNoBanMayBan || (user->rights & CAP_BAN)) {
      total = time_parse (c + 5);
      if ((KickMaxBanTime == 0) || ((KickMaxBanTime > 0) && (total <= KickMaxBanTime))
	  || (user->rights & CAP_BAN)) {
	if (total != 0) {
	  bf_printf (output, "\nBanning user for ");
	  time_print (output, total);
	  bf_strcat (output, "\n");
	} else {
	  bf_printf (output, "\nBanning user forever");
	}
	plugin_user_ban (target, buf, total);
	goto leave;
      } else {
	bf_printf (output, "\nSorry. You cannot ban users for longer than ");
	time_print (output, KickMaxBanTime);
	bf_strcat (output, " with a kick.");
      }
    } else {
      bf_printf (output, "\nSorry, you cannot ban.");
    }
  }

  /* actually kick the user. */
  plugin_user_kick (target, buf);

leave:
  bf_free (buf);

  return 0;
}

unsigned long handler_drop (plugin_user_t * user, buffer_t * output, void *priv, unsigned int argc,
			    unsigned char **argv)
{
  unsigned int i = 0;
  buffer_t *buf;
  plugin_user_t *target;
  struct in_addr ip;
  unsigned char *c;

  if (argc < 2) {
    bf_printf (output, "Usage: %s <nick> <reason>", argv[0]);
    return 0;
  };

  /* rebuild reason */
  buf = bf_alloc (1024);
  *buf->e = '\0';
  bf_printf (buf, "You were dropped because: ");
  for (i = 2; i < argc; i++)
    bf_printf (buf, " %s", argv[i]);

  /* find target */
  target = plugin_user_find (argv[1]);
  if (!target) {
    bf_printf (output, "User %s not found.\n", argv[1]);
    goto leave;
  }

  /* kick the user */
  ip.s_addr = target->ipaddress;
  bf_printf (output, "Dropped user %s (ip %s) because: %.*s", target->nick, inet_ntoa (ip),
	     bf_used (buf), buf->s);

  /* permban the user if the kick string contains _BAN_. */
  if ((c = strcasestr (buf->s, "_BAN_"))) {
    unsigned long total = 0;

    if (KickNoBanMayBan || (user->rights & CAP_BAN)) {
      total = time_parse (c + 5);
      if ((KickMaxBanTime == 0) || ((KickMaxBanTime > 0) && (total <= KickMaxBanTime))
	  || (user->rights & CAP_BAN)) {
	if (total != 0) {
	  bf_printf (output, "\nBanning user for ");
	  time_print (output, total);
	  bf_strcat (output, "\n");
	} else {
	  bf_printf (output, "\nBanning user forever");
	}
	plugin_ban_nick (target->nick, buf, total);
	plugin_ban_ip (target->ipaddress, buf, total);
      } else {
	bf_printf (output, "\nSorry. You cannot ban users for longer than ");
	time_print (output, KickMaxBanTime);
	bf_strcat (output, " with a kick.");
      }
    } else {
      bf_printf (output, "\nSorry, you cannot ban.");
    }
  }

  /* actually kick the user. */
  plugin_user_drop (target, buf);

leave:
  bf_free (buf);

  return 0;
}


unsigned long handler_zombie (plugin_user_t * user, buffer_t * output, void *priv,
			      unsigned int argc, unsigned char **argv)
{
  plugin_user_t *zombie;

  if (argc < 2) {
    bf_printf (output, "Usage: %s <nick>", argv[0]);
    return 0;
  }

  zombie = plugin_user_find (argv[1]);
  if (!zombie) {
    bf_printf (output, "User %s not found.", argv[1]);
    return 0;
  }

  plugin_user_zombie (zombie);

  bf_printf (output, "User %s's putrid flesh stinks up the place...", argv[1]);

  return 0;
}

unsigned long handler_unban (plugin_user_t * user, buffer_t * output, void *priv, unsigned int argc,
			     unsigned char **argv)
{
  if (argc < 2) {
    bf_printf (output, "Usage: %s <nick>", argv[0]);
    return 0;
  }

  if (plugin_unban (argv[1])) {
    bf_printf (output, "Nick %s unbanned.", argv[1]);
  } else {
    bf_printf (output, "No ban for nick %s found.", argv[1]);
  }

  return 0;
}

unsigned long handler_unbanip (plugin_user_t * user, buffer_t * output, void *priv,
			       unsigned int argc, unsigned char **argv)
{
  struct in_addr ip;

  if (argc < 2) {
    bf_printf (output, "Usage: %s <ip>", argv[0]);
    return 0;
  };

  if (inet_aton (argv[1], &ip)) {
    if (plugin_unban_ip (ip.s_addr)) {
      bf_printf (output, "IP %s unbanned.", inet_ntoa (ip));
    } else {
      bf_printf (output, "No ban for IP %s found.", inet_ntoa (ip));
    }
  } else {
    bf_printf (output, "Sorry, \"%s\" is not a recognisable IP address.", argv[1]);
  }

  return 0;
}

unsigned long handler_unbanip_hard (plugin_user_t * user, buffer_t * output, void *priv,
				    unsigned int argc, unsigned char **argv)
{
  struct in_addr ip;

  if (argc < 2) {
    bf_printf (output, "Usage: %s <ip>", argv[0]);
    return 0;
  };

  if (inet_aton (argv[1], &ip)) {
    if (plugin_unban_ip_hard (ip.s_addr)) {
      bf_printf (output, "IP %s unbanned.", inet_ntoa (ip));
    } else {
      bf_printf (output, "No ban for IP %s found.", inet_ntoa (ip));
    }
  } else {
    bf_printf (output, "Sorry, \"%s\" is not a recognisable IP address.", argv[1]);
  }

  return 0;
}

unsigned long handler_banip (plugin_user_t * user, buffer_t * output, void *priv, unsigned int argc,
			     unsigned char **argv)
{
  unsigned int i = 0;
  unsigned long period = 0;
  buffer_t *buf;
  plugin_user_t *target;
  struct in_addr ip;

  if (argc < 2) {
    bf_printf (output, "Usage: %s <ip/nick> [<period>] <reason>", argv[0]);
    return 0;
  }

  buf = bf_alloc (1024);
  *buf->e = '\0';

  if (argv[2]) {
    period = time_parse (argv[2]);
  }

  for (i = period ? 3 : 2; i < argc; i++)
    bf_printf (buf, " %s", argv[i]);

  target = plugin_user_find (argv[1]);
  if (target) {
    ip.s_addr = target->ipaddress;
    if (!period) {
      bf_printf (output, "IP Banning user %s (ip %s) forever: %.*s", target->nick, inet_ntoa (ip),
		 bf_used (buf), buf->s);
    } else {
      bf_printf (output, "IP Banning user %s (ip %s) for ", target->nick, inet_ntoa (ip));
      time_print (output, period);
      bf_printf (output, ": %.*s", bf_used (buf), buf->s);
    }
    plugin_user_banip (target, buf, period);
  } else {
    if (inet_aton (argv[1], &ip)) {
      plugin_ban_ip (ip.s_addr, buf, period);
      if (!period) {
	bf_printf (output, "IP Banning %s forever: %.*s.", inet_ntoa (ip), bf_used (buf), buf->s);
      } else {
	bf_printf (output, "IP Banning %s for ", inet_ntoa (ip));
	time_print (output, period);
	bf_printf (output, ": %.*s", bf_used (buf), buf->s);
      }
    } else {
      bf_printf (output, "User not found or IP address not valid: %s\n", argv[1]);
    }
  }


  bf_free (buf);

  return 0;
}

unsigned long handler_bannick (plugin_user_t * user, buffer_t * output, void *priv,
			       unsigned int argc, unsigned char **argv)
{
  unsigned int i = 0;
  unsigned long period = 0;
  buffer_t *buf;
  plugin_user_t *target;

  if (argc < 2) {
    bf_printf (output, "Usage: %s <nick> [<period>] <reason>", argv[0]);
    return 0;
  }

  /* read period */
  if (argv[2]) {
    period = time_parse (argv[2]);
  }
  /* build reason */
  buf = bf_alloc (1024);
  *buf->e = '\0';
  for (i = period ? 3 : 2; i < argc; i++)
    bf_printf (buf, " %s", argv[i]);

  /* find target */
  target = plugin_user_find (argv[1]);
  if (!target) {
    bf_printf (output, "User %s is not online.", argv[1]);
  }

  /* ban him. */
  if (!period) {
    bf_printf (output, "Banned nick %s forever: %.*s", argv[1], bf_used (buf), buf->s);
  } else {
    bf_printf (output, "Banned nick %s for ", argv[1]);
    time_print (output, period);
    bf_printf (output, ": %.*s", bf_used (buf), buf->s);
  }

  if (target) {
    plugin_user_bannick (target, buf, period);
  } else {
    plugin_ban_nick (argv[1], buf, period);
  }

  bf_free (buf);

  return 0;
}


unsigned long handler_ban (plugin_user_t * user, buffer_t * output, void *priv, unsigned int argc,
			   unsigned char **argv)
{
  unsigned int i = 0;
  unsigned long period = 0;
  buffer_t *buf;
  plugin_user_t *target;
  struct in_addr ip;

  if (argc < 2) {
    bf_printf (output, "Usage: %s <nick> [<period>] <reason>", argv[0]);
    return 0;
  }

  if (argv[2])
    period = time_parse (argv[2]);

  buf = bf_alloc (1024);
  *buf->e = '\0';
  for (i = period ? 3 : 2; i < argc; i++)
    bf_printf (buf, " %s", argv[i]);


  target = plugin_user_find (argv[1]);
  if (!target) {
    bf_printf (output, "User %s not found.\n", argv[1]);
    goto leave;
  }

  ip.s_addr = target->ipaddress;

  if (!period) {
    bf_printf (output, "Banning user %s (ip %s) forever: %.*s", target->nick, inet_ntoa (ip),
	       bf_used (buf), buf->s);
  } else {
    bf_printf (output, "Banning user %s (ip %s) for ", target->nick, inet_ntoa (ip));
    time_print (output, period);
    bf_printf (output, ": %.*s", bf_used (buf), buf->s);
  }

  plugin_user_ban (target, buf, period);

leave:
  bf_free (buf);

  return 0;
}

unsigned long handler_banhard (plugin_user_t * user, buffer_t * output, void *priv,
			       unsigned int argc, unsigned char **argv)
{
  unsigned int i = 0;
  unsigned long period = 0;
  buffer_t *buf;
  plugin_user_t *target;
  struct in_addr ip;

  if (argc < 2) {
    bf_printf (output, "Usage: %s <ip/nick> [<period>] <reason>", argv[0]);
    return 0;
  }

  if (argv[2])
    period = time_parse (argv[2]);

  buf = bf_alloc (1024);
  *buf->e = '\0';
  for (i = period ? 3 : 2; i < argc; i++)
    bf_printf (buf, " %s", argv[i]);

  target = plugin_user_find (argv[1]);
  if (target) {
    ip.s_addr = target->ipaddress;
    if (!period) {
      bf_printf (output, "HARD Banning user %s (ip %s) forever: %.*s", target->nick, inet_ntoa (ip),
		 bf_used (buf), buf->s);
    } else {
      bf_printf (output, "HARD Banning user %s (ip %s) for ", target->nick, inet_ntoa (ip));
      time_print (output, period);
      bf_printf (output, ": %.*s", bf_used (buf), buf->s);
    }
    plugin_user_banip_hard (target, buf, period);
  } else {
    if (inet_aton (argv[1], &ip)) {
      if (!period) {
	bf_printf (output, "HARD Banning ip %s forever: %.*s", inet_ntoa (ip), bf_used (buf),
		   buf->s);
      } else {
	bf_printf (output, "HARD Banning ip %s for ", inet_ntoa (ip));
	time_print (output, period);
	bf_printf (output, "%lus: %.*s", bf_used (buf), buf->s);
      }
      plugin_ban_ip_hard (ip.s_addr, buf, period);
    } else {
      bf_printf (output, "User not found or ip address not valid: %s\n", argv[1]);
    }
  }

  bf_free (buf);

  return 0;
}

unsigned long handler_banlist (plugin_user_t * user, buffer_t * output, void *priv,
			       unsigned int argc, unsigned char **argv)
{
  struct in_addr ip;

  if (argc < 2) {
    bf_printf (output, "Usage: %s <ip/nick>", argv[0]);
    return 0;
  }

  if (inet_aton (argv[1], &ip)) {
    if (!plugin_user_findipban (output, ip.s_addr)) {
      bf_printf (output, "No IP bans found for %s", inet_ntoa (ip));
      goto leave;
    }
  } else {
    if (!plugin_user_findnickban (output, argv[1])) {
      bf_printf (output, "No Nick bans found for %s", argv[1]);
      goto leave;
    }
  }

leave:
  return 0;
}

/* help command */
#include "hash.h"
extern command_t cmd_sorted;
extern command_t cmd_hashtable[COMMAND_HASHTABLE];

unsigned long handler_help (plugin_user_t * user, buffer_t * output, void *priv, unsigned int argc,
			    unsigned char **argv)
{
  unsigned int j, hash;
  command_t *cmd, *list;

  if (argc < 2) {
    bf_printf (output, "Available commands:\n");
    for (cmd = cmd_sorted.onext; cmd != &cmd_sorted; cmd = cmd->onext)
      if ((user->rights & cmd->req_cap) || !cmd->req_cap)
	bf_printf (output, "%s: %s\n", cmd->name, cmd->help);

    bf_printf (output, "Commands are preceded with ! or +\n");
    return 0;
  }

  for (j = 1; j < argc; j++) {
    hash = SuperFastHash (argv[j], strlen (argv[j]));
    list = &cmd_hashtable[hash & COMMAND_HASHMASK];
    for (cmd = list->next; cmd != list; cmd = cmd->next)
      if (!strcmp (cmd->name, argv[j]))
	break;

    if (cmd) {
      bf_printf (output, "%s: %s\n", cmd->name, cmd->help);
    } else {
      bf_printf (output, "Command %s not found.\n", argv[j]);
    }
  }

  return 0;
}

/* user handling */
#include "user.h"
extern account_t *accounts;
extern account_type_t *accountTypes;

unsigned long handler_groupadd (plugin_user_t * user, buffer_t * output, void *priv,
				unsigned int argc, unsigned char **argv)
{
  unsigned long cap = 0, ncap = 0;
  config_element_t *defaultrights;

  if (argc < 2) {
    bf_printf (output, "Usage: %s <group> <rights>...\n", argv[0]);
    command_flags_help ((command_flag_t *) Capabilities, output);
    goto leave;
  }

  if (account_type_find (argv[1])) {
    bf_printf (output, "Group %s already exists.\n", argv[1]);
    goto leave;
  }

  if (argc > 2)
    command_flags_parse ((command_flag_t *) Capabilities, output, argc, argv, 2, &cap, &ncap);

  if (!cap) {
    defaultrights = config_find ("user.defaultrights");
    cap = *defaultrights->val.v_cap;
  }

  /* verify if the user can actually assign these extra rights... */
  if (cap && (!(user->rights & CAP_INHERIT))) {
    bf_printf (output, "You are not allowed to assign rights.\n");
    goto leave;
  }
  if (cap & ~user->rights) {
    bf_printf (output, "You are not allowed to assign this group: ");
    command_flags_print ((command_flag_t *) (Capabilities + CAP_PRINT_OFFSET), output,
			 cap & ~user->rights);
    bf_strcat (output, "\n");
    goto leave;
  }
  account_type_add (argv[1], cap);

  bf_printf (output, "Group %s created with: ", argv[1]);
  command_flags_print ((command_flag_t *) (Capabilities + CAP_PRINT_OFFSET), output, cap);
  bf_strcat (output, "\n");

leave:
  return 0;
}

unsigned long handler_groupcap (plugin_user_t * user, buffer_t * output, void *priv,
				unsigned int argc, unsigned char **argv)
{
  unsigned long cap = 0, ncap = 0;
  account_type_t *type;

  if (argc < 2) {
    bf_printf (output, "Usage: %s <group> <[+]right/-right>...\n", argv[0]);
    command_flags_help ((command_flag_t *) Capabilities, output);
    goto leave;
  }

  if (!(type = account_type_find (argv[1]))) {
    bf_printf (output, "Group %s does not exist.\n", argv[1]);
    goto leave;
  }

  if (argc > 2)
    command_flags_parse ((command_flag_t *) Capabilities, output, argc, argv, 2, &cap, &ncap);

  /* verify if the user can actually assign these extra rights... */
  if (cap && (!(user->rights & CAP_INHERIT))) {
    bf_printf (output, "You are not allowed to assign rights.\n");
    goto leave;
  }
  if (cap & ~user->rights) {
    bf_printf (output, "You are not allowed to assign this group: ");
    command_flags_print ((command_flag_t *) (Capabilities + CAP_PRINT_OFFSET), output,
			 cap & ~user->rights);
    bf_strcat (output, "\n");
    goto leave;
  }

  if (ncap & ~user->rights) {
    bf_printf (output, "You are not allowed to remove the following rights from this group: ");
    command_flags_print ((command_flag_t *) (Capabilities + CAP_PRINT_OFFSET), output,
			 ncap & ~user->rights);
    bf_strcat (output, "\n");
    goto leave;
  }

  type->rights |= cap;
  type->rights &= ~ncap;

  bf_printf (output, "Group %s modified. Current rights:", argv[1]);
  command_flags_print ((command_flag_t *) (Capabilities + CAP_PRINT_OFFSET), output, type->rights);
  bf_strcat (output, "\n");

leave:
  return 0;
}

unsigned long handler_groupdel (plugin_user_t * user, buffer_t * output, void *priv,
				unsigned int argc, unsigned char **argv)
{
  account_type_t *type;

  if (argc < 2) {
    bf_printf (output, "Usage: %s <group>", argv[0]);
    return 0;
  }

  if (!(type = account_type_find (argv[1]))) {
    bf_printf (output, "Group %s does not exist.\n", argv[1]);
    goto leave;
  }

  if (type->refcnt) {
    bf_printf (output, "Group %s still has %ld users.\n", argv[1], type->refcnt);
    goto leave;
  }

  account_type_del (type);
  bf_printf (output, "Group %s deleted.\n", argv[1]);

leave:
  return 0;
}

unsigned long handler_grouplist (plugin_user_t * user, buffer_t * output, void *priv,
				 unsigned int argc, unsigned char **argv)
{
  account_type_t *type;

  for (type = accountTypes; type; type = type->next) {
    bf_printf (output, "%s: ", type->name);
    command_flags_print ((command_flag_t *) (Capabilities + CAP_PRINT_OFFSET), output,
			 type->rights);
    bf_strcat (output, "\n");
  };

  return 0;
}

unsigned long handler_userlist (plugin_user_t * user, buffer_t * output, void *priv,
				unsigned int argc, unsigned char **argv)
{
  account_type_t *type;
  account_t *account;
  unsigned int i, cnt;

  if (argc == 1) {
    for (cnt = 0, account = accounts; account; account = account->next) {
      bf_printf (output, "%s (%s),", account->nick, account->classp->name);
      cnt++;
    };
    /* remove trailing , */
    if (cnt) {
      output->e--;
      *output->e = '\0';
    }
  } else {
    for (i = 1; i < argc; i++) {
      type = account_type_find (argv[i]);
      if (!type) {
	bf_printf (output, "Group %s does not exist.\n", argv[1]);
	continue;
      }
      bf_printf (output, "Group %s: ", type->name);
      for (cnt = 0, account = accounts; account; account = account->next) {
	if (account->class == type->id) {
	  bf_printf (output, "%s,", account->nick);
	  cnt++;
	}
      };
      /* remove trailing , */
      if (cnt) {
	output->e--;
	*output->e = '\0';
      }
      bf_strcat (output, "\n");
    }
  }

  return 0;
}

unsigned long handler_useradd (plugin_user_t * user, buffer_t * output, void *priv,
			       unsigned int argc, unsigned char **argv)
{
  unsigned long cap = 0, ncap = 0;
  account_type_t *type;
  account_t *account;
  plugin_user_t *target;

  if (argc < 3) {
    bf_printf (output, "Usage: %s <nick> <group> [<rights...>]", argv[0]);
    return 0;
  }

  if (account_find (argv[1])) {
    bf_printf (output, "User %s already exists.\n", argv[1]);
    goto leave;
  }
  if (!(type = account_type_find (argv[2]))) {
    bf_printf (output, "Group %s does not exists.\n", argv[2]);
    goto leave;
  }

  if (argc > 3)
    command_flags_parse ((command_flag_t *) Capabilities, output, argc, argv, 3, &cap, &ncap);

  /* verify the user can assign users to this group */
  if (type->rights & ~user->rights) {
    bf_printf (output, "You are not allowed to assign a user to this group.\n");
    goto leave;
  }

  /* verify if the user can actually assign these extra rights... */
  if (cap && (!(user->rights & CAP_INHERIT))) {
    bf_printf (output, "You are not allowed to assign a user extra rights.\n");
    goto leave;
  }
  if (cap & ~user->rights) {
    bf_printf (output, "You are not allowed to assign this user: ");
    command_flags_print ((command_flag_t *) (Capabilities + CAP_PRINT_OFFSET), output,
			 cap & ~user->rights);
    bf_strcat (output, "\n");
    goto leave;
  }

  account = account_add (type, argv[1]);
  account->rights |= cap;

  bf_printf (output, "User %s created with group %s.\nCurrent rights:", account->nick, type->name);
  command_flags_print ((command_flag_t *) (Capabilities + CAP_PRINT_OFFSET), output,
		       account->rights | type->rights);
  bf_strcat (output, "\n");

  /* if user is online, warm him of his reg and notify to op we did so. */
  target = plugin_user_find (argv[1]);
  if (target) {
    buffer_t *message;

    /* warn user */
    message = bf_alloc (1024);
    bf_printf (message,
	       "You have been registered by %s. Please use !passwd <password> to set a password "
	       "and relogin to gain your new rights. You have been assigned:\n", user->nick);
    command_flags_print ((command_flag_t *) (Capabilities + CAP_PRINT_OFFSET), message,
			 account->rights | type->rights);
    plugin_user_sayto (NULL, target, message);
    bf_free (message);

    bf_printf (output,
	       "User is already logged in. He has been told to set a password and to relogin.\n");
  } else {
    bf_printf (output,
	       "No user with nickname %s is currently logged in. Please notify the user yourself.\n",
	       argv[1]);
  }
leave:
  return 0;
}

unsigned long handler_usercap (plugin_user_t * user, buffer_t * output, void *priv,
			       unsigned int argc, unsigned char **argv)
{
  unsigned long cap = 0, ncap = 0;
  account_t *account;

  if (argc < 3) {
    bf_printf (output, "Usage: %s <nick> <[+]right/-right>...\n", argv[0]);
    command_flags_help ((command_flag_t *) Capabilities, output);
    goto leave;
  }

  if (!(user->rights & CAP_INHERIT)) {
    bf_printf (output, "You are not allowed to assign a user extra rights.\n");
    goto leave;
  }

  if (!(account = account_find (argv[1]))) {
    bf_printf (output, "User %s not found.\n", argv[1]);
    goto leave;
  }

  if (argc > 2)
    command_flags_parse ((command_flag_t *) Capabilities, output, argc, argv, 2, &cap, &ncap);

  /* verify if the user can actually assign these extra rights... */
  if (cap & ~user->rights) {
    bf_printf (output, "You are not allowed to assign this user: ");
    command_flags_print ((command_flag_t *) (Capabilities + CAP_PRINT_OFFSET), output,
			 cap & ~user->rights);
    bf_strcat (output, "\n");
    goto leave;
  }
  if (ncap & ~user->rights) {
    bf_printf (output, "You are not allowed to touch: ");
    command_flags_print ((command_flag_t *) (Capabilities + CAP_PRINT_OFFSET), output,
			 ncap & ~user->rights);
    bf_strcat (output, "\n");
    goto leave;
  }

  account->rights |= cap;
  account->rights &= ~ncap;

  /* warn if rights could not be successfully deleted. */
  if (account->classp->rights & ncap) {
    bf_printf (output, "Warning! User %s still has rights ", account->nick);
    command_flags_print ((command_flag_t *) (Capabilities + CAP_PRINT_OFFSET), output,
			 ncap & account->classp->rights);
    bf_printf (output, " from his group!\n");
  }


  bf_printf (output, "User %s created with group %s.\nCurrent rights:", account->nick,
	     account->classp->name);
  command_flags_print ((command_flag_t *) (Capabilities + CAP_PRINT_OFFSET), output,
		       account->rights | account->classp->rights);
  bf_strcat (output, "\n");
  if (plugin_user_find (argv[1]))
    bf_printf (output,
	       "User is already logged in. Tell him to rejoin to gain all his new rights.\n");

leave:
  return 0;
}

unsigned long handler_userdel (plugin_user_t * user, buffer_t * output, void *priv,
			       unsigned int argc, unsigned char **argv)
{
  account_t *account;

  if (argc < 2) {
    bf_printf (output, "Usage: %s <nick>", argv[0]);
    return 0;
  }

  if (!(account = account_find (argv[1]))) {
    bf_printf (output, "User account %s does not exist.\n", argv[1]);
    goto leave;
  }

  account_del (account);
  bf_printf (output, "User account %s deleted.\n", argv[1]);
  if (plugin_user_find (argv[1]))
    bf_printf (output,
	       "User is still logged in with all rights of the deleted account. Kick the user to take them away.\n");

leave:
  return 0;
}

unsigned long handler_userinfo (plugin_user_t * user, buffer_t * output, void *priv,
				unsigned int argc, unsigned char **argv)
{
  account_t *account = NULL;
  struct in_addr in;
  plugin_user_t *target;

  if (argc < 2) {
    bf_printf (output, "Usage: %s <nick>", argv[0]);
    return 0;
  }

  target = plugin_user_find (argv[1]);
  if (!target) {
    bf_printf (output, "User %s is not online.\n", argv[1]);
  } else {
    bf_printf (output, "User information for user %s.\n", target->nick);
  };

  if ((account = account_find (argv[1]))) {
    bf_printf (output, "Group %s, Rights: ", account->classp->name);
    command_flags_print ((command_flag_t *) (Capabilities + CAP_PRINT_OFFSET), output,
			 account->rights | account->classp->rights);
    bf_strcat (output, "\n");
  }

  if (target) {
    in.s_addr = target->ipaddress;
    bf_printf (output, "IP %s\n", inet_ntoa (in));
    bf_printf (output, "Using Client %s version %s\n", target->client, target->versionstring);
    bf_printf (output, "Client claims to be %s and is sharing %s (%llu bytes)\n",
	       target->active ? "active" : "passive", format_size (target->share), target->share);
    bf_printf (output, "Hubs: (%u, %u, %u), Slots %u\n", target->hubs[0], target->hubs[1],
	       target->hubs[2], target->slots);
  };

  return 0;
}

unsigned long handler_usergroup (plugin_user_t * user, buffer_t * output, void *priv,
				 unsigned int argc, unsigned char **argv)
{
  account_t *account = NULL;
  account_type_t *group = NULL, *old;

  if (argc < 3) {
    bf_printf (output, "Usage: %s <nick> <group>", argv[0]);
    return 0;
  }

  if (!(account = account_find (argv[1]))) {
    bf_printf (output, "User %s not found.", argv[1]);
    return 0;
  }

  if (!(group = account_type_find (argv[2]))) {
    bf_printf (output, "User %s not found.", argv[2]);
    return 0;
  }

  /* move to new group */
  old = account->classp;
  account->class = group->id;
  account->classp->refcnt--;
  account->classp = group;
  group->refcnt++;

  bf_printf (output, "Moved user %s from group %s to %s\n", account->nick, old->name, group->name);

  return 0;
}

unsigned long handler_passwd (plugin_user_t * user, buffer_t * output, void *priv,
			      unsigned int argc, unsigned char **argv)
{
  account_t *account = NULL;
  unsigned char *passwd;

  if (argc < 2) {
    bf_printf (output,
	       "Usage: %s [<nick>] <password>\n If no nick is specified, your own password is changed.",
	       argv[0]);
    return 0;
  }

  if (argc > 2) {
    account = account_find (argv[1]);
    if (!account) {
      bf_printf (output, "User %s not found.\n", argv[1]);
      goto leave;
    }
    passwd = argv[2];

    if (!(user->rights & CAP_USER) || (user->rights <= account->rights)) {
      bf_printf (output, "You are not allowed to change %s\'s password\n", account->nick);
      goto leave;
    }
  } else {
    passwd = argv[1];
    account = account_find (user->nick);
    if (!account) {
      bf_printf (output, "No account for user %s.\n", user->nick);
      goto leave;
    }
  }


  if ((strlen (passwd) < MinPwdLength) || !strcmp (passwd, account->passwd)) {
    bf_printf (output,
	       "The password is unacceptable, please choose another. Minimum length is %d\n",
	       MinPwdLength);
    goto leave;
  }

  /* copy the password */
  account_pwd_set (account, passwd);
  bf_printf (output, "Password set.\n");
leave:
  return 0;
}

unsigned long handler_pwgen (plugin_user_t * user, buffer_t * output, void *priv,
			     unsigned int argc, unsigned char **argv)
{
  account_t *account = NULL;
  unsigned char passwd[NICKLENGTH];
  plugin_user_t *target;
  unsigned int i;
  buffer_t *message;

  if ((argc < 1) || (argc > 2)) {
    bf_printf (output,
	       "Usage: %s [<nick>]\n If no nick is specified, your own password is changed.",
	       argv[0]);
    return 0;
  }

  if (argc > 1) {
    account = account_find (argv[1]);
    if (!account) {
      bf_printf (output, "User %s not found.\n", argv[1]);
      goto leave;
    }

    if (!(user->rights & CAP_USER) || (user->rights <= account->rights)) {
      bf_printf (output, "You are not allowed to change %s\'s password\n", account->nick);
      goto leave;
    }

    target = plugin_user_find (argv[1]);
  } else {
    account = account_find (user->nick);
    if (!account) {
      bf_printf (output, "No account for user %s.\n", user->nick);
      goto leave;
    }
    target = user;
  }

  for (i = 0; i < (NICKLENGTH - 1); i++) {
    passwd[i] = (33 + (random () % 90));
  }
  passwd[i] = '\0';

  if (target) {
    message = bf_alloc (1024);
    bf_printf (message, "Your password was reset. It is now\n%*s\n", NICKLENGTH - 1, passwd);
    plugin_user_priv (NULL, target, NULL, message, 1);
    bf_free (message);
  } else {
    bf_printf (output, "The password of %s was reset. It is now\n%*s\n", argv[1], NICKLENGTH - 1,
	       passwd);
  }

  /* copy the password */
  account_pwd_set (account, passwd);
  bf_printf (output, "Password set.\n");
leave:
  return 0;
}

/************************** config ******************************/

#include "config.h"
extern config_element_t config_sorted;

int printconfig (buffer_t * buf, config_element_t * elem)
{
  switch (elem->type) {
    case CFG_ELEM_PTR:
      bf_printf (buf, "%s %p\n", elem->name, *elem->val.v_ptr);
      break;
    case CFG_ELEM_LONG:
      bf_printf (buf, "%s %ld\n", elem->name, *elem->val.v_long);
      break;
    case CFG_ELEM_ULONG:
      bf_printf (buf, "%s %lu\n", elem->name, *elem->val.v_ulong);
      break;
    case CFG_ELEM_ULONGLONG:
      bf_printf (buf, "%s %llu\n", elem->name, *elem->val.v_ulonglong);
      break;
    case CFG_ELEM_CAP:
      bf_printf (buf, "%s ", elem->name);
      command_flags_print ((command_flag_t *) (Capabilities + CAP_PRINT_OFFSET), buf,
			   *elem->val.v_cap);
      bf_strcat (buf, "\n");
      break;
    case CFG_ELEM_INT:
      bf_printf (buf, "%s %d\n", elem->name, *elem->val.v_int);
      break;
    case CFG_ELEM_UINT:
      bf_printf (buf, "%s %u\n", elem->name, *elem->val.v_int);
      break;
    case CFG_ELEM_DOUBLE:
      bf_printf (buf, "%s %lf\n", elem->name, *elem->val.v_double);
      break;
    case CFG_ELEM_STRING:
      bf_printf (buf, "%s \"%s\"\n", elem->name,
		 *elem->val.v_string ? *elem->val.v_string : (unsigned char *) "(NULL)");
      break;
    case CFG_ELEM_IP:
      {
	struct in_addr ia;

	ia.s_addr = *elem->val.v_ip;
	bf_printf (buf, "%s %s\n", elem->name, inet_ntoa (ia));
      }
      break;
    default:
      bf_printf (buf, "%s !Unknown Type!\n", elem->name);
  }
  return 0;
}

unsigned long handler_configshow (plugin_user_t * user, buffer_t * output, void *priv,
				  unsigned int argc, unsigned char **argv)
{
  unsigned int i;
  config_element_t *elem;

  if (argc < 2) {
    for (elem = config_sorted.onext; elem != &config_sorted; elem = elem->onext)
      printconfig (output, elem);
  } else {
    for (i = 1; i < argc; i++) {
      elem = config_find (argv[i]);
      if (!elem) {
	bf_printf (output, "Sorry, unknown configuration value %s\n", argv[i]);
	continue;
      }
      printconfig (output, elem);
    }
  };

  return 0;
}

unsigned long handler_confighelp (plugin_user_t * user, buffer_t * output, void *priv,
				  unsigned int argc, unsigned char **argv)
{
  unsigned int i;
  config_element_t *elem;

  if (argc < 2) {
    for (elem = config_sorted.onext; elem != &config_sorted; elem = elem->onext)
      bf_printf (output, "%s: %s\n", elem->name, elem->help);
  } else {
    for (i = 1; i < argc; i++) {
      elem = config_find (argv[i]);
      if (!elem) {
	bf_printf (output, "Sorry, unknown configuration value %s\n", argv[i]);
	continue;
      }
      bf_printf (output, "%s: %s\n", elem->name, elem->help);
    }
  };

  return 0;
}

unsigned long handler_configset (plugin_user_t * user, buffer_t * output, void *priv,
				 unsigned int argc, unsigned char **argv)
{
  unsigned long cap = 0, ncap = 0;
  config_element_t *elem;

  if (argc < 3) {
    bf_printf (output, "Usage: %s <setting> <value>", argv[0]);
    return 0;
  }

  elem = config_find (argv[1]);
  if (!elem) {
    bf_printf (output, "Sorry, unknown configuration value %s\n", argv[1]);
    goto leave;
  }

  bf_strcat (output, "Old: ");
  printconfig (output, elem);
  switch (elem->type) {
    case CFG_ELEM_PTR:
      sscanf (argv[2], "%p", elem->val.v_ptr);
      break;
    case CFG_ELEM_LONG:
      sscanf (argv[2], "%ld", elem->val.v_long);
      break;
    case CFG_ELEM_ULONG:
      sscanf (argv[2], "%lu", elem->val.v_ulong);
      break;
    case CFG_ELEM_ULONGLONG:
      sscanf (argv[2], "%Lu", elem->val.v_ulonglong);
      break;
    case CFG_ELEM_CAP:
      if (!(user->rights & CAP_INHERIT)) {
	bf_printf (output, "You are not allowed to assign rights.\n");
	break;
      }
      command_flags_parse ((command_flag_t *) Capabilities, output, argc, argv, 2, &cap, &ncap);
      if (cap & ~user->rights) {
	bf_printf (output, "You are not allowed to assign: ");
	command_flags_print ((command_flag_t *) (Capabilities + CAP_PRINT_OFFSET), output,
			     cap & ~user->rights);
	bf_strcat (output, "\n");
	break;
      }
      if (ncap & ~user->rights) {
	bf_printf (output, "You are not allowed to remove: ");
	command_flags_print ((command_flag_t *) (Capabilities + CAP_PRINT_OFFSET), output,
			     ncap & ~user->rights);
	bf_strcat (output, "\n");
	break;
      }
      *elem->val.v_cap |= cap;
      *elem->val.v_cap &= ~ncap;
      break;
    case CFG_ELEM_INT:
      sscanf (argv[2], "%d", elem->val.v_int);
      break;
    case CFG_ELEM_UINT:
      sscanf (argv[2], "%u", elem->val.v_uint);
      break;
    case CFG_ELEM_DOUBLE:
      sscanf (argv[2], "%lf", elem->val.v_double);
      break;
    case CFG_ELEM_STRING:
      if (*elem->val.v_string)
	free (*elem->val.v_string);
      *elem->val.v_string = strdup (argv[2]);
      break;
    case CFG_ELEM_IP:
      {
	struct in_addr ia;

	if (!inet_aton (argv[2], &ia)) {
	  bf_printf (output, "\"%s\" is not a valid IP address.\n");
	  break;
	}
	*elem->val.v_ip = ia.s_addr;
      }
      break;
    default:
      bf_printf (output, "%s !Unknown Type!\n", elem->name);
  }

  bf_strcat (output, "New: ");
  printconfig (output, elem);

  plugin_user_event (user, PLUGIN_EVENT_CONFIG, elem);

leave:
  return 0;
}


unsigned long handler_save (plugin_user_t * user, buffer_t * output, void *priv, unsigned int argc,
			    unsigned char **argv)
{
  unsigned long retval = plugin_config_save (output);

  bf_printf (output, "All Data saved.");

  /* reset autosave counter */
  if (AutoSaveInterval)
    gettimeofday (&savetime, NULL);

  return retval;
}
unsigned long handler_load (plugin_user_t * user, buffer_t * output, void *priv, unsigned int argc,
			    unsigned char **argv)
{
  unsigned long retval = plugin_config_load ();

  bf_printf (output, "Data reloaded.");

  return retval;
}

unsigned long handler_autosave (plugin_user_t * user, void *ctxt, unsigned long event, void *token)
{
  struct timeval now;
  buffer_t *output;
  unsigned int l;

  if (!AutoSaveInterval)
    return 0;

  gettimeofday (&now, NULL);
  if (now.tv_sec > (savetime.tv_sec + (time_t) AutoSaveInterval)) {
    savetime = now;
    output = bf_alloc (1024);
    bf_printf (output, "Errors during autosave:\n");
    l = bf_used (output);
    plugin_config_save (output);
    if (bf_used (output) != l) {
      plugin_report (output);
    }
    bf_free (output);
  }

  return 0;
}

/************************** INIT ******************************/

int builtincmd_init ()
{
  /* *INDENT-OFF* */
  MinPwdLength        = DEFAULT_MINPWDLENGTH;
  AutoSaveInterval    = DEFAULT_AUTOSAVEINTERVAL;
  ReportTarget      = strdup ("");
  KickMaxBanTime      = 0;
  KickNoBanMayBan      = 0;
  
  config_register ("MinPwdLength",     CFG_ELEM_UINT,   &MinPwdLength,     "Minimum length of a password.");
  config_register ("AutoSaveInterval", CFG_ELEM_ULONG,  &AutoSaveInterval, "Period for autosaving settings, set to 0 to disable.");
  config_register ("ReportTarget",     CFG_ELEM_STRING, &ReportTarget,     "User to send report to. Can be a chatroom.");
  config_register ("kickmaxbantime",   CFG_ELEM_ULONG,  &KickMaxBanTime,   "This is the maximum bantime you can give with a kick (and using _ban_). This does not affect someone with the ban right.");
  config_register ("kicknobanmayban",  CFG_ELEM_UINT,   &KickNoBanMayBan,  "If set, then a user without the ban right can use _ban_ to ban anyway. The maximum time can be set with kickmaxbantime.");


  /* command_register ("say",      &handler_say,  CAP_SAY,   "Make the HubSec say something."); */
  command_register ("shutdown",	  &handler_shutdown,     CAP_ADMIN,   "Shut the hub down.");
  command_register ("report",     &handler_report,       0,           "Send a report to the OPs.");
  command_register ("version",    &handler_version,      0,           "Displays the Aquila version.");
  command_register ("myip",       &handler_myip,         0,           "Shows you your IP address.");
  command_register ("kick",       &handler_kick,         CAP_KICK,    "Kick a user. Automatic short ban included.");
  command_register ("drop",       &handler_drop,         CAP_KICK,    "Drop a user. Automatic short ban included.");
  command_register ("banip",      &handler_banip,        CAP_BAN,     "IP Ban a user by IP address.");
  command_register ("bannick",    &handler_bannick,      CAP_BAN,     "Nick ban a user by nick.");
  command_register ("ban",        &handler_ban,          CAP_BAN,     "Ban a user by nick.");
  command_register ("banlist",    &handler_banlist,      CAP_BAN,     "Show ban by nick/IP.");
  command_register ("help",       &handler_help,         0,           "Display help message.");
  command_register ("unban",      &handler_unban,        CAP_BAN,     "Unban a nick.");
  command_register ("unbanip",    &handler_unbanip,      CAP_BAN,     "Unban an ip.");
  command_register ("baniphard",  &handler_banhard,      CAP_BANHARD, "Hardban an IP.");
  command_register ("unbaniphard",&handler_unbanip_hard, CAP_BANHARD, "Unhardban an IP.");
  command_register ("zombie",     &handler_zombie,       CAP_KICK,    "Zombie a user. Can't talk or pm.");

  command_register ("massall",	  &handler_massall,    CAP_ADMIN,     "Send a private message to all users.");

  command_register ("groupadd",   &handler_groupadd,   CAP_GROUP,     "Add a user group.");
  command_register ("groupdel",   &handler_groupdel,   CAP_GROUP,     "Delete a user group.");
  command_register ("grouprights",&handler_groupcap,   CAP_GROUP,     "Edit the rights of a user group.");
  command_register ("grouplist",  &handler_grouplist,  CAP_GROUP,     "List all groups with their rights.");

  command_register ("useradd",    &handler_useradd,    CAP_USER,      "Add a user.");
  command_register ("userdel",    &handler_userdel,    CAP_USER,      "Delete a user.");
  command_register ("userrights", &handler_usercap,    CAP_USER|CAP_INHERIT,  "Edit the extra rights of a user.");
  command_register ("userlist",   &handler_userlist,   CAP_USER,      "List all users of a user group.");
  command_register ("userinfo",   &handler_userinfo,   CAP_KICK,      "Show information of user.");
  command_register ("usergroup",  &handler_usergroup,  CAP_USER,      "Move user to new group. Reconnect for change to activate.");

  command_register ("configshow", &handler_configshow, CAP_CONFIG,    "Show configuration.");
  command_register ("confighelp", &handler_confighelp, CAP_CONFIG,    "Show configuration value help string.");
  command_register ("configset",  &handler_configset,  CAP_CONFIG,    "Set configuration values.");
  command_register ("=",     	  &handler_configset,  CAP_CONFIG,    "Set configuration values.");

  command_register ("load",       &handler_load,       CAP_CONFIG,    "Reload all data from files. WARNING: All unsaved changes will be discarded.");
  command_register ("save",       &handler_save,       CAP_CONFIG,    "Save all changes to file. WARNING: All previously saved settings will be lost!");
  
  command_register ("passwd",     &handler_passwd,	0,            "Change your password.");
  command_register ("pwgen",      &handler_pwgen,	0,            "Let " HUBSOFT_NAME " generate a random password.");

  gettimeofday (&savetime, NULL);
  
  plugin_request (NULL, PLUGIN_EVENT_CACHEFLUSH, (plugin_event_handler_t *)handler_autosave);

  /* *INDENT-ON* */

  return 0;
}
