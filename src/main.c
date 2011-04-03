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
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "proto.h"
#include "config.h"
#include "user.h"
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

typedef struct {
  /* go to daemon mode */
  unsigned int detach;

  /* seperate working dir */
  unsigned int setwd;
  unsigned char *wd;

  /* pid/lock file */
  unsigned int setlock;
  unsigned char *lock;
} args_t;

args_t args = {
detach:0,

setwd:0,
wd:NULL,

setlock:0,
lock:"aquila.pid"
};

/* utility functions */

void usage (unsigned char *name)
{
  printf ("Usage: %s [-hd] [-p <pidfile>] [-c <config dir>]\n"
	  "  -h : print this help\n"
	  "  -d : detach (run as daemon)\n"
	  "  -p : specify pidfile\n" "  -c : specify config directory\n", name);
}

void parseargs (int argc, char **argv)
{
  char opt;

  while ((opt = getopt (argc, argv, "hdp:c:")) > 0) {
    switch (opt) {
      case '?':
      case 'h':
	usage (argv[0]);
	exit (0);
	break;
      case 'd':
	args.detach = 1;
	break;
      case 'p':
	if (args.setlock)
	  free (args.lock);
	args.setlock = 1;
	args.lock = strdup (optarg);
	break;
      case 'c':
	if (args.setwd)
	  free (args.wd);
	args.setwd = 1;
	args.wd = strdup (optarg);
	break;
    }
  }
}

void daemonize ()
{
  int i, lockfd;
  unsigned char pid[16];

  /* detach if asked */
  if (args.detach) {
    if (getppid () == 1)
      return;			/* already a daemon */

    /* spawn daemon */
    i = fork ();
    if (i < 0)
      exit (1);			/* fork error */
    if (i > 0)
      exit (0);			/* parent exits */

    /* child (daemon) continues */
    setsid ();			/* obtain a new process group */
    for (i = getdtablesize (); i >= 0; --i)
      close (i);		/* close all descriptors */

    /* close parent fds and send output to fds 0, 1 and 2 to bitbucket */
    i = open ("/dev/null", O_RDWR);
    dup (i);
    dup (i);			/* handle standart I/O */
  }

  /* change to working directory */
  if (args.setwd)
    chdir (args.wd);

  /* create local lock */
  lockfd = open (args.lock, O_RDWR | O_CREAT, 0640);
  if (lockfd < 0) {
    perror ("lock: open");
    exit (1);
  }

  /* lock the file */
  if (lockf (lockfd, F_TLOCK, 0) < 0) {
    perror ("lock: lockf");
    printf (HUBSOFT_NAME " is already running.\n");
    exit (0);
  }

  /* write to pid to lockfile */
  snprintf (pid, 16, "%d\n", getpid ());
  write (lockfd, pid, strlen (pid));

  /* restrict created files to 0750 */
  umask (027);
}

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

  /* block SIGPIPE */
  sigemptyset (&set);
  sigaddset (&set, SIGPIPE);
  sigprocmask (SIG_BLOCK, &set, NULL);

#ifdef DEBUG
  /* add stacktrace handler */
  StackTraceInit (argv[0], -1);
#endif

  /* parse arguments */
  parseargs (argc, argv);

  /* deamonize */
  daemonize ();

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
  plugin_config_load ();

  /* add lowest member of the statistices */
  gettimeofday (&boottime, NULL);

  /* initialize the random generator */
  srandom (boottime.tv_sec ^ boottime.tv_usec);

  /* setup socket handler */
  h = esocket_create_handler (4);
  h->toval.tv_sec = 60;
  h->toval.tv_usec = 0;

  /* setup server */
  server_setup (h);
  nmdc_setup (h);
  command_setup ();
  pi_hublist_setup (h);

#ifdef ALLOW_LUA
#ifdef HAVE_LUA_H
  /* lua must only be loaded last. */
  pi_lua_init ();
#endif
#endif

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
