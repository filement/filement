tell application "Finder"
	tell disk "Filement"
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
end tell
