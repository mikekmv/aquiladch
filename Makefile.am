
EXTRA_DIST = TODO ChangeLog LICENSE AUTHORS COPYING INSTALL README doc/aquila-manual.txt config.h.in depcomp install-sh missing
SUBDIRS = src tools

CYGCRYPT_PACKAGE = crypt/crypt-1.1-1.tar.bz2
CYGCRYPT_SRC =  $(subst .tar.bz2,-src.tar.bz2,${CYGCRYPT_PACKAGE})
CYGCRYPT_FILE = usr/bin/cygcrypt-0.dll
CYGWIN1_PACKAGE = cygwin/cygwin-1.5.21-2.tar.bz2
CYGWIN1_SRC = $(subst .tar.bz2,-src.tar.bz2,${CYGWIN1_PACKAGE})
CYGWIN1_FILE = usr/bin/cygwin1.dll
CYGZ_PACKAGE = zlib/zlib-1.2.3-1.tar.bz2
CYGZ_SRC =  $(subst .tar.bz2,-src.tar.bz2,${CYGZ_PACKAGE})
CYGZ_FILE = usr/bin/cygz.dll
CYGWIN_FTP = ftp://ftp.easynet.be/cygwin/release/

ACLOCAL_AMFLAGS = -I m4

commit:
	make -C src indent
	svn commit

release: commit dist
	svn copy svn://gw/aquila/trunk/aquila svn://gw/aquila/tags/aquila-${VERSION} -m "Aquila Release ${VERSION}"
	@echo Release ${VERSION}
	@echo File: aquila-${VERSION}.tar.gz

dtinstall: install
	tools/aqdtinstall

cygdist/%.tar.bz2:
	( PWD=`pwd`; mkdir -p ${@D}; cd ${@D}; wget ${CYGWIN_FTP}/${*}.tar.bz2 ; cd ${PWD})

aquila-${VERSION}-cygbin.tgz: all-recursive cygdist/${CYGCRYPT_PACKAGE} cygdist/${CYGWIN1_PACKAGE} cygdist/${CYGZ_PACKAGE}
	@(cd cygdist; \
	tar jxvf ${CYGCRYPT_PACKAGE} ${CYGCRYPT_FILE}; \
	tar jxvf ${CYGWIN1_PACKAGE} ${CYGWIN1_FILE}; \
	tar jxvf ${CYGZ_PACKAGE} ${CYGZ_FILE}; \
	mkdir -p aquila-${VERSION}-cygbin; \
	cp ${CYGCRYPT_FILE} aquila-${VERSION}-cygbin; \
	cp ${CYGWIN1_FILE} aquila-${VERSION}-cygbin; \
	cp ${CYGZ_FILE} aquila-${VERSION}-cygbin; \
	cp ../src/aquila.exe aquila-${VERSION}-cygbin; \
	cp ../tools/aqpasswd.exe aquila-${VERSION}-cygbin; \
	tar zcvf aquila-${VERSION}-cygbin.tgz aquila-${VERSION}-cygbin; \
	mv aquila-${VERSION}-cygbin.tgz .. ; \
	echo Generated	Aquila ${VERSION} Cygwin Dist aquila-${VERSION}-cygbin.tgz; \
	)

cygdist: aquila-${VERSION}-cygbin.tgz

cygsrc: cygdist/${CYGCRYPT_SRC} cygdist/${CYGWIN1_SRC} cygdist/${CYGZ_SRC}
	@mkdir -p cygsrc
	cp $^ cygsrc/

