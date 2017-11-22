# Filement

all: filement filement-gtk

pubcloud:
	$(MAKE) -C src pubcloud

filement:
	$(MAKE) -C src filement

filement-gtk:
	$(MAKE) -C src filement-gtk
	ln -f resources/background.png share/filement
	ln -f resources/logo.png share/filement
	ln -sf -t gui ../resources/applications ../resources/icons

check:
	$(MAKE) -C tests check

failsafe:
	$(MAKE) -C src failsafe

register:
	$(MAKE) -C src register

install:
	install -d /usr/local/bin /usr/local/lib /usr/local/share/filement
	install bin/* /usr/local/bin
	install lib/* /usr/local/lib
	cp -r share/* /usr/local/share
	cp -rL gui/* /usr/share

uninstall:
	rm -f /usr/local/bin/filement /usr/local/bin/filement-gtk
	rm -f /usr/local/lib/libfilement.so
	rm -rf /usr/local/share/filement/
	rm -f /usr/share/icons/hicolor/{48x48,256x256}/apps/filement.png /usr/share/applications/filement.desktop

clean:
	$(MAKE) -C src clean
	$(MAKE) -C tests clean
	rm -f share/filement/background.png share/filement/logo.png
	rm -f gui/applications gui/icons

mrproper: clean
	$(MAKE) -C src mrproper
