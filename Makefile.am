
EXTRA_DIST = TODO ChangeLog LICENSE AUTHORS COPYING INSTALL README doc/aquila-manual.txt config.h.in depcomp install-sh missing
SUBDIRS = src tools

ACLOCAL_AMFLAGS = -I m4

commit:
	make -C src indent
	svn commit

release: commit dist
	@VERSION=`cat configure.ac | grep "AC_INIT" | cut -d'[' -f 5 | cut -d']' -f 1`
	svn copy svn://gw/aquila/trunk/aquila svn://gw/aquila/tags/aquila-${VERSION} -m "Aquila Release ${VERSION}"
	@echo Release ${VERSION}
	@echo File: aquila-${VERSION}.tar.gz

dtinstall: install
	tools/aqdtinstall
