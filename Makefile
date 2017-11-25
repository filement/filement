# Filement

mac_target=mac/build/Release/Filement.app

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

mac/build/Release/Filement.app/Contents/MacOS/Filement:
	$(MAKE) -C src libfilement.so
	ln -f resources/background.png share/filement
	ln -f resources/logo.png share/filement
	install_name_tool -id "/Applications/Filement.app/Contents/Frameworks/libfilement.dylib" "lib/libfilement.dylib" # TODO ideally I should keep the original lib
	cd mac && xcodebuild -project Filement.xcodeproj -configuration Release

filement.dmg: mac/build/Release/Filement.app/Contents/MacOS/Filement
	find $(mac_target) -name '.DS_Store' -delete
	chmod a+x $(mac_target)"/Contents/MacOS/ffmpeg"
	hdiutil create '/tmp/temp.dmg' -volname "Filement" -ov -fs 'Case-sensitive HFS+' -fsargs "-c c=64,a=16,e=16" -srcfolder $(mac_target) -srcfolder "resources/.background" -format UDRW -size 65536k
	hdiutil attach -readwrite -noverify '/tmp/temp.dmg'
	ln -s /Applications /Volumes/Filement
	osascript < mac/makeimage
	rmdir /Volumes/Filement/.Trashes
	chmod -Rf go-w /Volumes/Filement
	sync
	hdiutil detach /Volumes/Filement
	hdiutil convert /tmp/temp.dmg -ov -format UDZO -imagekey zlib-level=9 -o gui/filement.dmg
	rm /tmp/temp.dmg

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
	cp -rL gui/* /usr/share 2> /dev/null || :

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
	rm -f mac/build/Release/Filement.app/Contents/MacOS/Filement

mrproper: clean
	$(MAKE) -C src mrproper
