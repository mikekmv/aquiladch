/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#include "cap.h"

/* IMPORTANT add new capabilitie names BEFORE the CAP_DEFAULT name
	otherwise printcapabilities will not work correctly */

/* *INDENT-OFF* */
cap_array_t Capabilities[] = {
	{"default",	CAP_DEFAULT,	"Default capabilities: chat, pmop, dl and search."},
	{"reg",		CAP_REG,	"Default REG capabilities: chat, search, pm, dl."},
	{"vip",		CAP_VIP,	"Default VIP capabilities: chat, search, pm, dl, share."},
	{"kvip",	CAP_KVIP,	"Default KVIP capabilities: chat, search, pm, dl, share, kick."},
	{"op",		CAP_OP,		"Default OP capabilities: chat, search, pm, dl, share, key, kick, ban."},
	{"cheef",	CAP_CHEEF,	"Default CHEEF capabilities: chat, search, pm, dl, share, key, kick, ban, user."},
	{"admin",	CAP_ADMIN,	"Default ADMIN capabilities: chat, search, pm, dl, share, key, kick, ban, user, group, inherit, config."},
	{"owner",	CAP_OWNER,	"Everything. All powerfull. Mostly, capable of hardbanning."},

	{"chat", 	CAP_CHAT,	"Allows the user to chat in main window."},
	{"search",	CAP_SEARCH,	"Allows user to search the hub for files."},
	{"pmop",	CAP_PMOP,	"Allows the user to send private messages to OPs only."},
	{"pm", 		CAP_PM,		"Allows the user to send private messages to anyone."},
	{"dl", 		CAP_DL,		"Allows user to download files."},
	{"share",	CAP_SHARE,	"Allows users to circumvent the share requirements."},
	{"key", 	CAP_KEY,	"Awards the user the much desired \"key\"."},
	{"kick",	CAP_KICK,	"Allows the user to kick other users."},
	{"ban", 	CAP_BAN,	"Allows the user to ban other users."},
	{"config", 	CAP_CONFIG,	"Allows the user to edit the configuratoin of the hub."},
	{"say", 	CAP_SAY,	"Allows the user to use the \"say\" command."},
	{"user", 	CAP_USER,	"Allows the user to add new users."},
	{"group", 	CAP_GROUP,	"Allows the user to add new groups."},
	{"inherit", 	CAP_INHERIT,	"Allows the user to awards rights he posseses to users or groups."},
	{"hardban", 	CAP_BANHARD,	"Allows the user to hardban IPs. A hardban is a bit like firewalling the IP."},
	{"tag",		CAP_TAG,	"This users does not need a tag to get in the hub."},
	{"sharehide",	CAP_SHAREHIDE,	"This hides the share of the user for all excepts ops."},
	{"shareblock",	CAP_SHAREBLOCK,	"This blocks everyone from downloading from this user. Only works for active users."},
	{"spam",	CAP_SPAM,	"Users with this right can post messages as large as they want."},
	{0, 0, 0}
};
/* *INDENT-ON* */
