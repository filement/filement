#!/bin/bash

version=`cat version | tr -d '\n'`

target=mac/build/Release/Filement.app

export SUFFIX='.dylib'

# TODO use -install_name gcc option instead of install_name_tool

rm -rf "$target" &&
rm -rf bin/* &&
rm -rf mac/build &&
make libfilement.so &&
install_name_tool -id "/Applications/Filement.app/Contents/Frameworks/libfilement$SUFFIX" "lib/libfilement$SUFFIX" &&
cd mac/ &&
xcodebuild -project Filement.xcodeproj -configuration Release &&
cd .. &&
echo 'Copying bundle to bin/' &&
cp -rp "$target" bin/ &&
find bin/ -name '.DS_Store' -exec rm '{}' \; &&
cd bin/ &&
echo 'Preparing files for packing'
chmod a+x "Filement.app/Contents/MacOS/ffmpeg" &&
cp -r ../mac/.background . &&
#if [ "$1" == 'appstore' ] ; then echo 'Signing...'; codesign -s 'Filement' --deep Filement.app; fi &&
echo 'Packing files' &&
hdiutil create temp.dmg -volname "Filement" -ov -fs 'Case-sensitive HFS+' -fsargs "-c c=64,a=16,e=16" -srcfolder "Filement.app" -srcfolder ".background" -format UDRW -size 65536k &&
device=$(hdiutil attach -readwrite -noverify -noautoopen "temp.dmg" | $(which grep) -E '/Volumes/Filement$' | awk '{print $1}') &&
ln -s /Applications /Volumes/Filement/ &&
echo 'tell application "Finder"
	tell disk "'Filement'"
		open
		set current view of container window to icon view
		set toolbar visible of container window to false
		set statusbar visible of container window to false
		set the bounds of container window to {100, 100, 740, 500}
		set theViewOptions to the icon view options of container window
		set arrangement of theViewOptions to not arranged
		set icon size of theViewOptions to 128
		set background picture of theViewOptions to file ".background:archive.png"
		set position of item "Filement.app" of container window to {160, 200}
		set position of item "Applications" of container window to {480, 200}
		close
		open
		update without registering applications
		delay 10
	end tell
end tell' | osascript &&
rmdir /Volumes/Filement/.Trashes &&
chmod -Rf go-w /Volumes/Filement &&
sync &&
hdiutil detach "$device" &&
hdiutil convert temp.dmg -ov -format UDZO -imagekey zlib-level=9 -o filement.dmg &&
mv Filement.app files &&
rm -r temp.dmg .background &&
cd .. &&
echo 'Success.'
#tar cvfz filement_mac.tar.gz bin &&

# The above script is based on this howto:
# http://stackoverflow.com/questions/96882/how-do-i-create-a-nice-looking-dmg-for-mac-os-x-using-command-line-tools

# https://developer.apple.com/library/mac/documentation/security/conceptual/CodeSigningGuide/Procedures/Procedures.html
