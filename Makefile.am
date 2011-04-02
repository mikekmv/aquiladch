
EXTRA_DIST = TODO ChangeLog LICENSE AUTHORS COPYING INSTALL README doc/aquila-manual.txt config.h.in
SUBDIRS = src tools

commit:
	make -C src indent
	svn commit

dtinstall: install
	tools/aqdtinstall
