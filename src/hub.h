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

#ifndef _HUB_H_
#define _HUB_H_

#include <arpa/inet.h>

#include "config.h"
#include "buffer.h"
#include "esocket.h"
#include "proto.h"
#include "stringlist.h"
#include "banlist.h"
#include "banlistnick.h"

/* local timeouts */

typedef struct hub_statitics {
  unsigned long TotalBytesSend;
  unsigned long TotalBytesReceived;
} hub_statistics_t;

extern hub_statistics_t hubstats;

/*
 *  server private context.
 */

typedef struct client {
  proto_t *proto;
  esocket_t *es;
  buffer_t *buffers;		/* contains read but unparsed buffers */
  string_list_t outgoing;
  unsigned long offset;

  user_t *user;
} client_t;

extern unsigned long users;

/*  banlists */
extern banlist_t hardbanlist, softbanlist;
extern banlist_nick_t nickbanlist;

extern int server_init ();
extern int server_setup (esocket_handler_t *);
extern int server_disconnect_user (client_t *);
extern int server_write (client_t *, buffer_t *);
extern int server_settimeout (client_t * cl, unsigned long timeout);
extern int server_add_port (esocket_handler_t * h, proto_t * proto,  unsigned long address, int port);

#endif /* _HUB_H_ */
