/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#ifndef _NMDC_UTILS_H_
#define _NMDC_UTILS_H_
#include "hub.h"

/* minimum number of bytes in the buffer before you decide to actually compress it. */
#define ZLINES_THRESHOLD	100

extern int escape_string (char *output, int j);
extern int unescape_string (char *output, unsigned int j);
extern int parse_tag (char *desc, user_t * user);
extern buffer_t *rebuild_myinfo (user_t * u, buffer_t * b);

#ifdef ZLINES

extern buffer_t *zline (buffer_t *);
extern buffer_t *zunline (buffer_t * input);

#endif

#endif /* _NMDC_UTILS_H_ */
