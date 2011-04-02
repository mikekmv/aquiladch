/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#ifndef _LEAKY_BUCKET_H_
#define _LEAKY_BUCKET_H_

#include <sys/time.h>

typedef struct leaky_bucket {
  time_t timestamp;
  time_t lasteval;
  unsigned long tokens;
} leaky_bucket_t;

typedef struct leaky_bucket_type {
  unsigned long period;
  unsigned long burst;
  unsigned long refill;
} leaky_bucket_type_t;

extern inline int get_token (leaky_bucket_type_t * type, leaky_bucket_t * bucket, time_t now);
extern inline void init_bucket (leaky_bucket_t * bucket, unsigned long now);
extern inline void init_bucket_type (leaky_bucket_type_t * type, unsigned long period,
				     unsigned long burst, unsigned long refill);

#endif
