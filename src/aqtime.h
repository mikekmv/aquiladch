/* $Id$
 *  (C) Copyright 2011 Former Developer: Johan Verrept (jove@users.berlios.de)
 *                      Now Continued And Maitained By https://Aquila-dc.info                                                 
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
#ifndef _AQTIME_H_

#ifdef __USE_W32_SOCKETS
#include <winsock2.h>
#else
#include <sys/time.h>
#endif

extern struct timeval now;

extern int gettime ();

#endif
