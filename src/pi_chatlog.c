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
#include "stringlist.h"

/*********************************************************************************************/

plugin_t *plugin_chatlog = NULL;
string_list_t chatlog;
unsigned long chatlogmax = 0;

/*********************************************************************************************/

unsigned long pi_chatlog_handler_chatlog (plugin_user_t * user, buffer_t * output, void *dummy,
					  unsigned int argc, unsigned char **argv)
{
  string_list_entry_t *e;

  if (!chatlogmax) {
    bf_printf (output, "No Chat History configured.\n");
    return 0;
  }

  bf_printf (output, "Chat History:\n");
  for (e = chatlog.first; e; e = e->next)
    bf_printf (output, "%*s\n", bf_used (e->data), e->data->s);

  return 0;
}

unsigned long pi_chatlog_handler_chat (plugin_user_t * user, void *priv, unsigned long event,
				       buffer_t * token)
{
  unsigned char *c;

  if (!chatlogmax)
    return PLUGIN_RETVAL_CONTINUE;

  for (c = token->s; *c && (*c != '>'); c++);
  c += 2;
  if ((*c == '!') || (*c == '+'))
    return PLUGIN_RETVAL_CONTINUE;

  string_list_add (&chatlog, NULL, token);

  while (chatlog.count > chatlogmax)
    string_list_del (&chatlog, chatlog.first);

  return PLUGIN_RETVAL_CONTINUE;
}

/********************************* INIT *************************************/

int pi_chatlog_init ()
{
  plugin_chatlog = plugin_register ("chatlog");

  string_list_init (&chatlog);

  plugin_request (plugin_chatlog, PLUGIN_EVENT_CHAT, &pi_chatlog_handler_chat);

  config_register ("chatlog.lines", CFG_ELEM_ULONG, &chatlogmax,
		   "Maximum number of chat history lines.");

  command_register ("chatlog", &pi_chatlog_handler_chatlog, 0, "Command returns last chatlines.");

  return 0;
}
