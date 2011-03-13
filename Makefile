all: check_libxmlpatch
	$(MAKE) -C src

check_libxmlpatch: check_pkgconfig
	@pkg-config --exists libxmlpatch || (echo 'ERROR: libxmlpatch is not installed' && false)

check_pkgconfig:
	@which pkg-config || (echo 'ERROR: pkg-config is not installed' && false)
