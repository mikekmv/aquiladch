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

#include "utils.h"

#include "nmdc_protocol.h"
#include "nmdc_local.h"
#include "nmdc_utils.h"

/******************************************************************************\
**                                                                            **
**                            NICKLIST CACHING                                **
**                                                                            **
\******************************************************************************/

int nicklistcache_adduser (user_t * u)
{
  unsigned long l;

  cache.length_estimate += strlen (u->nick) + 2;
  cache.length_estimate_info += bf_used (u->MyINFO) + 1;	/* add one for the | */
  u->flags |= NMDC_FLAG_CACHED;

  cache.usercount++;

  if (u->op) {
    cache.length_estimate_op += strlen (u->nick) + 2;
    cache.needrebuild = 1;
  }

  if (cache.needrebuild)
    return 0;

  /* append the myinfo message */
  l = bf_used (u->MyINFO);
  if (l >= bf_unused (cache.infolistupdate)) {
    cache.needrebuild = 1;
    return 0;
  }

  bf_strncat (cache.infolistupdate, u->MyINFO->s, l);
  bf_strcat (cache.infolistupdate, "|");
  bf_printf (cache.hellolist, "$Hello %s|", u->nick);

  BF_VERIFY (cache.infolistupdate);
  BF_VERIFY (cache.hellolist);

  return 0;
}

int nicklistcache_updateuser (user_t * u, buffer_t * new)
{
  unsigned long l;

  cache.length_estimate_info -= bf_used (u->MyINFO);
  cache.length_estimate_info += bf_used (new);

  if (cache.needrebuild)
    return 0;

  l = bf_used (new);
  if (l > bf_unused (cache.infolistupdate)) {
    cache.needrebuild = 1;
    return 0;
  }

  bf_strncat (cache.infolistupdate, new->s, l);
  bf_strcat (cache.infolistupdate, "|");

  BF_VERIFY (cache.infolistupdate);

  return 0;
}

int nicklistcache_deluser (user_t * u)
{
  if (!(u->flags & NMDC_FLAG_CACHED))
    return 0;

  cache.length_estimate -= (strlen (u->nick) + 2);
  cache.length_estimate_info -= (bf_used (u->MyINFO) + 1);

  cache.usercount--;

  if (u->op) {
    cache.needrebuild = 1;
    cache.length_estimate_op -= (strlen (u->nick) + 2);
  }

  if (cache.needrebuild)
    return 0;

  if (bf_unused (cache.infolistupdate) < (strlen (u->nick) + 8)) {
    cache.needrebuild = 1;
    return 0;
  }

  bf_printf (cache.infolistupdate, "$Quit %s|", u->nick);
  bf_printf (cache.hellolist, "$Quit %s|", u->nick);

  BF_VERIFY (cache.infolistupdate);
  BF_VERIFY (cache.hellolist);

  return 0;
}

int nicklistcache_rebuild (struct timeval now)
{
  unsigned char *s, *o;
  user_t *t;

  nmdc_stats.cacherebuild++;

  DPRINTF (" Rebuilding cache\n");
#ifdef ZLINES
  if (cache.infolistzpipe != cache.infolist)
    bf_free (cache.infolistzpipe);
  cache.infolistzpipe = NULL;
  if (cache.infolistzline != cache.infolist)
    bf_free (cache.infolistzline);
  cache.infolistzline = NULL;

  if (cache.nicklistzpipe != cache.nicklist)
    bf_free (cache.nicklistzpipe);
  cache.nicklistzpipe = NULL;
  if (cache.nicklistzline != cache.nicklist)
    bf_free (cache.nicklistzline);
  cache.nicklistzline = NULL;
#endif
  bf_free (cache.nicklist);
  bf_free (cache.oplist);
  bf_free (cache.infolist);
  bf_free (cache.infolistupdate);
  bf_free (cache.hellolist);

  cache.nicklist = bf_alloc (cache.length_estimate + 32);
  s = cache.nicklist->buffer;
  cache.oplist = bf_alloc (cache.length_estimate_op + 32);
  o = cache.oplist->buffer;
  cache.infolist = bf_alloc (cache.length_estimate_info + 32);
  cache.infolistupdate = bf_alloc ((cache.length_estimate_info >> NICKLISTCACHE_SPARE) + 32);
  cache.hellolist = bf_alloc (cache.length_estimate_info + 32);

  s += sprintf (s, "$NickList ");
  o += sprintf (o, "$OpList ");
  for (t = userlist; t; t = t->next) {
    if ((t->state != PROTO_STATE_ONLINE) && (t->state != PROTO_STATE_VIRTUAL))
      continue;
    bf_strncat (cache.infolist, t->MyINFO->s, bf_used (t->MyINFO));
    bf_strcat (cache.infolist, "|");
    s += sprintf (s, "%s$$", t->nick);
    if (t->op)
      o += sprintf (o, "%s$$", t->nick);
  }
  strcat (s++, "|");
  cache.nicklist->e = s;
  strcat (o++, "|");
  cache.oplist->e = o;

  cache.needrebuild = 0;
  cache.lastrebuild = now.tv_sec;

#ifdef ZLINES
  zline (cache.infolist, cache.ZpipeSupporters ? &cache.infolistzpipe : NULL,
	 cache.ZlineSupporters ? &cache.infolistzline : NULL);
  zline (cache.nicklist, cache.ZpipeSupporters ? &cache.nicklistzpipe : NULL,
	 cache.ZlineSupporters ? &cache.nicklistzline : NULL);
#endif

  BF_VERIFY (cache.infolist);
  BF_VERIFY (cache.nicklist);
  BF_VERIFY (cache.oplist);
#ifdef ZLINES
  BF_VERIFY (cache.infolistzline);
  BF_VERIFY (cache.infolistzpipe);
  BF_VERIFY (cache.nicklistzline);
  BF_VERIFY (cache.nicklistzpipe);
#endif

  return 0;
}

int nicklistcache_sendnicklist (user_t * target)
{
  struct timeval now;

  gettimeofday (&now, NULL);
  if ((now.tv_sec - cache.lastrebuild) > PROTOCOL_REBUILD_PERIOD)
    cache.needrebuild = 1;

  if (cache.needrebuild)
    nicklistcache_rebuild (now);

  /* do not send out a nicklist to nohello clients: they have enough with the infolist 
   * unless they do not support NoGetINFO. Very nice. NOT.
   */
  if (!(target->supports & NMDC_SUPPORTS_NoHello)) {
#ifdef ZLINES
    if (target->supports & NMDC_SUPPORTS_ZPipe) {
      server_write_credit (target->parent, cache.nicklistzpipe);
    } else if (target->supports & NMDC_SUPPORTS_ZLine) {
      server_write_credit (target->parent, cache.nicklistzline);
    } else {
      server_write_credit (target->parent, cache.nicklist);
    }
#else
    server_write_credit (target->parent, cache.nicklist);
#endif
  } else {
    if (!(target->supports & NMDC_SUPPORTS_NoGetINFO)) {
      server_write_credit (target->parent, cache.nicklist);
    }
  }
  /* always send the oplist */
  server_write_credit (target->parent, cache.oplist);

  /* clients that support NoGetINFO get a infolist and a infolistupdate, other get a  hello list update */
  if (target->supports & NMDC_SUPPORTS_NoGetINFO) {
#ifdef ZLINES
    if (target->supports & NMDC_SUPPORTS_ZPipe) {
      server_write_credit (target->parent, cache.infolistzpipe);
    } else if (target->supports & NMDC_SUPPORTS_ZLine) {
      server_write_credit (target->parent, cache.infolistzline);
    } else {
      server_write_credit (target->parent, cache.infolist);
    }
#else
    server_write_credit (target->parent, cache.infolist);
#endif
    server_write_credit (target->parent, cache.infolistupdate);
  } else {
    server_write_credit (target->parent, cache.hellolist);
  }

  return 0;
}

int nicklistcache_sendoplist (user_t * target)
{
  struct timeval now;

  gettimeofday (&now, NULL);
  nicklistcache_rebuild (now);

  /* write out to all users except target. */
  target->SearchCnt++;
  target->CacheException++;

  /* we set the user to NULL so we don't get deleted by a search from this user */
  cache_queue (cache.asearch, NULL, cache.oplist);
  cache_queue (cache.psearch, NULL, cache.oplist);

  return 0;
}