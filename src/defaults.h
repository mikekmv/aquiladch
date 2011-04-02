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

#ifndef _DEFAULTS_H_
#define _DEFAULTS_H_

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#ifdef SVNREVISION
#define AQUILA_VERSION			VERSION " (" SVNREVISION ")"
#else
#define AQUILA_VERSION			VERSION
#endif

#define HUBSOFT_NAME		"Aquila"

#define CONFIG_HASHSIZE		32
#define CONFIG_HASHMASK		(CONFIG_HASHSIZE - 1)
#define CONFIG_NAMELENGTH	64

#define LOCK	 HUBSOFT_NAME
#define LOCKLENGTH 4

#define HUBNAME  "Capitol"
#define HUBDESC  "Licat volare si super tergum Aquila volat."
#define HUBOWNER "Unknown"

#define NICKLENGTH  64
#define PASSWDLENGTH 8
#define BUFSIZE 65536
#define MAX_TOKEN_SIZE 65535

#define DEFAULT_MAX_OUTGOINGBUFFERS 10
#define DEFAULT_MAX_OUTGOINGSIZE    100*1024
#define DEFAULT_OUTGOINGTHRESHOLD   1000

#define DEFAULT_PORT	      411
#define DEFAULT_IP	      0L
#define DEFAULT_ADDRESS	      "localhost"
#define NMDC_EXTRA_PORTS      ""

#define DEFAULT_HUBSECURITY_NICK     		"Aquila"
#define DEFAULT_HUBSECURITY_DESCRIPTION		"His attribute is the lightning bolt and the eagle is both his symbol and his messenger."

#define DEFAULT_SAVEFILE      "hub.conf"

#define DEFAULT_HARDBANFILE	"hardban.conf"
#define DEFAULT_SOFTBANFILE	"softban.conf"
#define DEFAULT_NICKBANFILE	"nickban.conf"
#define DEFAULT_ACCOUNTSFILE	"accounts.conf"

#define DEFAULT_REDIRECT	""

#define DEFAULT_MAXDESCRIPTIONLENGTH	11
#define DEFAULT_MAXTAGLENGTH		50
#define DEFAULT_MAXSPEEDLENGTH		10
#define DEFAULT_MAXEMAILLENGTH		50
#define DEFAULT_MAXSHARELENGTH		13	/* should be in the PetaByte range */

#define DEFAULT_KICKPERIOD		300

#define DEFAULT_AUTOSAVEINTERVAL	300

#define DEFAULT_MINPWDLENGTH		4

#define DEFAULT_MAXCHATLENGTH		512
#define DEFAULT_MAXSEARCHLENGTH		512
#define DEFAULT_MAXSRLENGTH		512
#define DEFAULT_CLONING			0

#define DEFAULT_MAXLUASCRIPTS		25

#ifdef DEBUG
#include "stacktrace.h"
#define DPRINTF	printf
//#define ASSERT assert
#define ASSERT(expr) ( (void)( (expr) ? 0 : CrashHandler(-1)) )
#else
#define DPRINTF(x...) /* x */
#define ASSERT(x...) /* x */
#endif

#endif /* _DEFAULTS_H_ */
