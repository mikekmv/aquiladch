#                      -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT([[Aquila]], [[0.1.10-beta3]], [[jove@users.berlios.de]], [aquila])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/banlistclient.c])
AC_CONFIG_HEADER([config.h])

# check for gdb
AC_CHECK_TOOL(GDB, [gdb], [gdb])
AC_CHECK_TOOL(GPROF, [gprof], [gprof])

AM_GNU_GETTEXT([external], [need-ngettext])

# Checks for programs.
AC_PROG_CC
AC_CANONICAL_HOST

if test "${GCC}" = "yes" ;
then
	GCC_VERSION=`gcc --version | head -n 1 | cut -d ' ' -f 3`
	case $GCC_VERSION in
	3.3.*)  GCC_CFLAGS="-Wall -W -Wno-unused-parameter" ;;
	3.4.*)	GCC_CFLAGS="-Wall -Wextra -Wno-unused-parameter" ;;
	4.*.*)	GCC_CFLAGS="-Wall -Wextra -Wno-pointer-sign -Wno-unused-parameter" ;;
	esac
fi
AC_SUBST(GCC_CFLAGS)

# add arguments
AC_ARG_ENABLE(debug,
	AC_HELP_STRING([--enable-debug],[Turn on debugging (default OFF)]),
	[case "$enableval" in
	  yes|true) debug=true
		 ;;
	  no|false) debug=false
	  	 ;;
	  *)   AC_MSG_ERROR(bad value ${enableval} for --enable-debug);;
	esac],
	[unset debug])
AM_CONDITIONAL(DEBUG, test x$debug = xtrue)

sCFLAGS=""
AC_ARG_ENABLE(profiler,
	AC_HELP_STRING([--enable-profiler],[Turn on profiling (default OFF)]),
	[case "$enableval" in
	  yes|true) 
	  	if test "${GPROF}" = "gprof"
	  	then
		  	sCFLAGS="${sCFLAGS} -pg"
		else
			sCFLAGS="${sCFLAGS} -p"
		fi
		profiler=true;
		 ;;
	  no|false) 
	  	profiler=false;
	  	 ;;
	  *)   AC_MSG_ERROR(bad value ${enableval} for --enable-profiler);;
	esac],
	[])

AC_ARG_ENABLE(testcoverage,
	AC_HELP_STRING([--enable-testcoverage],[Turn on test coverage logging (default OFF)]),
	[case "$enableval" in
	  yes|true) 
	  	sCFLAGS="${sCFLAGS} -ggdb -O0 -fprofile-generate -ftest-coverage"
	  	testcoverage=true;
	  	 ;;
	  no|false)
		testcoverage=false; 
	  	 ;;
	  *)   AC_MSG_ERROR(bad value ${enableval} for --enable-testcoverage);;
	esac],
	[])

AC_ARG_ENABLE(useprofile,
	AC_HELP_STRING([--enable-useprofile],[Turn on optimization based on run-time profiles. This requires a previous run with option --enable-profiler. See FAQ. (default OFF)]),
	[case "$enableval" in
	  yes|true) 
	  	sCFLAGS="${sCFLAGS} -fprofile-use"
	  	useprofile=true;
	  	 ;;
	  no|false) 
	  	useprofile=false;
	  	 ;;
	  *)   AC_MSG_ERROR(bad value ${enableval} for --enable-testcoverage);;
	esac],
	[])

if test x$debug = xtrue;
then
	sCFLAGS="${sCFLAGS} -g${GDB} -O0"
	if test x$GDB = xgdb
	then
		sCFLAGS="${sCFLAGS} -DDUMP_CORE"
	fi
fi;

if test "${sCFLAGS}" != ""
then
	CFLAGS="${sCFLAGS}"
fi;
AC_SUBST(CFLAGS)


# Add zline argument
AC_ARG_ENABLE(zline,
	AC_HELP_STRING([--enable-zline],[Turn on ZLine support (default ON)]),
	[case "$enableval" in
	  yes|true) ZLINE=-DZLINES ;;
	  no|false) unset ZLINE ;;
	  *)   AC_MSG_ERROR(bad value ${enableval} for --enable-zline);;
	esac],
	[ZLINE=-DZLINES])
AC_SUBST(ZLINE)

# Add epoll argument. This allows users to disable epoll usage. Mostly for 2.4 kernels or older glibcs
AC_ARG_ENABLE(epoll,
	AC_HELP_STRING([--enable-epoll],[Allow epoll (default ON).]),
	[case "$enableval" in
	  yes|true) ALLOW_EPOLL=-DALLOW_EPOLL ;;
	  no|false) unset ALLOW_EPOLL ;;
	  *)   AC_MSG_ERROR(bad value ${enableval} for --enable-epoll);;
	esac],
	[ALLOW_EPOLL=-DALLOW_EPOLL])
AC_SUBST(ALLOW_EPOLL)

# Allow falback to poll. Poll is slow, but less limited than select.
AC_ARG_ENABLE(poll,
	AC_HELP_STRING([--enable-poll],[Allow poll() if no epoll (default ON).]),
	[case "$enableval" in
	  yes|true) ALLOW_POLL=-DALLOW_POLL ;;
	  no|false) unset ALLOW_POLL ;;
	  *)   AC_MSG_ERROR(bad value ${enableval} for --enable-poll);;
	esac],
	[ALLOW_POLL=-DALLOW_POLL])
AC_SUBST(ALLOW_POLL)

# Allow lua support
AC_ARG_ENABLE(lua,
	AC_HELP_STRING([--enable-lua],[Support LUA if available (default ON).]),
	[case "$enableval" in
	  yes|true) ALLOW_LUA=-DALLOW_LUA ;;
	  no|false) unset ALLOW_LUA ;;
	  *)   AC_MSG_ERROR(bad value ${enableval} for --enable-lua);;
	esac],
	[ALLOW_LUA=-DALLOW_LUA])

AC_ARG_WITH([lua-prefix],
            [AS_HELP_STRING([--with-lua-prefix=DIR],
                            [Lua files are in DIR])])
AC_ARG_WITH([lua-includes],
            [AS_HELP_STRING([--with-lua-includes=DIR],
                            [Lua include files are in DIR])])
AC_ARG_WITH([lua-libraries],
            [AS_HELP_STRING([--with-lua-libraries=DIR],
                            [Lua library files are in DIR])])
AC_ARG_WITH([lua-suffix],
            [AS_HELP_STRING([--with-lua-suffix=ARG],
                            [Lua binary and library files are suffixed with
                             ARG])])

if test "x$with_lua_prefix" = x; then
    lua_search_path="$PATH"
else
    lua_search_path="$with_lua_prefix/bin"
fi

if test "x$with_lua_includes" != x; then
    LUA_INCLUDES="-I$with_lua_includes"
elif test "x$with_lua_prefix" != x; then
    LUA_INCLUDES="-I$with_lua_prefix/include"
fi

if test "x$with_lua_libraries" != x; then
    LUA_LIBS="-L$with_lua_libraries"
elif test "x$with_lua_prefix" != x; then
    LUA_LIBS="-L$with_lua_prefix/lib"
fi


# Checks for libraries.
AC_CHECK_LIB(crypt, crypt)

if test "${ZLINE}" = "" ;
then
	unset ZLIB;
else
	AC_CHECK_LIB([z], [deflate], [ZLIB=-lz], [unset ZLINE])
fi
AC_SUBST(ZLIB)

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h fcntl.h netdb.h netinet/in.h netinet/tcp.h inttypes.h stdint.h stdlib.h string.h strings.h sys/socket.h sys/time.h unistd.h crypt.h string.h memory.h stdlib.h malloc.h locale.h])

# if epoll is enables, check for the header
if test "${ALLOW_EPOLL}" != "" ;
then
	AC_CHECK_HEADERS([sys/epoll.h linux/eventpoll.h])
fi

# if epoll is enables, check for the header
if test "${ALLOW_POLL}" != "" ;
then
	AC_CHECK_HEADERS([sys/poll.h])
fi

if test "${ZLINE}" != "";
then
	AC_CHECK_HEADERS([zlib.h], [], [unset ZLINE])
fi

dnl if test "${ALLOW_LUA}" != "" ;
dnl then
dnl 	AC_CHECK_HEADERS([lua.h lualib.h])
dnl fi

if test "${ALLOW_LUA}" != "" ;
then

save_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $LUA_INCLUDES"
AC_CHECK_HEADERS([lua.h lualib.h])
CPPFLAGS="$save_CPPFLAGS"

AC_CHECK_LIB([m], [exp], [lua_extra_libs="$lua_extra_libs -lm"], [])
AC_CHECK_LIB([dl], [dlopen], [lua_extra_libs="$lua_extra_libs -ldl"], [])
AC_CHECK_LIB([lua$with_lua_suffix],
             [lua_call],
             [LUA_LIBS="$LUA_LIBS -llua$with_lua_suffix -llualib$with_lua_suffix $lua_extra_libs"],
             [],
             [$LUA_LIBS $lua_extra_libs])

AC_MSG_CHECKING([for liblua version 5.0.x])
save_CPPFLAGS="$CPPFLAGS"
save_LIBS="$LIBS"
CPPFLAGS="$CPPFLAGS $LUA_INCLUDES"
LIBS="$LIBS $LUA_LIBS"
AC_RUN_IFELSE([AC_LANG_SOURCE([[

#include <lua.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int main()
{
    printf("(found %s)... ", LUA_VERSION);
    if(strstr (LUA_VERSION, "5.0"))
        exit(EXIT_SUCCESS);
    exit(EXIT_FAILURE);
}

]])],
              [AC_MSG_RESULT([yes])],
              [AC_MSG_RESULT([no])
               unset LUA_LIBS])

CPPFLAGS="$save_CPPFLAGS"
LIBS="$save_LIBS"

fi

LUALIB=${LUA_LIBS}
AC_SUBST(LUALIB)
AC_SUBST(LUA_INCLUDES)
AM_CONDITIONAL(LUA, test "${LUALIB}" != "" )

if test "${LUALIB}" == ""
then
	unset ALLOW_LUA
fi
AC_SUBST(ALLOW_LUA)

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
AC_CHECK_FUNCS([gettimeofday inet_ntoa memset select socket strcasecmp strchr strdup strerror strncasecmp strstr strtol memcpy strcasestr mallinfo])
AC_REPLACE_FUNCS(memset)

AC_TYPE_SOCKLEN_T
AC_TYPE_IN_PORT_T
AC_DECL_H_ERRNO

AC_CHECK_FUNCS(getaddrinfo getnameinfo)
if test "$ac_cv_func_getaddrinfo$ac_cv_func_getnameinfo" != yesyes ; then
	AC_LIBOBJ(getaddrinfo)
fi
AM_CONDITIONAL(GETADDRINFO, test "$ac_cv_func_getaddrinfo$ac_cv_func_getnameinfo" != yesyes)

# if epoll is enabled, check for the function
if test "${ALLOW_EPOLL}" != "" ;
then
	AC_CHECK_FUNCS([epoll_wait])
	if test "$ac_cv_func_epoll_wait" != yes ;
	then
		unset ALLOW_EPOLL
	else
		AC_MSG_CHECKING([if epoll_wait implemented])
		AC_RUN_IFELSE(
			[AC_LANG_PROGRAM([[#include <sys/epoll.h>
					   #include <errno.h>
					]],
				[[
					struct epoll_event event;
					int fd = epoll_create(1);
					int retval = epoll_wait (fd, &event, 1, 0);
					if (retval <0) {
						perror (errno);
						exit (errno);
					};
					exit (0);
				]])],
			[AC_MSG_RESULT([yes])],
			[AC_MSG_RESULT([no]) 
			 unset ALLOW_EPOLL]
		)
	fi	
	
fi

if test "${ALLOW_POLL}" != "" ;
then
	AC_CHECK_FUNCS([poll])
fi

case "${host}" in
  *-*-cygwin*)
  	if test x$debug = xfalse
  	then
  		CYGWIN_LDFLAGS="-Wl,--subsystem,windows";
  	fi
	;;
  *)
	;;
esac
AC_SUBST(CYGWIN_LDFLAGS)  

AC_CONFIG_FILES([Makefile po/Makefile.in
                 src/Makefile
                 tools/Makefile])

AC_OUTPUT

echo "-------------------------------------------------------------------------------"
echo "                            Aquila configuration"

echo    "   HOST                                             " $host

echo -n "   EPOLL Support                                     "
if test "${ALLOW_EPOLL}" != ""
then
	echo "ENABLED"
else
	echo "DISABLED"
fi

echo -n "   POLL Support                                      "
if test "${ALLOW_POLL}" != ""
then
	echo "ENABLED"
else
	echo "DISABLED"
fi

echo -n "   ZLine/ZPipe Support                               "
if test "${ZLINE}" != ""
then
	echo "ENABLED"
else
	echo "DISABLED"
fi

echo -n "   LUA Support                                       "
if test "${ALLOW_LUA}" != ""
then
	echo "ENABLED"
else
	echo "DISABLED"
fi

echo -n "   NLS Support                                       "
if test "${USE_NLS}" == "yes"
then
	echo "ENABLED"
else
	echo "DISABLED"
fi

echo
echo    " These should be disabled for a nornal build:"
echo -n "   DEBUG Support                                     "
if test x$debug = xtrue
then
	echo "ENABLED"
else
	echo "DISABLED"
fi

echo -n "   Profiler                                          "
if test "${profiler}" = "true"
then
	echo "ENABLED"
else
	echo "DISABLED"
fi

echo -n "   Test coverage                                     "
if test "${testcoverage}" = "true"
then
	echo "ENABLED"
else
	echo "DISABLED"
fi

echo -n "   Compile with runtime profiler optimization        "
if test "${useprofile}" = "true"
then
	echo "ENABLED"
else
	echo "DISABLED"
fi

echo "-------------------------------------------------------------------------------"

