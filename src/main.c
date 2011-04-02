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


#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>

#include "proto.h"
#include "config.h"
#include "user.h"
#include "hub.h"
#include "plugin_int.h"
#include "builtincmd.h"
#include "commands.h"

#include "nmdc.h"

/* FIXME: for nmdc_proto.cache_flush */
#include "nmdc_protocol.h"

#ifdef DEBUG
#include "stacktrace.h"
#endif

/* global data */

struct timeval boottime;

/* utility functions */

/*
 * MAIN LOOP
 */
int main (int argc, char **argv)
{
  int ret, cont;
  struct timeval to, tnow, tnext;
  esocket_handler_t *h;
  sigset_t set;

  /* unbuffer the output */
  setvbuf (stdout, (char *) NULL, _IOLBF, 0);

  sigemptyset (&set);
  sigaddset (&set, SIGPIPE);
  sigprocmask (SIG_BLOCK, &set, NULL);

#ifdef DEBUG
  /* add stacktrace handler */
  {
    struct sigaction sact;

    StackTraceInit (argv[0], -1);

    sigemptyset (&sact.sa_mask);
    sact.sa_flags = 0;
    sact.sa_handler = CrashHandler;
    sigaction (SIGSEGV, &sact, NULL);
    sigaction (SIGBUS, &sact, NULL);
  }
#endif

  /* initialize the global configuration */
  config_init ();
  accounts_init ();
  plugin_init ();
  command_init ();
  builtincmd_init ();
  server_init ();
  nmdc_init ();

  pi_iplog_init ();
  pi_user_init ();
  pi_chatroom_init ();
  pi_statistics_init ();
  pi_trigger_init ();
  pi_chatlog_init ();
  pi_statbot_init ();
  pi_hublist_init ();
#ifdef ALLOW_LUA
#ifdef HAVE_LUA_H
  pi_lua_init ();
#endif
#endif
  plugin_config_load ();

  /* add lowest member of the statistices */
  gettimeofday (&boottime, NULL);

  /* initialize the random generator */
  srandom (boottime.tv_sec ^ boottime.tv_usec);

  /* setup socket handler */
  h = esocket_create_handler (4);
  h->toval.tv_sec = 60;

  /* setup server */
  server_setup (h);
  nmdc_setup (h);
  command_setup ();
  pi_hublist_setup (h);

  /* main loop */
  gettimeofday (&tnext, NULL);
  tnext.tv_sec += 1;
  cont = 1;
  for (; cont;) {
    /* do not assume select does not alter timeout value */
    to.tv_sec = 0;
    to.tv_usec = 100000;

    /* wait until an event */
    ret = esocket_select (h, &to);

    /* 1s periodic cache flush */
    gettimeofday (&tnow, NULL);
    if (timercmp (&tnow, &tnext, >=)) {
      tnext = tnow;
      tnext.tv_sec += 1;
      nmdc_proto.flush_cache ();
    }
  }
  /* should never be reached... */
  printf ("Shutdown\n");

  return 0;
}
