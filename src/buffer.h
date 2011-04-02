/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#ifndef _BUFFER_H_
#define _BUFFER_H_

#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>

typedef struct buffer_stats {
  unsigned long long peak;
  unsigned long long size;
  unsigned long count;
  unsigned long max;
} buffer_stats_t;

/* remark:
 *   the buffer is allocated so that:
 *   the b->s pointer can be passed to "free"
 *   it will also make the list very overwrite sesitive
 */

typedef struct buffer {
  struct buffer *next, *prev;

  unsigned char *buffer,	/* pointer to the real buffer */
   *s,				/* start or the data */
   *e;				/* pointer to the first unused byte */
  unsigned long size;		/* allocation size of the buffer */
  unsigned int refcnt;		/* reference counter of the buffer */
} buffer_t;

extern buffer_stats_t bufferstats;

#define bf_used(buffer) 	((unsigned long)(buffer->e - buffer->s))
#define bf_unused(buf) 	((unsigned long)(buf->buffer + buf->size - buf->e))
#define bf_clear(buf)	(buf->e = buf->s)

extern buffer_t *bf_alloc (unsigned long size);
extern void bf_free (buffer_t * buffer);
extern void bf_free_single (buffer_t * buffer);
extern void bf_claim (buffer_t * buffer);

extern int bf_append_raw (buffer_t ** buffer, unsigned char *data, unsigned long size);
extern int bf_append (buffer_t ** buffer, buffer_t * b);

extern buffer_t *bf_sep (buffer_t ** list, unsigned char *sep);
extern buffer_t *bf_sep_char (buffer_t ** list, unsigned char sep);
extern int bf_prepend (buffer_t ** list, buffer_t * buf);

extern buffer_t *bf_copy (buffer_t * src, unsigned long extra);

extern int bf_strcat (buffer_t * dst, unsigned char *data);
extern int bf_strncat (buffer_t * dst, unsigned char *data, unsigned long length);
extern int bf_printf (buffer_t * dst, const char *format, ...);
extern int bf_vprintf (buffer_t * dst, const char *format, va_list ap);

extern unsigned long bf_size (buffer_t * src);

extern buffer_t *bf_buffer (unsigned char *text);

#endif
