/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#ifdef DEBUG
#include <assert.h>
#endif

#include "hub.h"
#include "core_config.h"
#include "nmdc_utils.h"
#include "cap.h"

buffer_t *rebuild_myinfo (user_t * u, buffer_t * b)
{
  buffer_t *d;
  unsigned int l;
  long long share;
  unsigned char *s, *e, *t;

  /* op myinfos are copied verbatim */

  /* normal users */
  l =
    12 + NICKLENGTH + 1 + config.MaxDescriptionLength + config.MaxTagLength + 2 + 2 +
    config.MaxSpeedLength + 1 + config.MaxEMailLength + 1 + config.MaxShareLength + 2;
  d = bf_alloc (l);

  s = b->s;
  bf_strcat (d, "$MyINFO $ALL ");
  s += 12;
  if ((s >= b->e) || (*s++ != ' '))
    goto nuke;

  /* verify nick */
  if (strncmp (s, u->nick, strlen (u->nick)))
    goto nuke;

  /* verify nicklength */
  s += strlen (u->nick);
  if ((s >= b->e) || (*s++ != ' '))
    goto nuke;

  /* append nick from clean copy */
  bf_strcat (d, u->nick);
  bf_strcat (d, " ");

  /* handle description */
  t = e = s;
  while ((*e != '$') && (*e != '<') && (e < b->e))
    e++;
  if (e == b->e)
    goto nuke;

  /* handle tag */
  if (*e == '<') {
    /* this loop should handle double tags correctly: ie, skip the first */
    while (e < b->e) {
      /* new tag char ? */
      if (*e == '<')
	s = e;
      /* end tag */
      if ((*e == '>') && (e[1] == '$')) {
	e++;
	break;
      }
      e++;
    };
    if (e == b->e)
      goto nuke;

    u->active = parse_tag (s, u);

    l = s - t;
  } else {
    /* mark as tagless client */
    u->active = -1;
    l = e - t;
  }

  /* the code above has skipped everything until the actual tag: this is the real desc field. 
     the start of the tag is the end of the desc... */
  if ((l > config.MaxDescriptionLength) && (!u->op))
    l = config.MaxDescriptionLength;
  bf_strncat (d, t, l);

  /* if we parsed a tag, append it */
  if (*s == '<') {
    l = e - s;
    /* FIXME perhaps generate a custom tag? */
    if ((l <= config.MaxTagLength) || (u->op))
      bf_strncat (d, s, l);
  }
  bf_strcat (d, "$ $");

  /* handle speed tag */
  if (*++e != ' ')
    goto nuke;
  if (*++e != '$')
    goto nuke;
  s = ++e;
  while ((*e != '$') && (e < b->e))
    e++;
  if (e == b->e)
    goto nuke;
  l = e - s;
  if (!l)
    goto nuke;			/* speed entries aren't allowed to be 0. they should at least contain a single character. */
  if ((l <= config.MaxSpeedLength) || (u->op)) {
    bf_strncat (d, s, l);
  } else {
    bf_strncat (d, s, config.MaxSpeedLength - 1);
    bf_strncat (d, s + l - 1, 1);	/* append last byte of his speed entry. */
  }
  bf_strcat (d, "$");

  /* handle email tag */
  s = ++e;
  while ((*e != '$') && (e < b->e))
    e++;
  if (e == b->e)
    goto nuke;
  l = e - s;
  if ((l <= config.MaxEMailLength) || (u->op))
    bf_strncat (d, s, l);
  bf_strcat (d, "$");

  /* add share tag */
  s = ++e;
  while ((*e != '$') && (e < b->e))
    e++;
  l = e - s;
  /* extrat sharesize */
  share = strtoll (s, NULL, 10);
  if (share >= 0LL) {
    u->share = share;
    if (!(u->rights & CAP_SHAREHIDE)) {
      /* include real sharesize */
      if ((l > config.MaxShareLength) && (!u->op))
	l = config.MaxShareLength;
      bf_strncat (d, s, l);
    } else {
      /* hide real sharesize */
      bf_strcat (d, "0");
    }
  } else {
    u->share = 0;
    bf_strcat (d, "0");
  }

  bf_strcat (d, "$");

  return d;
nuke:
  bf_free (d);
  return NULL;
}


int unescape_string (char *output, unsigned int j)
{
  unsigned int v;
  char *l, *k, *e;

  l = output;
  k = output;
  e = output + j;

  /* unescape string */
  for (; l < e; l++, k++) {
    *k = *l;
    if (*l != '/')
      continue;
    if (!sscanf (l, "/%%DCN%3u%%/", &v))
      continue;
    *k = (unsigned char) v & 127;
    l += 9;
  }

  /* move rest of string and terminate */
  /* FIXME!!
     while (*l)
     *k++ = *l++;
   */

  *k = '\0';

  return k - output;
}

int escape_string (char *output, int j)
{
  int i;
  char *l, *k;
  char tmpbuf[2048];

  if (j > 2048)
    return -1;

  memcpy (tmpbuf, output, j);

  l = tmpbuf;
  k = output;

  /* do not escape first $ character */
  if (*k == '$') {
    k++;
    l++;
    j--;
  };

  for (i = 0; (i < j) && ((k - output) < 2038); i++)
    switch (l[i]) {
      case 0:
      case 5:
      case 36:
      case 96:
      case 124:
      case 126:
	k += sprintf (k, "/%%DCN%03d%%/", l[i]);
	break;
      default:
	*k++ = l[i];
    }
  *k = '\0';

  return 0;
}

int parse_tag (char *desc, user_t * user)
{
  char tmpbuf[2048];
  char *s, *e, *t;
  int i;

  strncpy (tmpbuf, desc, 2048);

  /* $MyINFO $ALL Jove yes... i cannot type. I can Dream though...<DCGUI V:0.3.3,M:A,H:1,S:5>$ $DSL.$email$0$ */

  /* find start */
  s = tmpbuf;
  strsep (&s, "<");
  if (!s)
    return -1;

  /* find end */
  e = s;
  strsep (&e, "$");
  if (!e)
    return -1;
  /* terminate tag */
  e -= 2;
  if (*e != '>')
    return -1;

  /* terminate string at the >  and seach for the matching < */
  *e = '\0';
  while ((e > s) && (*e != '<'))
    e--;
  if (*e == '<')
    e++;
  s = e;

  /* client type */
  t = strsep (&s, " ");
  if (!s)
    return -1;
  strncpy (user->client, t, 64);

  /* client version */
  t = strsep (&s, ":");		/* V: */
  if (!s)
    return -1;
  t = strsep (&s, ",");
  if (!s)
    return -1;
  strncpy (user->versionstring, t, 64);
  sscanf (t, "%lf", &user->version);

  /* client mode */
  t = strsep (&s, ":");		/* M: */
  if (!s)
    return -1;
  t = strsep (&s, ",");
  if (!t)
    return -1;
  user->active = (tolower (*t) == 'a');

  /* hubs mode */
  t = strsep (&s, ",");
  if (!t)
    return -1;
  /* skip H: */
  strsep (&t, ":");
  if (!t)
    return -1;
  /* total hub count */
  i = 0;
  while ((*t) && (i < 3)) {
    user->hubs[i++] = strtol (t, &t, 0);
    if (!isdigit (*t) && (*t != '\0'))
      t++;
  }

  /* slots */
  t = strsep (&s, ",");
  if (!t)
    return -1;
  strsep (&t, ":");
  if (!t)
    return -1;
  user->slots = strtol (t, &t, 0);

  return user->active;
}

#ifdef ZLINES
#include <zlib.h>

/* compatibility with zlib 1.2.2 and older */
#ifndef Z_TEXT
#define Z_TEXT Z_ASCII
#endif

buffer_t *zline (buffer_t * input)
{
  z_stream stream;
  unsigned char *w, *o, *e;
  buffer_t *output, *work;

  if (bf_used (input) < ZLINES_THRESHOLD)
    return input;

  /* prepare work buffer */
  work = bf_alloc (bf_used (input) + 64);
  if (!work)
    return input;

  /* init zlib struct */
  memset (&stream, 0, sizeof (stream));
  stream.zalloc = Z_NULL;
  stream.zfree = Z_NULL;
  stream.data_type = Z_TEXT;

  memset (&stream, 0, sizeof (stream));
  deflateInit (&stream, Z_BEST_COMPRESSION);

  stream.next_in = input->s;
  stream.avail_in = bf_used (input);

  stream.next_out = work->s;
  stream.avail_out = work->size;

  /* compress */
  if (deflate (&stream, Z_FINISH) != Z_STREAM_END) {
    deflateEnd (&stream);
    bf_free (work);
    return input;
  }
  work->e = work->s + stream.total_out;

  /* cleanup zlib */
  deflateEnd (&stream);

  /* size increased. we won't use this. */
  if (bf_used (work) >= bf_used (input)) {
    bf_free (work);
    return input;
  }

  /* allocate output buffer */
  output = bf_alloc (bf_used (input) + 4);
  if (!output) {
    bf_free (work);
    return input;
  }

  /* build Zline. escape the zblob */
  bf_strcat (output, "$Z ");
  for (w = work->s, o = output->e, e = output->buffer + output->size; (w < work->e) && (o < e);
       w++, o++) {
    switch (*w) {
      case '\\':
	*o++ = '\\';
	*o = '\\';
	break;
      case '|':
	*o++ = '\\';
	*o = 'P';
	break;
      default:
	*o = *w;
    }
  }

  bf_free (work);

  if (o >= e) {
    bf_free (output);
    return input;
  }

  output->e = o;
  bf_strcat (output, "|");

  return output;
}

buffer_t *zunline (buffer_t * input)
{
  z_stream stream;
  unsigned char *o, *i, *e;
  buffer_t *output, *work;

  /* prepare work buffer */
  work = bf_alloc ((bf_used (input) * 2) + 64);

  i = input->s + 3;		/* skip "$Z " */
  for (o = work->s, e = work->buffer + work->size; (i < input->e) && (o < e); o++, i++) {
    if (*i != '\\') {
      *o = *i;
      continue;
    }
    i++;
    switch (*i) {
      case '\\':
	*o = '\\';
	break;
      case 'P':
	*o = '|';
	break;
    }
  }
  work->e = o;

  /* allocate output buffer */
  output = bf_alloc (bf_used (work) * 10);

  /* init zlib struct */
  memset (&stream, 0, sizeof (stream));
  stream.zalloc = Z_NULL;
  stream.zfree = Z_NULL;
  stream.data_type = Z_TEXT;

  stream.avail_in = bf_used (work);
  stream.next_in = work->s;

  stream.next_out = output->s;
  stream.avail_out = output->size;

  inflateInit (&stream);

  /* compress */
  if (inflate (&stream, Z_FINISH) != Z_STREAM_END) {
    inflateEnd (&stream);
    bf_free (work);
    bf_free (output);
    return NULL;
  }
  /* cleanup zlib */
  deflateEnd (&stream);

  output->e = output->s + stream.total_out;

  return output;
}


#endif