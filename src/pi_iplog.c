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
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "plugin.h"
#include "user.h"
#include "commands.h"
#include "hub.h"
#include "banlistclient.h"
#include "utils.h"
#include "defaults.h"

#include "esocket.h"

typedef struct iplog_entry {
  dllist_entry_t dllist;

  unsigned char nick[NICKLENGTH];
  unsigned long ip;
  // unsigned long login; NA
  unsigned long logout;
} iplog_entry_t;

plugin_t *plugin_iplog;

unsigned int iplog_count = 0;

unsigned int iplog_length = 0;

dllist_entry_t iplog;

unsigned long pi_iplog_event_logout (plugin_user_t * user, void *dummy, unsigned long event,
				     buffer_t * token)
{
  iplog_entry_t *entry;

  /* no logging */
  if (!iplog_length)
    return PLUGIN_RETVAL_CONTINUE;

  /* if queue is full, reuse last element */
  if (iplog_count >= iplog_length) {
    entry = dllist_prev (&iplog);
    dllist_del (&entry->dllist);
    iplog_count--;
    /* this happens when the list is made shorter */
    while (iplog_count >= iplog_length) {
      free (entry);
      entry = dllist_prev (&iplog);
      dllist_del (&entry->dllist);
      iplog_count--;
    }
  } else {
    entry = malloc (sizeof (iplog_entry_t));
    if (!entry)
      return PLUGIN_RETVAL_CONTINUE;
  }

  strncpy (entry->nick, user->nick, NICKLENGTH);
  entry->ip = user->ipaddress;
  entry->logout = time (NULL);

  dllist_append (&iplog, &entry->dllist);
  iplog_count++;

  return PLUGIN_RETVAL_CONTINUE;
}

/*************************************************************************************************************************/

unsigned long pi_iplog_handler_clientlist (plugin_user_t * user, buffer_t * output, void *dummy,
					   unsigned int argc, unsigned char **argv)
{
  iplog_entry_t *entry;
  struct in_addr ia;
  unsigned char *nick;
  unsigned int i = 0;

  if (!iplog_length) {
    bf_printf (output, "No IP logging enabled.\n");
    return 0;
  }

  if (argc > 1) {
    nick = argv[1];
  } else {
    nick = NULL;
  }

  bf_printf (output, "Logout time  :  IP  : Nick\n");
  dllist_foreach (&iplog, entry) {
    if (nick && strncasecmp (nick, entry->nick, NICKLENGTH))
      continue;
    ia.s_addr = entry->ip;
    bf_printf (output, "%24.24s : %s : %s\n", ctime (&entry->logout), inet_ntoa (ia), entry->nick);
    i++;
  }

  if (!i) {
    bf_printf (output, "No IPs found.\n");
  }

  return 0;
}

/*************************************************************************************************************************/

int pi_iplog_init ()
{
  plugin_iplog = plugin_register ("iplog");

  config_register ("iplog.length", CFG_ELEM_UINT, &iplog_length, "Number of IPs to remember.");

  plugin_request (plugin_iplog, PLUGIN_EVENT_LOGOUT,
		  (plugin_event_handler_t *) & pi_iplog_event_logout);

  command_register ("iplog", &pi_iplog_handler_clientlist, CAP_KEY, "List iplogs.");

  dllist_init (&iplog);

  return 0;
}
