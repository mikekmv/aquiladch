#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT([[Aquila]], [[0.1.3]], [[jove@users.berlios.de]], [aquila])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/banlistclient.c])
AC_CONFIG_HEADER([config.h])

# check for gdb
AC_CHECK_TOOL(GDB, [gdb], [gdb])

# add arguments
AC_ARG_ENABLE(debug,
	AC_HELP_STRING([--enable-debug],[Turn on debugging (default OFF)]),
	[case "$enableval" in
	  yes|true) DEBUG=-DDEBUG
		 ;;
	  no|false)  unset DEBUG 
	  	 ;;
	  *)   AC_MSG_ERROR(bad value ${enableval} for --enable-debug);;
	esac],
	[unset DEBUG])
AC_SUBST(DEBUG)

if test "${DEBUG}" != "" ;
then
	CFLAGS="-g${GDB} -O0"
fi
AC_SUBST(CFLAGS)

AC_ARG_ENABLE(zline,
	AC_HELP_STRING([--enable-zline],[Turn on ZLine support (default ON)]),
	[case "$enableval" in
	  yes|true) ZLINE=-DZLINES ;;
	  no|false) unset ZLINE ;;
	  *)   AC_MSG_ERROR(bad value ${enableval} for --enable-zline);;
	esac],
	[ZLINE=-DZLINES])
AC_SUBST(ZLINE)

AC_ARG_ENABLE(epoll,
	AC_HELP_STRING([--enable-epoll],[Turn on epoll support (default ON)]),
	[case "$enableval" in
	  yes|true) USE_EPOLL=-DUSE_EPOLL ;;
	  no|false) unset USE_EPOLL ;;
	  *)   AC_MSG_ERROR(bad value ${enableval} for --enable-epoll);;
	esac],
	[USE_EPOLL=-DUSE_EPOLL])
AC_SUBST(USE_EPOLL)

# Checks for programs.
AC_PROG_CC

if test "${GCC}" = "yes" ;
then
	GCC_VERSION=`gcc --version | head -n 1 | cut -d ' ' -f 3`
	case $GCC_VERSION in
	3.3.*)  GCC_CFLAGS="-Wall -W -Wno-unused-parameter" ;;
	3.4.*)	GCC_CFLAGS="-Wall -Wextra -Wno-unused-parameter" ;;
	esac
fi
AC_SUBST(GCC_CFLAGS)

# Checks for libraries.
AC_CHECK_LIB(crypt, crypt)

if test "${ZLINE}" = "" ;
then
	unset ZLIB;
else
	AC_CHECK_LIB([z], [deflate], [ZLIB=-lz])
fi
AC_SUBST(ZLIB)
	
# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h fcntl.h netdb.h netinet/in.h inttypes.h stdint.h stdlib.h string.h strings.h sys/socket.h sys/time.h unistd.h crypt.h])

# if epoll is enables, check for the header
if test "${USE_EPOLL}" != "" ;
then
	AC_CHECK_HEADERS([sys/epoll.h linux/eventpoll.h])
fi

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
AC_HEADER_TIME

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_SELECT_ARGTYPES
AC_TYPE_SIGNAL
AC_FUNC_STAT
AC_CHECK_FUNCS([gettimeofday inet_ntoa memset select socket strcasecmp strchr strdup strerror strncasecmp strstr strtol])

# if epoll is enabled, check for the function
if test "${USE_EPOLL}" != "" ;
then
	AC_CHECK_FUNCS([epoll_wait])
fi

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 tools/Makefile])

AC_OUTPUT
