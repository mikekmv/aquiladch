
bin_PROGRAMS = aqpasswd
aqpasswd_SOURCES = aqpasswd.c
aqpasswd_LDADD = -lcrypt

EXTRA_DIST = aqdtinstall.in verli_import

noinst_SCRIPTS = aqdtinstall
CLEANFILES = aqdtinstall

aqdtinstall: aqdtinstall.in
	rm -rf $(@).tmp
	sed -e 's,@exec_prefix\@,$(prefix),g' $< > ${@}.tmp
	chmod +x ${@}.tmp
	mv ${@}.tmp $@
