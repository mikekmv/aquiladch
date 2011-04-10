	#include <stdio.h>
	#include <time.h>
int czas()
{
time_t t;
time(&t);
printf("%s", ctime(&t));
}
