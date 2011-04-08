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

#include "cap.h"
#include "gettext.h"

/* IMPORTANT add new capabilitie names BEFORE the CAP_DEFAULT name
	otherwise printcapabilities will not work correctly */

/* *INDENT-OFF* */
flag_t Capabilities[] = {
	{"default",	CAP_DEFAULT,	_("Default capabilities: chat, pmop, dl and search.")},
	{"reg",		CAP_REG,	_("Default REG capabilities: chat, search, pm, dl.")},
	{"vip",		CAP_VIP,	_("Default VIP capabilities: chat, search, pm, dl, share.")},
	{"kvip",	CAP_KVIP,	_("Default KVIP capabilities: chat, search, pm, dl, share, kick.")},
	{"op",		CAP_OP,		_("Default OP capabilities: chat, search, pm, dl, share, key, kick, ban.")},
	{"cheef",	CAP_CHEEF,	_("Default CHEEF capabilities: chat, search, pm, dl, share, key, kick, ban, user.")},
	{"admin",	CAP_ADMIN,	_("Default ADMIN capabilities: chat, search, pm, dl, share, key, kick, ban, user, group, inherit, config.")},
	{"owner",	CAP_OWNER,	_("Everything. All powerfull. Mostly, capable of hardbanning.")},

	{"chat", 	CAP_CHAT,	_("Allows the user to chat in main window.")},
	{"search",	CAP_SEARCH,	_("Allows user to search the hub for files.")},
	{"pmop",	CAP_PMOP,	_("Allows the user to send private messages to OPs only.")},
	{"pm", 		CAP_PM,		_("Allows the user to send private messages to anyone.")},
	{"dl", 		CAP_DL,		_("Allows user to download files.")},
	{"share",	CAP_SHARE,	_("Allows users to circumvent the share requirements.")},
	{"key", 	CAP_KEY,	_("Awards the user the much desired \"key\".")},
	{"kick",	CAP_KICK,	_("Allows the user to kick other users.")},
	{"ban", 	CAP_BAN,	_("Allows the user to ban other users.")},
	{"config", 	CAP_CONFIG,	_("Allows the user to edit the configuratoin of the hub.")},
	{"say", 	CAP_SAY,	_("Allows the user to use the \"say\" command.")},
	{"user", 	CAP_USER,	_("Allows the user to add new users.")},
	{"group", 	CAP_GROUP,	_("Allows the user to add new groups.")},
	{"inherit", 	CAP_INHERIT,	_("Allows the user to awards rights he posseses to users or groups.")},
	{"hardban", 	CAP_BANHARD,	_("Allows the user to hardban IPs. A hardban is a bit like firewalling the IP.")},
	{"tag",		CAP_TAG,	_("This users does not need a tag to get in the hub.")},
	{"sharehide",	CAP_SHAREHIDE,	_("This hides the share of the user for all excepts ops.")},
	{"shareblock",	CAP_SHAREBLOCK,	_("This blocks everyone from downloading from this user. Only works for active users.")},
	{"spam",	CAP_SPAM,	_("Users with this right can post messages as large as they want.")},
	{"nosrchlimit", CAP_NOSRCHLIMIT,_("Users with this right are not subject to search limitations.")},
	{"sourceverify",CAP_SOURCEVERIFY,_("Users with this right are only allowed into the hub if their source IP is listed for their nick in the userrestict list.")},
	{"redirect",    CAP_REDIRECT,    _("Users with this right are allowed to redirect users.")},
	{"locallan",    CAP_LOCALLAN,    _("Users with this right are allowed to use locallan ips (and avoid ctm and asearch checks).")},
	{0, 0, 0}
};
/* *INDENT-ON* */
