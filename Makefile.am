
EXTRA_DIST = TODO ChangeLog LICENSE AUTHORS COPYING INSTALL README doc/aquila-manual.txt config.h.in depcomp install-sh missing
SUBDIRS = src tools

ACLOCAL_AMFLAGS = -I m4

commit:
	make -C src indent
	svn commit

dtinstall: install
	tools/aqdtinstall
