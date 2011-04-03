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
#include <assert.h>

#include "plugin.h"
#include "user.h"
#include "commands.h"
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
    bf_printf (output, "%.*s\n", bf_used (e->data), e->data->s);

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
