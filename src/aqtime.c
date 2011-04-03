#include <stddef.h>
#include "aqtime.h"
struct timeval now;

int gettime ()
{
  return gettimeofday (&now, NULL);
};
