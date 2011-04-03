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

#include "esocket.h"

#include <arpa/inet.h>

#include "config.h"
#include "buffer.h"
#include "proto.h"
#include "stringlist.h"
#include "banlist.h"
#include "iplist.h"

/* local timeouts */

#define HUB_TIMEOUT_INIT	 5000
#define HUB_TIMEOUT_ONLINE	30000

#define HUB_STATE_NORMAL	1
#define HUB_STATE_BUFFERING	2
#define HUB_STATE_OVERFLOW	3

typedef struct hub_statitics {
  unsigned long TotalBytesSend;
  unsigned long TotalBytesReceived;
} hub_statistics_t;

/*
 *  server private context.
 */

typedef struct server {
  unsigned long buffering;
  unsigned long buf_mem;

  esocket_handler_t *h;

  /* banlist */
  banlist_t hardbanlist;
  iplist_t  lastlist;

  unsigned int es_type_server, es_type_listen;

  hub_statistics_t hubstats;
} server_t;

typedef struct listener {
  proto_t  *proto;
  server_t *server;
} listener_t;

typedef struct client {
  proto_t *proto;
  esocket_t *es;
  buffer_t *buffers;		/* contains read but unparsed buffers */
  string_list_t outgoing;
  unsigned long offset, credit;
  unsigned int state;

  server_t *server;
  
  void *user;
} client_t;

extern server_t *server_init (unsigned int hint);
extern int server_setup (server_t *);
extern int server_disconnect_user (client_t *, char *);
extern int server_write (client_t *, buffer_t *);
extern int server_write_credit (client_t *, buffer_t *);
extern int server_settimeout (client_t * cl, unsigned long timeout);
extern int server_add_port (server_t *, proto_t * proto,  unsigned long address, int port);

#endif /* _HUB_H_ */
