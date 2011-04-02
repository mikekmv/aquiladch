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

#include "../config.h"
#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
#  include <stdint.h>
# endif
#endif

#include "hash.h"

#undef getshort
#if (defined(__GNUC__) && defined(__i386__)) || defined(__WATCOMC__)
#define getshort(d) (*((const unsigned short *) (d)))
#endif
#if defined(_MSC_VER) || defined(__BORLANDC__)
#define getshort(d) (*((const unsigned short *) (d)))
#endif

#if !defined(getshort)
#define getshort(d) ((((const unsigned char *) (d))[1] << 8UL)\
                     +((const unsigned char *) (d))[0])
#endif

__inline__ unsigned int SuperFastHash (const unsigned char *data, int len)
{
  unsigned long hash;
  int i, rem;

  if (len <= 0 || data == NULL)
    return 0;

  hash = (unsigned long) len;	/* Avoid 0 -> 0 trap */
  rem = len & 3;
  len >>= 2;

  /* Main loop */
  for (i = 0; i < len; i++) {
    hash += getshort (data);
    data += 2 * sizeof (char);
    hash ^= hash << 16;
    hash ^= getshort (data) << 11;
    data += 2 * sizeof (char);
    hash += hash >> 11;
  }

  /* Handle end cases */
  switch (rem) {
    case 3:
      hash += getshort (data);
      hash ^= hash << 16;
      hash ^= data[2 * sizeof (char)] << 18;
      hash += hash >> 11;
      break;
    case 2:
      hash += getshort (data);
      hash ^= hash << 11;
      hash += hash >> 17;
      break;
    case 1:
      hash += *data;
      hash ^= hash << 10;
      hash += hash >> 1;
  }

  /* Force "avalanching" of final 127 bits */
  hash ^= hash << 3;
  hash += hash >> 5;
  hash ^= hash << 2;
  hash += hash >> 15;
  hash ^= hash << 10;

  return hash;
}
