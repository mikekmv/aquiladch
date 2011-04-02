/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
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

  plugin_config_load ();

  /* add lowest member of the statistices */
  gettimeofday (&boottime, NULL);

  /* initialize the random generator */
  srandom (boottime.tv_sec ^ boottime.tv_usec);

  /* setup socket handler */
  h = esocket_create_handler (3);
  h->toval.tv_sec = 60;

  /* setup server */
  server_setup (h);
  nmdc_setup (h);
  command_setup ();

  /* main loop */
  gettimeofday (&tnext, NULL);
  tnext.tv_sec += 1;
  cont = 1;
  for (; cont;) {
    /* do not assume select does not alter timeout value */
    to.tv_sec = 1;
    to.tv_usec = 0;

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
