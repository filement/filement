all:
	@echo 'Use "make install" to install Filement (requires root permissions)'
	@echo 'Use "make uninstall" to uninstall Filement (requires root permissions)'

install:
	cp -af usr/local/bin/* /usr/local/bin/
	cp -af usr/local/lib/* /usr/local/lib/
	cp -raf usr/local/share/filement /usr/local/share/
	cp -raf usr/share/* /usr/share/

uninstall:
	rm /usr/local/bin/filement*
	rm /usr/local/lib/libfilement*
	rm -r /usr/local/share/filement/
	rm -r /usr/share/applications/filement.desktop
	rm -r /usr/share/icons/hicolor/48x48/apps/filement.png
	rm -r /usr/share/icons/hicolor/256x256/apps/filement.png
