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
#include <crypt.h>
#include <time.h>
#include "../src/cap.h"
       
int main (int argc, char **argv) {
	unsigned char salt[3];
	FILE *fp;
	
	if (argc < 3) {
		printf ("%s <filename> <user> <passwd>\n", argv[0]);
		return 0;
	}
	
	srandom (time(NULL));
	salt [2] = 0;
	fp = fopen (argv[1], "a");
	if (!fp) {
		perror ("Error: ");
		return 0;
	}

	if (ftell (fp) == 0) {
		fprintf (fp, "T regs %lu 5\n", CAP_REG);
		fprintf (fp, "T vips %lu 4\n", CAP_VIP);
		fprintf (fp, "T ops %lu 3\n", CAP_OP);
		fprintf (fp, "T cheefs %lu 2\n", CAP_CHEEF);
		fprintf (fp, "T admins %lu 1\n", CAP_ADMIN);
		fprintf (fp, "T owner %lu 0\n", CAP_ADMIN|CAP_OWNER);
	} else
		printf ("Appending new owner %s to file %s\n", argv[2], argv[1]);

	salt[0] = ((random() % 46) + 45);
	salt[1] = ((random() % 46) + 45);
	salt[2] = 0;
	fprintf (fp, "A %s %s %u %u %u\n", argv[2], crypt (argv[3], salt), CAP_ADMIN|CAP_OWNER, 0, 0);
	
	fclose (fp);
	
	return 0;
}
