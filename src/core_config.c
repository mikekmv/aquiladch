/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#include "config.h"
#include "core_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "hash.h"
#include "cap.h"

config_t config;

int core_config_init () {
	config.ListenPort  = DEFAULT_PORT;
	config.NMDCExtraPorts = strdup (NMDC_EXTRA_PORTS);
	config.HubName     = strdup (HUBNAME);
	config.HubDesc     = strdup (HUBDESC);
	config.HubOwner    = strdup (HUBOWNER);
	config.Hostname	   = strdup (DEFAULT_ADDRESS);
	config.HubSecurityNick = strdup (DEFAULT_HUBSECURITY_NICK);
	config.HubSecurityDesc = strdup (DEFAULT_HUBSECURITY_DESCRIPTION);

	config.MaxDescriptionLength = DEFAULT_MAXDESCRIPTIONLENGTH;
	config.MaxTagLength	   = DEFAULT_MAXTAGLENGTH;
	config.MaxSpeedLength	   = DEFAULT_MAXSPEEDLENGTH;
	config.MaxEMailLength	   = DEFAULT_MAXEMAILLENGTH;
	config.MaxShareLength	   = DEFAULT_MAXSHARELENGTH;
	
	config.Redirect		   = strdup (DEFAULT_REDIRECT);
	config.defaultKickPeriod   = DEFAULT_KICKPERIOD;
	
	config.DefaultRights = CAP_DEFAULT;
	
	config_register ("NMDC.ListenPort",	CFG_ELEM_UINT,   &config.ListenPort, "Port on which the hub will listen.");
	config_register ("NMDC.ExtraPorts",	CFG_ELEM_STRING, &config.NMDCExtraPorts, "Extra NMDC ports to listen on.");
	config_register ("HubAddress",		CFG_ELEM_STRING, &config.Hostname, "A hostname (or IP address) where your machine will be reachable.");
	config_register ("HubName",		CFG_ELEM_STRING, &config.HubName,    "Name of the hub");
	config_register ("HubDescription",	CFG_ELEM_STRING, &config.HubDesc,    "Description of the hub.");
	config_register ("HubOwner",		CFG_ELEM_STRING, &config.HubOwner,    "Owner of the hub.");
	config_register ("HubSecurity.Nick",	CFG_ELEM_STRING, &config.HubSecurityNick, "Name of the Hub Security bot.");
	config_register ("HubSecurity.Desc",	CFG_ELEM_STRING, &config.HubSecurityDesc, "Description of the Hub Security bot.");

	config_register ("tag.MaxDescLength",   CFG_ELEM_UINT, &config.MaxDescriptionLength, "Maximum Length of the users description field.");
	config_register ("tag.MaxTagLength",    CFG_ELEM_UINT, &config.MaxTagLength,   "Maximum length of the users tag field.");
	config_register ("tag.MaxSpeedLength",  CFG_ELEM_UINT, &config.MaxSpeedLength, "Maximum length of the users speed field.");
	config_register ("tag.MaxEMailLength",  CFG_ELEM_UINT, &config.MaxEMailLength, "Maximum length of the users email address field.");
	config_register ("tag.MaxShareLength",  CFG_ELEM_UINT, &config.MaxShareLength, "Maximum length of the users sharesize field.");
	
	config_register ("Redirect",          CFG_ELEM_STRING, &config.Redirect,          "Redirection target.");
	config_register ("KickAutoBanLength", CFG_ELEM_ULONG,  &config.defaultKickPeriod, "Length of automatic temporary ban after a kick.");


	config_register ("user.defaultrights",CFG_ELEM_CAP,  &config.DefaultRights,     "These are the rights of an unregistered user.");

	return 0;
}
