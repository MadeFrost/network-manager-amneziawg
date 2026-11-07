# Shortcuts for the Meson build (meson.build is the real build description).
# The build directory is configured for packaging: prefix /usr.
MESON ?= meson
B     := build

all: $(B)/build.ninja
	$(MESON) compile -C $(B)

$(B)/build.ninja:
	$(MESON) setup $(B) --prefix=/usr

check: all
	$(MESON) test -C $(B) --suite unit --print-errorlogs

check-integration: all
	$(MESON) test -C $(B) --suite integration --print-errorlogs

deb:
	packaging/build-deb.sh

install: all
	$(MESON) install -C $(B) --no-rebuild

uninstall: $(B)/build.ninja
	$(MESON) compile -C $(B) uninstall

pot update-po: $(B)/build.ninja
	$(MESON) compile -C $(B) network-manager-amneziawg-$@

clean:
	rm -rf $(B)

.PHONY: all check check-integration deb install uninstall pot update-po clean
