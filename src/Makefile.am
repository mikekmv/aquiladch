
AM_CFLAGS=@DEBUG@ @ZLINE@ @GCC_CFLAGS@ @USE_EPOLL@

EXTRA_DIST = buffer.h banlist.h commands.h hash.h nmdc_protocol.h rbt.h banlistclient.h config.h hashlist.h nmdc_token.h stringlist.h banlistnick.h core_config.h hashlist_func.h nmdc_utils.h user.h buffer.h defaults.h hub.h plugin.h utils.h builtincmd.h dllist.h leakybucket.h plugin_int.h cap.h esocket.h nmdc.h proto.h

bin_PROGRAMS = aquila
aquila_SOURCES = buffer.c esocket.c stringlist.c rbt.c utils.c hash.c dllist.c leakybucket.c config.c hub.c core_config.c hashlist.c user.c banlist.c banlistnick.c banlistclient.c plugin.c commands.c builtincmd.c cap.c nmdc_token.c nmdc_protocol.c nmdc_utils.c nmdc.c pi_user.c pi_statistics.c pi_chatroom.c pi_trigger.c pi_chatlog.c pi_statbot.c main.c
aquila_LDADD = -lcrypt @ZLIB@
