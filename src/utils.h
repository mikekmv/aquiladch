/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#ifndef _UTILS_H_
#define _UTILS_H_

extern unsigned char *format_size (unsigned long long size);
extern unsigned long long parse_size (unsigned char *token);
extern unsigned int time_print (buffer_t * b, unsigned long s);
extern unsigned long time_parse (unsigned char *string);

#endif /* _UTILS_H_ */
