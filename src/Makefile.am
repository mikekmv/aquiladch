# for gettext
PACKAGE = @PACKAGE@
VERSION = @VERSION@

top_srcdir = @top_srcdir@
subdir = src

#
## NLS Support
#
datadir = @datadir@
localedir = $(datadir)/locale
LOCALE_DEFS=-DLOCALEDIR=\"$(localedir)\"
# "

#
## Set the correct flags for debugging support
#
if DEBUG
DEBUG_CFLAGS = -DDEBUG
DEBUG_FILES = stacktrace.c
else
DEBUG_CFLAGS =
DEBUG_FILES =
endif

#
## set platform specific stuff.
#
if USE_WINDOWS
WINDOWS_DEFS = -DUSE_WINDOWS
else
if GETADDRINFO
GETADDRINFO_SOURCES = getaddrinfo.c
endif
if USE_PTHREADDNS
DNS_FILES = dns.c
DNS_FLAGS = -D_REENTRANT -DUSE_PTHREADDNS
endif
endif

#
## Set LUA variables
#
if LUA
LUA_SOURCES = pi_lua.c
else
LUA_SOURCES =
endif

DEFS = @DEFS@ $(LOCALE_DEFS) $(WINDOWS_DEFS)

AM_CFLAGS=$(DEBUG_CFLAGS) @ZLINE@ @GCC_CFLAGS@ @ALLOW_EPOLL@ @ALLOW_POLL@ @ALLOW_LUA@ @LUA_INCLUDES@ @ALLOW_IOCP@ @CYGWIN_CFLAGS@ $(DNS_FLAGS)

EXTRA_DIST = buffer.h commands.h hash.h nmdc_protocol.h rbt.h banlistclient.h config.h hashlist.h nmdc_token.h stringlist.h core_config.h hashlist_func.h nmdc_utils.h user.h buffer.h defaults.h hub.h plugin.h utils.h builtincmd.h dllist.h leakybucket.h plugin_int.h cap.h esocket.h nmdc.h proto.h searchlist.c searchlist.h stacktrace.c stacktrace.h getaddrinfo.h getaddrinfo.c pi_lua.c nmdc_nicklistcache.h banlist.h nmdc_local.h tth.h aqtime.h iplist.h gettext.h dns.h xml.h
DISTCLEANFILES = .indent
STACKTRACEFILES = stacktrace.c

NMDC_SOURCES = nmdc_token.c nmdc_protocol.c nmdc_nicklistcache.c nmdc_utils.c nmdc.c nmdc_interface.c

bin_PROGRAMS = aquila
aquila_SOURCES = buffer.c \
		 esocket.c \
		 stringlist.c \
		 rbt.c utils.c \
		 hash.c \
		 dllist.c \
		 leakybucket.c \
		 config.c \
		 hub.c \
		 core_config.c \
		 $(NMDC_SOURCES) \
		 hashlist.c \
		 user.c \
		 banlist.c \
		 banlistclient.c \
		 plugin.c \
		 commands.c \
		 builtincmd.c \
		 cap.c \
		 pi_user.c \
		 pi_statistics.c \
		 pi_chatroom.c \
		 pi_trigger.c \
		 pi_chatlog.c \
		 pi_statbot.c \
		 pi_iplog.c \
		 pi_hublist.c \
		 $(LUA_SOURCES) \
		 main.c \
		 searchlist.c \
		 $(DEBUG_FILES) \
		 $(GETADDRINFO_SOURCES) \
		 tth.c \
		 aqtime.c \
		 iplist.c \
		 pi_configlock.c \
		 $(DNS_FILES) \
		 xml.c

aquila_LDADD = @ZLIB@ @LUALIB@ @CYGWIN_LDFLAGS@ @LIBINTL@

indent: .indent

.indent: $(aquila_SOURCES)
	indent -l100 -bad -bap -br -ce -cli2 -cbi2 -nss -nbc -brs -lp -npsl -saf -sai -saw $?
	rm *~
	touch .indent

commit:
	make -C .. commit

%.gcno: %.c
	touch $@

%.gcda: %.c
	touch $@

%.c.gcov : %.c %.gcno %.gcda
	gcov $(*).c || touch $(*).c.gcov

coverage: $(aquila_SOURCES:.c=.c.gcov)
	-cat $^ > coverage

coverage2: $(aquila_SOURCES)
	gcov -bn $^ > coverage2

clean-local:
	-rm -f *.gcov *.gcno *.gcda

