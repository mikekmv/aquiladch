/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#include <sys/time.h>

#include "leakybucket.h"

__inline__ int get_token (leaky_bucket_type_t * type, leaky_bucket_t * bucket, time_t now)
{
  if (bucket->tokens) {
    bucket->tokens--;
    return 1;
  }

  if (bucket->lasteval != now) {
    unsigned long t;

    t = ((now - bucket->timestamp) / type->period);
    bucket->timestamp += type->period * t;
    bucket->lasteval = now;
    bucket->tokens += (t * type->refill);
    /* never store more tokens than the burst value */
    if (bucket->tokens > type->burst)
      bucket->tokens = type->burst;
  }

  if (!bucket->tokens)
    return 0;

  bucket->tokens--;
  return 1;
}

__inline__ void init_bucket (leaky_bucket_t * bucket, unsigned long now)
{
  bucket->lasteval = now;
  bucket->timestamp = now;
  bucket->tokens = 0;
}

__inline__ void init_bucket_type (leaky_bucket_type_t * type, unsigned long period,
				  unsigned long burst, unsigned long refill)
{
  type->period = period;
  type->burst = burst;
  type->refill = refill;
}
