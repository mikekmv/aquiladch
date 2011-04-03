
bin_PROGRAMS = aqpasswd
aqpasswd_SOURCES = aqpasswd.c
aqpasswd_LDADD = -lcrypt

EXTRA_DIST = aqdtinstall.in verli_import ddch_import

noinst_SCRIPTS = aqdtinstall
CLEANFILES = aqdtinstall

aqdtinstall: aqdtinstall.in
	rm -rf $(@).tmp
	sed -e 's,@exec_prefix\@,$(prefix),g' ${@}.in > ${@}.tmp
	chmod +x ${@}.tmp
	mv ${@}.tmp $@

mrproper: clean
	-rm -f *.gcov *.gcno *.gcda
