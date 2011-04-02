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
#include <ctype.h>

#include "buffer.h"
#include "utils.h"

const char *units[] = { "Bytes", "Kilobyte", "Megabyte", "Gigabyte", "Terabyte", "Petabyte" };

unsigned char *format_size (unsigned long long size)
{
  static unsigned char buf[128];
  int i;
  double output;

  i = 0;
  while (((size >> 10) > 0) && (i < 5)) {
    i++;
    output = size;
    size = size >> 10;
    output = output / 1024;
  };

  snprintf (buf, 128, "%.3f %s", output, units[i]);

  return buf;
}

const unsigned char rev_units[] = "bkmgtp";

unsigned long long parse_size (unsigned char *token)
{
  char *t;
  int i;
  unsigned long long size = 0, mod = 1;

  size = strtoll (token, &t, 10);
  if (!*t)
    return size;

  for (mod = 1, i = 0; rev_units[i] && (*t != rev_units[i]); i++, mod *= 1024);
  if (!rev_units[i])
    return size;

  return size * mod;
}

unsigned int time_print (buffer_t * b, unsigned long s)
{
  unsigned int weeks, days, hours, minutes, seconds, total;

  seconds = s % 60;
  minutes = s / 60;
  hours = minutes / 60;
  days = hours / 24;
  weeks = days / 7;
  minutes = minutes % 60;
  hours = hours % 24;
  days = days % 7;

  total = 0;

  if (weeks)
    total += bf_printf (b, "%u week%s, ", weeks, (weeks > 1) ? "s" : "");
  if (days)
    total += bf_printf (b, "%u day%s, ", days, (days > 1) ? "s" : "");

  if ((hours || minutes || seconds) || (!(weeks || days)))
    total += bf_printf (b, "%02u:%02u:%02u", hours, minutes, seconds);

  return total;
}

unsigned long time_parse (unsigned char *string)
{
  unsigned long total, reg;
  unsigned char *c;

  reg = 0;
  total = 0;
  c = string;
  while (*c && *c != ' ') {
    if (isdigit (*c)) {
      reg *= 10;
      reg += (*c - '0');
    } else if (isalpha (*c)) {
      switch (tolower (*c)) {
	case 'y':
	  reg *= 365 * 24 * 60 * 60;
	  break;
	case 'w':
	  reg *= 7 * 24 * 60 * 60;
	  break;
	case 'd':
	  reg *= 24 * 60 * 60;
	  break;
	case 'h':
	  reg *= 3600;
	  break;
	case 'm':
	  reg *= 60;
	  break;
	case 's':
	  break;
	default:
	  return 0;
      }
      total += reg;
      reg = 0;
    } else {
      return 0;
    }
    c++;
  }
  /* plain numbers are interpreted as seconds. */
  if (reg)
    total += reg;

  return total;
}
