/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#ifndef _CAP_H_
#define _CAP_H_

#define CAP_SHARE  	 0x0001
#define CAP_KICK   	 0x0002
#define CAP_BAN    	 0x0004
#define CAP_KEY    	 0x0008
#define CAP_CONFIG 	 0x0010
#define CAP_SAY    	 0x0020
#define CAP_USER   	 0x0040
#define CAP_GROUP  	 0x0080
#define CAP_INHERIT	 0x0100
#define CAP_CHAT   	 0x0200
#define CAP_PM     	 0x0400
#define CAP_DL	   	 0x0800
#define CAP_BANHARD	 0x1000
#define CAP_SEARCH	 0x2000
#define CAP_PMOP	 0x4000
#define CAP_TAG		 0x8000
#define CAP_SHAREHIDE	0x10000
#define CAP_SHAREBLOCK	0x20000
#define CAP_SPAM	0x40000
#define CAP_OWNER	0x80000000

/* shortcuts... */
#define CAP_DEFAULT (CAP_CHAT | CAP_PMOP | CAP_DL | CAP_SEARCH)
#define CAP_REG   (CAP_DEFAULT | CAP_PM)
#define CAP_VIP	  (CAP_REG  | CAP_SHARE | CAP_SPAM)
#define CAP_KVIP  (CAP_VIP  | CAP_KICK)
#define CAP_OP    (CAP_KVIP | CAP_BAN | CAP_KEY)
#define CAP_CHEEF (CAP_OP   | CAP_USER)
#define CAP_ADMIN (CAP_CHEEF | CAP_CONFIG | CAP_INHERIT | CAP_GROUP)

#define CAP_PRINT_OFFSET 8

typedef struct cap_array {
	unsigned char *name;
	unsigned long cap;
	unsigned char *help;
} cap_array_t;

extern cap_array_t Capabilities[];

#endif
