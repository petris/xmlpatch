PREFIX ?= /usr/local/

.PHONY: all check_libxmlpatch check_libxml check_pkgconfig install

all: check_libxmlpatch check_libxml
	$(MAKE) -C src
	$(MAKE) -C doc

check_libxml: check_pkgconfig
	@pkg-config --exists libxml-2.0 || (echo 'ERROR: libxml-2.0 is not installed' && false)

check_libxmlpatch: check_pkgconfig
	@pkg-config --exists libxmlpatch || (echo 'ERROR: libxmlpatch is not installed' && false)

check_pkgconfig:
	@which pkg-config || (echo 'ERROR: pkg-config is not installed' && false)

install: all
	install -D -t $(PREFIX)/bin -o root -g root -m 0755 src/xmlpatch 
	install -D -t $(PREFIX)/share/man/man1 -o root -g root -m 0644 doc/xmlpatch.1.gz
