#!/bin/sh

uuid=452A8212-FFD8-4970-B61E-5CB3DCE908FF

# iOS simulator home directory:
# /Users/martin/Library/Application\ Support/iPhone\ Simulator/5.0/Applications/D30B45A4-7082-49F5-9495-413D7057030D/

make -C ios/ios all &&
make libfilement.so
#install_name_tool -id '/Users/martin/Library/Application Support/iPhone Simulator/5.0/Applications/D30B45A4-7082-49F5-9495-413D7057030D/ios.app/Frameworks/libfilement.dylib' lib/libfilement.dylib 
install_name_tool -id "/var/mobile/Applications/$uuid/Filement.app/Frameworks/libfilement.dylib" lib/libfilement.dylib 
