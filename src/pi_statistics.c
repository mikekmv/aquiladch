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

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>

#ifdef HAVE_MALLOC_H
#  include <malloc.h>
#endif

#include "aqtime.h"
#include "plugin.h"
#include "user.h"
#include "commands.h"
#include "proto.h"
#include "utils.h"
#include "nmdc_protocol.h"

#define STATS_NUM_MEASUREMENTS 360
#define STATS_NUM_LEVELS	 3

typedef struct stat_bw_elem {
  unsigned long long BytesSend, BytesReceived;
  struct timeval stamp;
} stat_bw_elem_t;

typedef struct stat_bw_level {
  stat_bw_elem_t probes[STATS_NUM_MEASUREMENTS];
  unsigned int current;

  unsigned long long TotalBytesSend, TotalBytesReceived;
  struct timeval TotalTime;
} stat_bw_level_t;

typedef struct statistics {
  /* traffic counters */
  unsigned long long TotalBytesSend, TotalBytesReceived;

  /* bw calculations */
  stat_bw_level_t bandwidth[STATS_NUM_LEVELS];
  hub_statistics_t oldcounters;
  struct timeval oldstamp;
} statistics_t;

extern struct timeval boottime;

plugin_t *plugin_stats = NULL;
statistics_t stats;

static server_t *server;

#define PROCSTAT_LEVELS 3
#define PROCSTAT_MEASUREMENTS 60

typedef struct procstat {
  struct rusage ps;
  struct timeval tv;
} procstat_t;

procstat_t procstat_boot;
procstat_t procstats[PROCSTAT_LEVELS][PROCSTAT_MEASUREMENTS + 1];
unsigned int current[PROCSTAT_LEVELS];

buffer_t *cpuinfo = NULL;
unsigned int cpucount = 0;

/************************* CPU FUNCTIONS ******************************************/

void cpu_parse ()
{
  FILE *fp;
  buffer_t *b;
  unsigned char buf[1024];

  ASSERT (!cpuinfo);
  ASSERT (!cpucount);

  b = bf_alloc (BUFSIZE);

  fp = fopen ("/proc/cpuinfo", "r");
  if (!fp) {
    bf_printf (b, _("CPU Unknown\n"));
    goto leave;
  }

  /* This is crap. The kernel really needs some aligning here.
   * Supported:
   *    x86, ARM, MIPS, Broadcom BCM947XX, Alpha
   * Not Support:
   *    eMac, PowerMac 740/750, PA-RISC
   * Some supported achitectures may miss clock info.
   */
  while (!feof (fp)) {
    fgets (buf, 1024, fp);
    if (!strncasecmp (buf, "model name", 10) || !strncmp (buf, "Processor", 9)
	|| !strncmp (buf, "cpu model", 9)) {
      bf_printf (b, _("CPU%.1d: %.*s\n"), cpucount++, strlen (buf) - 14, buf + 13);
    } else if (!strncasecmp (buf, "cpu MHz", 7)) {
      bf_printf (b, _("     Clock: %.*s MHz "), strlen (buf) - 12, buf + 11);
    } else if (!strncasecmp (buf, "bogomips", 8)) {
      bf_printf (b, _("BogoMIPS: %.*s\n"), strlen (buf) - 12, buf + 11);
    }
  }
  fclose (fp);


leave:
  /* fallback */
  if (!cpucount)
    cpucount = 1;

  cpuinfo = bf_copy (b, 0);
  bf_free (b);
}

void cpu_init ()
{
  memset (&procstats, 0, sizeof (procstats));
  memset (&current, 0, sizeof (current));
  memset (&procstat_boot, 0, sizeof (procstat_t));
  getrusage (RUSAGE_SELF, &(procstats[0][0].ps));
  gettimeofday (&(procstats[0][0].tv), NULL);
  procstat_boot = procstats[0][0];
  procstats[2][0] = procstats[1][0] = procstats[0][0];

  cpu_parse ();
}

unsigned int cpu_measure ()
{
  int i;

  current[0]++;
  getrusage (RUSAGE_SELF, &(procstats[0][current[0]].ps));
  gettimeofday (&(procstats[0][current[0]].tv), NULL);
  for (i = 0; (current[i] == (PROCSTAT_MEASUREMENTS)) && (i < PROCSTAT_LEVELS); i++) {
    if (i < (PROCSTAT_LEVELS - 1)) {
      current[i + 1]++;
      procstats[i + 1][current[i + 1]] = procstats[i][PROCSTAT_MEASUREMENTS];
    }
    current[i] = 0;
    procstats[i][0] = procstats[i][PROCSTAT_MEASUREMENTS];
  }

  return 0;
}

#define TV_TO_MSEC(tv) ((tv.tv_sec*1000)+(tv.tv_usec/1000))

float cpu_calc (procstat_t * old, procstat_t * new)
{
  long used, real;

  /* we aren't up that long yet */
  if (!old->tv.tv_sec)
    return 0;

  used = TV_TO_MSEC (new->ps.ru_utime);
  used += TV_TO_MSEC (new->ps.ru_stime);
  used -= TV_TO_MSEC (old->ps.ru_utime);
  used -= TV_TO_MSEC (old->ps.ru_stime);

  real = TV_TO_MSEC (new->tv);
  real -= TV_TO_MSEC (old->tv);

  return ((100.0 * (float) used) / (float) real) / (float) cpucount;
}

float cpu_calculate (int seconds)
{
  int level = seconds % PROCSTAT_MEASUREMENTS;

  if (procstats[level][(current[level] - 1) % PROCSTAT_MEASUREMENTS].tv.tv_sec > 0) {
    return cpu_calc (&procstats[level][(current[level] - 1) % PROCSTAT_MEASUREMENTS],
		     &procstats[0][current[0]]);
  } else {
    return 0.0;
  }
}

int cpu_printf (buffer_t * buf)
{

  time_t tnow = now.tv_sec;

  bf_printf (buf, _("\nCpu Statistics at %s"), ctime (&tnow));

  if (procstats[2][(current[2] - 1) % PROCSTAT_MEASUREMENTS].tv.tv_sec > 0)
    bf_printf (buf, _(" last hour %2.2f%%\n"),
	       cpu_calc (&procstats[2][(current[2] - 1) % PROCSTAT_MEASUREMENTS],
			 &procstats[0][current[0]]));
  if (procstats[1][(current[1] - 1) % PROCSTAT_MEASUREMENTS].tv.tv_sec > 0)
    bf_printf (buf, _(" last minute %2.2f%%\n"),
	       cpu_calc (&procstats[1][(current[1] - 1) % PROCSTAT_MEASUREMENTS],
			 &procstats[0][current[0]]));
  if (procstats[0][(current[0] - 5) % PROCSTAT_MEASUREMENTS].tv.tv_sec > 0)
    bf_printf (buf, _(" last 5 seconds %2.2f%%\n\n"),
	       cpu_calc (&procstats[0][(current[0] - 5) % PROCSTAT_MEASUREMENTS],
			 &procstats[0][current[0]]));

  bf_printf (buf, _("Since boot %2.2f%%\n"), cpu_calc (&procstat_boot, &procstats[0][current[0]]));

  if (cpuinfo)
    bf_printf (buf, "\n%.*s", bf_used (cpuinfo), cpuinfo->s);

  return 0;
}

/************************* BW FUNCTIONS ******************************************/

/* assumes total stats struct has been zeroed */
unsigned int bandwidth_init ()
{
  struct timeval now;
  unsigned int i;

  gettimeofday (&now, NULL);
  for (i = 0; i < STATS_NUM_LEVELS; i++) {
    stats.bandwidth[i].probes[0].stamp = now;
    stats.bandwidth[i].current++;
  }
  stats.oldstamp = now;

  return 0;
}

/* assumes counters will not get incremented with more than a full countermax */
unsigned int bandwidth_measure ()
{
  struct timeval now, diff;
  unsigned int i = 0;
  stat_bw_level_t *lvl, *src;
  unsigned long in, out;

  gettimeofday (&now, NULL);

  /* preparing values */
  timersub (&now, &stats.oldstamp, &diff);
  if (server->hubstats.TotalBytesSend >= stats.oldcounters.TotalBytesSend) {
    out = server->hubstats.TotalBytesSend - stats.oldcounters.TotalBytesSend;
  } else {
    out = ((ULONG_MAX - stats.oldcounters.TotalBytesSend) + 1 + server->hubstats.TotalBytesSend);
  }
  if (server->hubstats.TotalBytesReceived >= stats.oldcounters.TotalBytesReceived) {
    in = server->hubstats.TotalBytesReceived - stats.oldcounters.TotalBytesReceived;
  } else {
    in =
      ((ULONG_MAX - stats.oldcounters.TotalBytesReceived) + 1 +
       server->hubstats.TotalBytesReceived);
  }

  /* creating new probe */
  lvl = &stats.bandwidth[0];
  lvl->probes[lvl->current].BytesSend = out;
  lvl->probes[lvl->current].BytesReceived = in;
  lvl->probes[lvl->current].stamp = diff;

  /* updating totals */
  lvl->TotalBytesSend += out;
  lvl->TotalBytesReceived += in;
  timeradd (&lvl->TotalTime, &diff, &lvl->TotalTime);

  /* storing old data */
  stats.oldcounters = server->hubstats;
  stats.oldstamp = now;

  /* handle all the other levels. */
  if (++(lvl->current) == STATS_NUM_MEASUREMENTS) {
    lvl->current = 0;
    for (i = 0; i < (STATS_NUM_LEVELS - 1); ++i) {
      src = &stats.bandwidth[i];
      lvl = &stats.bandwidth[i + 1];
      /* creating new probe */
      lvl->probes[lvl->current].BytesSend = src->TotalBytesSend;
      lvl->probes[lvl->current].BytesReceived = src->TotalBytesReceived;
      lvl->probes[lvl->current].stamp = src->TotalTime;
      lvl->TotalBytesSend += src->TotalBytesSend;
      lvl->TotalBytesReceived += src->TotalBytesReceived;
      timeradd (&lvl->TotalTime, &src->TotalTime, &lvl->TotalTime);

      /* resetting helper counters */
      src->TotalBytesSend = 0;
      src->TotalBytesReceived = 0;
      timerclear (&src->TotalTime);

      if (++(lvl->current) != STATS_NUM_MEASUREMENTS)
	break;

      lvl->current = 0;
    }
  }

  return 0;
}

unsigned int bandwidth_printf (buffer_t * buf)
{
  unsigned int i, retval = 0;
  double in, out;
  stat_bw_level_t *lvl;
  stat_bw_elem_t *elem;

  bf_printf (buf, _("Total traffic since boot: In %lu, Out %lu\n"),
	     server->hubstats.TotalBytesReceived, server->hubstats.TotalBytesSend);

  lvl = &stats.bandwidth[0];
  elem = &lvl->probes[lvl->current ? lvl->current - 1 : (STATS_NUM_MEASUREMENTS - 1)];

  out = (8 * (double) elem->BytesSend) / ((double) TV_TO_MSEC (elem->stamp));
  in = (8 * (double) elem->BytesReceived) / ((double) TV_TO_MSEC (elem->stamp));

  retval += bf_printf (buf, _("In last %lu ms: In %f kbps, Out %f kbps\n"),
		       TV_TO_MSEC (elem->stamp), in, out);

  for (i = 0; i < STATS_NUM_LEVELS; i++) {
    lvl = &stats.bandwidth[i];

    if (timerisset (&lvl->TotalTime)) {
      out = (8000 * (double) lvl->TotalBytesSend) / ((double) TV_TO_MSEC (lvl->TotalTime));
      in = (8000 * (double) lvl->TotalBytesReceived) / ((double) TV_TO_MSEC (lvl->TotalTime));

      retval +=
	bf_printf (buf, _("Average over %d seconds: In %f kbps, Out %f kbps\n"),
		   lvl->TotalTime.tv_sec, in / 1000, out / 1000);
    };
  };
  return retval;
}


/****************************************************************************************/
unsigned long pi_statistics_event_cacheflush (plugin_user_t * user, void *dummy,
					      unsigned long event, buffer_t * token)
{
  cpu_measure ();
  return bandwidth_measure ();
}


#define bfz_used(buf) (buf ? bf_used(buf) : 0)

#define add_elem(buf, name, now) { bf_printf (buf, #name ": count: %lu size: %lu, next: %ld\n", name.messages.count, name.length, name.timertype.period + name.timer.timestamp - now); totalmem += name.length;totallines += name.messages.count; }
unsigned long pi_statistics_handler_statcache (plugin_user_t * user, buffer_t * output, void *dummy,
					       unsigned int argc, unsigned char **argv)
{
/* FIXME
  unsigned long totalmem = 0;
  unsigned long totallines = 0;


  add_elem (output, cache.chat, now.tv_sec);
  add_elem (output, cache.myinfo, now.tv_sec);
  add_elem (output, cache.myinfoupdate, now.tv_sec);
  add_elem (output, cache.myinfoupdateop, now.tv_sec);
  add_elem (output, cache.asearch, now.tv_sec);
  add_elem (output, cache.psearch, now.tv_sec);
  add_elem (output, cache.aresearch, now.tv_sec);
  add_elem (output, cache.presearch, now.tv_sec);
  add_elem (output, cache.results, now.tv_sec);
  add_elem (output, cache.privatemessages, now.tv_sec);

  bf_printf (output, _("Total Count: %lu, Total Memory: %lu\n"), totallines, totalmem);

  bf_printf (output, _("ZPipe users: %lu, ZLine users: %lu\n\n"), cache.ZpipeSupporters,
	     cache.ZlineSupporters);
  bf_printf (output, _("Nicklist cache (last updated %lu seconds ago).\n"),
	     now.tv_sec - cache.lastrebuild);
#ifdef ZLINES
  bf_printf (output, _("  Nicklist length %lu (zpipe %lu, zline %lu)\n"), bf_used (cache.nicklist),
	     bfz_used (cache.nicklistzpipe), bfz_used (cache.nicklistzline));
  bf_printf (output, _("  Infolist length %lu (zpipe %lu, zline %lu)\n"), bf_used (cache.infolist),
	     bfz_used (cache.infolistzpipe), bfz_used (cache.infolistzline));
#else
  bf_printf (output, _("  Nicklist length %lu\n"), bf_used (cache.nicklist));
  bf_printf (output, _("  Infolist length %lu\n"), bf_used (cache.infolist));
#endif

*/
  return 0;
}

unsigned long pi_statistics_handler_statbw (plugin_user_t * user, buffer_t * output, void *dummy,
					    unsigned int argc, unsigned char **argv)
{
  bandwidth_printf (output);
#ifdef DEBUG
  bf_printf (output, _("Warning: DEBUG build, cpu measurements highly inaccurate.\n"));
#endif
  return 0;
}

#include "nmdc_local.h"
unsigned long pi_statistics_handler_statbuffer (plugin_user_t * user, buffer_t * output,
						void *dummy, unsigned int argc,
						unsigned char **argv)
{
  client_t *cl;
  nmdc_user_t *u;
  unsigned long count;
  unsigned long long bufs, total, rest;

  bf_printf (output, _("Allocated size: %llu (max: %llu)\n)"), bufferstats.size, bufferstats.peak);
  bf_printf (output, _("Allocated buffers: %lu (max %lu)\n)"), bufferstats.count, bufferstats.max);
  bf_printf (output, _(" Users having buffered output: %lu, buffering %lu\n)"), server->buffering,
	     server->buf_mem);

  count = 0;
  bufs = 0;
  total = 0;
  rest = 0;
  bf_printf (output, _("\nBuffering clients:\n"));
  for (u = userlist; u; u = (nmdc_user_t *) u->user.next) {
    cl = (client_t *) u->user.parent;

    if (!cl) {
      if (u->state == PROTO_STATE_VIRTUAL)
	continue;
      bf_printf (output, _("Real user %s without socket?\n"), u->user.nick);
      continue;
    }
    if (!cl->outgoing.count)
      continue;

    if (bf_unused (output) < 256) {
      buffer_t *b = bf_alloc (10000);

      bf_append (&output, b);
      output = b;
    }

    bf_printf (output, _(" %s (online: %s), %lu buffers, total %lu, offset %lu, credit %lu\n"),
	       u->user.nick, time_print (now.tv_sec - u->user.joinstamp), cl->outgoing.count,
	       cl->outgoing.size, cl->offset, cl->credit);
    bufs += cl->outgoing.count;
    total += cl->outgoing.size;
    rest += (cl->outgoing.size - cl->offset);

    count++;
  }
  /* free up some room for the totals */
  if (!count)
    bf_printf (output, _(" None.\n"));

  bf_printf (output,
	     _
	     ("\n Total found: %lu\n Total bytes queued %lu\n Average bufs %lu\n Average bytes queued %lu\n Average bytes to write %lu\n"),
	     count, total, count ? bufs / count : 0, count ? total / count : 0,
	     count ? rest / count : 0);

  return 0;
}

unsigned long pi_statistics_handler_statcpu (plugin_user_t * user, buffer_t * output, void *dummy,
					     unsigned int argc, unsigned char **argv)
{
  cpu_printf (output);
#ifdef DEBUG
  bf_printf (output, _("Warning: DEBUG build, cpu measurements higher as normal.\n"));
#endif
  return 0;
}

#include "iplist.h"
unsigned long pi_statistics_handler_statconn (plugin_user_t * user, buffer_t * output, void *dummy,
					      unsigned int argc, unsigned char **argv)
{
  bf_printf (output, _("Total IPs remembered (roughly last %us): %lu\n"), iplist_interval,
	     server->lastlist.count);
  return 0;
}

/* FIXME read this from proc.*/
extern unsigned long cachelist_count;

unsigned long pi_statistics_handler_statmem (plugin_user_t * user, buffer_t * output, void *dummy,
					     unsigned int argc, unsigned char **argv)
{

  bf_printf (output, _("Memory Usage:\n"));

#if defined(HAVE_MALLOC_H) && defined(HAVE_MALLINFO)
  if (1) {
    struct mallinfo mi;

    mi = mallinfo ();

    bf_printf (output, _("GNU LibC memory statistics:\n"));
    bf_printf (output, _(" Total heap     : %lu (%s)\n"), mi.arena, format_size (mi.arena));
    bf_printf (output, _(" # Free chunks    : %lu\n"), mi.ordblks);
    bf_printf (output, _(" # Fastbin blocks : %lu\n"), mi.smblks);
    bf_printf (output, _(" Total alloced space : %lu (%s)\n"), mi.uordblks,
	       format_size (mi.uordblks));
    bf_printf (output, _(" Total free space    : %lu (%s)\n"), mi.fordblks,
	       format_size (mi.fordblks));
    bf_printf (output, _(" keepcost : %lu (%s)\n"), mi.keepcost, format_size (mi.keepcost));
    bf_printf (output, _(" # MMAP regions : %lu\n"), mi.hblks);
    bf_printf (output, _(" MMAP space     : %lu (%s)\n\n"), mi.hblkhd, format_size (mi.hblkhd));
  }
#endif

  bf_printf (output, _("%s stats:\n"), HUBSOFT_NAME);
  bf_printf (output, _(" Buffering memory: %lu\n"), server->buf_mem);
  bf_printf (output, _(" Cachelist size: %lu\n"), cachelist_count);

  return 0;
}

unsigned long pi_statistics_handler_uptime (plugin_user_t * user, buffer_t * output, void *dummy,
					    unsigned int argc, unsigned char **argv)
{
  struct timeval diff;

  timersub (&now, &boottime, &diff);

  bf_printf (output, _("Booted at %.*s, up %lu seconds (%s)\n"), 24, ctime (&boottime.tv_sec),
	     diff.tv_sec, time_print (diff.tv_sec));

  return 0;
}

unsigned long pi_statistics_handler_statnmdc (plugin_user_t * user, buffer_t * output, void *dummy,
					      unsigned int argc, unsigned char **argv)
{
  bf_printf (output, " cacherebuild : %lu\n", nmdc_stats.cacherebuild);
  bf_printf (output, " userjoin : %lu\n", nmdc_stats.userjoin);
  bf_printf (output, " userpart : %lu\n", nmdc_stats.userpart);
  bf_printf (output, " userviolate : %lu\n", nmdc_stats.userviolate);
  bf_printf (output, " banned : %lu\n", nmdc_stats.banned);
  bf_printf (output, " forcemove : %lu\n", nmdc_stats.forcemove);
  bf_printf (output, " disconnect : %lu\n", nmdc_stats.disconnect);
  bf_printf (output, " redirect : %lu\n", nmdc_stats.redirect);
  bf_printf (output, " tokens : %lu\n", nmdc_stats.tokens);
  bf_printf (output, " brokenkey : %lu\n", nmdc_stats.brokenkey);
  bf_printf (output, " usednick : %lu\n", nmdc_stats.usednick);
  bf_printf (output, " softban : %lu\n", nmdc_stats.softban);
  bf_printf (output, " nickban : %lu\n", nmdc_stats.nickban);
  bf_printf (output, " badpasswd : %lu\n", nmdc_stats.badpasswd);
  bf_printf (output, " notags : %lu\n", nmdc_stats.notags);
  bf_printf (output, " badmyinfo : %lu\n", nmdc_stats.badmyinfo);
  bf_printf (output, " preloginevent : %lu\n", nmdc_stats.preloginevent);
  bf_printf (output, " loginevent : %lu\n", nmdc_stats.loginevent);
  bf_printf (output, " logincached : %lu\n", nmdc_stats.logincached);
  bf_printf (output, " chatoverflow : %lu\n", nmdc_stats.chatoverflow);
  bf_printf (output, " chatfakenick : %lu\n", nmdc_stats.chatfakenick);
  bf_printf (output, " chattoolong : %lu\n", nmdc_stats.chattoolong);
  bf_printf (output, " chatevent : %lu\n", nmdc_stats.chatevent);
  bf_printf (output, " myinfooverflow : %lu\n", nmdc_stats.myinfooverflow);
  bf_printf (output, " myinfoevent : %lu\n", nmdc_stats.myinfoevent);
  bf_printf (output, " searchoverflow : %lu\n", nmdc_stats.searchoverflow);
  bf_printf (output, " searchcorrupt : %lu\n", nmdc_stats.searchcorrupt);
  bf_printf (output, " searchtoolong : %lu\n", nmdc_stats.searchtoolong);
  bf_printf (output, " searchevent : %lu\n", nmdc_stats.searchevent);
  bf_printf (output, " researchdrop : %lu\n", nmdc_stats.researchdrop);
  bf_printf (output, " researchmatch : %lu\n", nmdc_stats.researchmatch);
  bf_printf (output, " searchtth : %lu\n", nmdc_stats.searchtth);
  bf_printf (output, " searchnormal : %lu\n", nmdc_stats.searchnormal);
  bf_printf (output, " srtoolong : %lu\n", nmdc_stats.srtoolong);
  bf_printf (output, " sroverflow : %lu\n", nmdc_stats.sroverflow);
  bf_printf (output, " srevent : %lu\n", nmdc_stats.srevent);
  bf_printf (output, " srrobot : %lu\n", nmdc_stats.srrobot);
  bf_printf (output, " srfakesource : %lu\n", nmdc_stats.srfakesource);
  bf_printf (output, " ctmoverflow : %lu\n", nmdc_stats.ctmoverflow);
  bf_printf (output, " ctmbadtarget : %lu\n", nmdc_stats.ctmbadtarget);
  bf_printf (output, " rctmoverflow : %lu\n", nmdc_stats.rctmoverflow);
  bf_printf (output, " rctmbadtarget : %lu\n", nmdc_stats.rctmbadtarget);
  bf_printf (output, " rctmbadsource : %lu\n", nmdc_stats.rctmbadsource);
  bf_printf (output, " pmoverflow : %lu\n", nmdc_stats.pmoverflow);
  bf_printf (output, " pmoutevent : %lu\n", nmdc_stats.pmoutevent);
  bf_printf (output, " pmbadtarget : %lu\n", nmdc_stats.pmbadtarget);
  bf_printf (output, " pmbadsource : %lu\n", nmdc_stats.pmbadsource);
  bf_printf (output, " pminevent : %lu\n", nmdc_stats.pminevent);
  bf_printf (output, " botinfo : %lu\n", nmdc_stats.botinfo);
  bf_printf (output, " cache_myinfo : %lu\n", nmdc_stats.cache_myinfo);
  bf_printf (output, " cache_myinfoupdate : %lu\n", nmdc_stats.cache_myinfoupdate);
  bf_printf (output, " cache_chat : %lu\n", nmdc_stats.cache_chat);
  bf_printf (output, " cache_asearch : %lu\n", nmdc_stats.cache_asearch);
  bf_printf (output, " cache_psearch : %lu\n", nmdc_stats.cache_psearch);
  bf_printf (output, " cache_messages : %lu\n", nmdc_stats.cache_messages);
  bf_printf (output, " cache_results : %lu\n", nmdc_stats.cache_results);

  return 0;
}

int pi_statistics_init (plugin_manager_t * pm, server_t * s)
{

  server = s;

  memset (&procstats, 0, sizeof (procstats));
  memset (&current, 0, sizeof (current));
  memset (&stats, 0, sizeof (statistics_t));
  bandwidth_init ();

  cpu_init ();

  plugin_stats = plugin_register (pm, "stats");
  plugin_request (plugin_stats, PLUGIN_EVENT_CACHEFLUSH, &pi_statistics_event_cacheflush);

  command_register ("statbuffer", &pi_statistics_handler_statbuffer, CAP_CONFIG,
		    _("Show buffer stats."));
  command_register ("statcache", &pi_statistics_handler_statcache, 0, _("Show cache stats."));
  command_register ("statbw", &pi_statistics_handler_statbw, 0, _("Show cache stats."));
  command_register ("statcpu", &pi_statistics_handler_statcpu, 0, _("Show cpu usage stats."));
  command_register ("statnmdc", &pi_statistics_handler_statnmdc, 0,
		    _("Show nmdc protocol stats. Experts only."));
  command_register ("statmem", &pi_statistics_handler_statmem, 0, _("Show memory usage stats."));
  command_register ("statconn", &pi_statistics_handler_statconn, 0, _("Show connection stats."));
  command_register ("uptime", &pi_statistics_handler_uptime, 0, _("Show uptime."));

  return 0;
}
