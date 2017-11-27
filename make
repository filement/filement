#!/bin/bash

if [ $# -lt 1 ]
then
	echo "Usage: $0 <target> [debug]"
	exit
fi

if [ -z "$FILEMENT_CORE" ]
then export CC=gcc
fi

version=`head -c -1 version`

target="$1"

# TODO -D_REENTRANT

#OPTIONS='-std=c99 -Werror -Wchar-subscripts -Wimplicit -Wsequence-point -Wno-parentheses -Wno-return-type -Wno-empty-body -Wno-deprecated-declarations'
OPTIONS='-std=c99 -Werror -Wchar-subscripts -Wimplicit -Wsequence-point -Wcast-align'
if [ "$2" = 'debug' ]
then OPTIONS="-g $OPTIONS -DDEBUG"
else OPTIONS="-O2 $OPTIONS"
fi

INC='-I../include/ -I./ -Ilib/'

# Detect operating system
case "$OSTYPE" in
	linux*)
		OS='Linux'
		DEF="-D_BSD_SOURCE -D_GNU_SOURCE -D_POSIX_SOURCE -D_FILE_OFFSET_BITS=64 -DPLATFORM_ID=4 -DOS_BSD -DOS_LINUX -DPREFIX=\"/\" -DEXECUTABLE=\"/usr/local/bin/filement\" -fPIC"
		LIB="-lm -lz -ldl"
		SHARED='-shared'
		SUFFIX='.so'
		SONAME='-soname'
		#OPTIONS="$OPTIONS -Wno-unused-result"
		export LD_RUN_PATH="/usr/local/lib"
		;;
	darwin*)
		OS='MacOS X'
		DEF="-D_BSD_SOURCE -DPLATFORM_ID=2 -DOS_BSD -DOS_MAC -DPREFIX=\"/Applications/Filement.app/\" -DEXECUTABLE=\"/Applications/Filement.app/Contents/MacOS/Filement\" -fPIC"
		LIB="-lz -framework CoreFoundation -framework CoreServices"
		SHARED='-dynamiclib'
		SUFFIX='.dylib'
		SONAME='-install_name'
		;;
	msys*)
		OS='Windows'
		DEF="-D_POSIX_SOURCE -DBIG_ENDIAN=4321 -DLITTLE_ENDIAN=1234 -DBYTE_ORDER=LITTLE_ENDIAN -DOS_WINDOWS -DFILEMENT_AV -DFILEMENT_TLS -DFILEMENT_THUMBS -DFILEMENT_UPNP"
		LIB="-L../windows/ -lm -lz -lwsock32 -lws2_32 -liphlpapi -lshlwapi -L. -lsqlite3 -lgnutls -lavformat -lavutil -lavcodec -lfilement_indexsearch"
		INC="$INC -I../windows/"
		SHARED='-shared'
		SUFFIX='.dll'
		FS="../windows/mingw.c"
		F="remote.c ../windows/mingw.c"
		WINDOWS='true'
		;;
	*)
		echo "Unsupported platform \"${OSTYPE}\" (try \"export OSTYPE\")."
		exit
		;;
esac
DEF="$DEF -D"$(echo "$target" | tr '[:lower:]' '[:upper:]')
LIB="-L../lib/ -lpthread $LIB"

# Uncomment this to compile for flmntdev.com
#DEF="$DEF -DTEST=-10"
#dev=true

# Uncomment this to compile distribute server for upgrade testing
#DEF="$DEF -DUPGRADE_TEST"

case "$target" in
	device)
		LIB="$LIB -lgnutls"
		if [ "$OS" != 'Linux' ]; then LIB="$LIB -lavformat -lavutil -lavcodec"; fi
		DEF="$DEF -DTLS"
		#DEF="$DEF "
		F="$F upload.c dlna/*.c"
		storage=sqlite
		;;
	distribute)
		LIB="distribute/ipdb.o $LIB -lm -lmysqlclient -lmemcached -lgnutls -Wl,-unresolved-symbols=ignore-in-shared-libs"
		DEF="$DEF -DTLS"
		F="$F upload.c"
		storage=mysql
		;;
	cloud)
		LIB="$LIB -lgnutls"
		DEF="$DEF -DTLS"
		F="$F upload.c"
		storage=sqlite
		;;
	ftp)
		LIB="/root/libcurl.a /root/libcares.a $LIB -lrt -lssl -lcrypto -lz -lidn"
		storage=sqlite
		;;
	pubcloud)
		LIB="$LIB -lrt -lm -lmysqlclient -lavutil -lavcodec -lavformat"
		F="$F upload.c"
		storage=mysql
		;;
	pubsync)
        LIB="$LIB -lm -lmysqlclient -lavutil -lavcodec -lavformat"
		F="$F upload.c"
		storage=mysql
        ;;
esac

echo "Compile $target for $OS"

# Compile external libraries
#if [ "$WINDOWS" = '' ]; then
#	cd 'external'
#	for lib in 'gnutls'
#	do
#		if [ ! -L "../lib/lib${lib}${SUFFIX}" ]
#		then
#			echo "Compile lib${lib}"
#			. ./"${lib}.compile"
#		fi
#	done
#	cd '..'
#fi

# cd external && ./epeg.compile && cd ..

if [ "$2" = 'debug' ]
then DEF="$DEF -DRUN_MODE=1"
else DEF="$DEF -DRUN_MODE=2"
fi

cd src

library()
{
	src=
	obj=
	for item in $1
	do
		src="$src $item.c"
		obj="$obj $item.o"
	done

	dest="$2.a"

	if [ "$WINDOWS" = '' ]
	then $CC -I../../include/ -I../ -c $OPTIONS $DEF $src
	else $CC -I../../include/ -c $OPTIONS $DEF $src
	fi
	if [ -f "$dest" ]; then rm "$dest"; fi
	ar rcs "$dest" $obj
	rm $obj
}

# Compile libraries
cd lib/
library "buffer string vector dictionary queue format sha2 log" types
library stream stream
library json json
library aes aes
cd ..

# Compile SQLite
if [ "$WINDOWS" = '' ]; then
	if [ ! -f sqlite.o ]
	then
		echo 'Compile SQLite'
		$CC -c -O2 -fPIC $OPTIONS $DEF sqlite.c
	fi
fi

# Generate actions
if [ "$target" != 'distribute' ]
then
	echo 'Generate actions'
	gcc -E -x c $DEF -o 'actions.out' - < 'actions.in';
	./actions_sort.pl $target
fi

echo 'Compile'

if [ "$target" = 'device' -a "$2" = 'debug' ]
then LIB="debug/main.c $LIB"
fi

if [ "$target" = 'device' ]
then sed -e "s/@{VERSION}/$version/" -e 's/@{VERSION_LENGTH}/6/' < device/device.c.in > device/device.c
elif [ "$target" = 'pubcloud' ]
then sed -e "s/@{VERSION}/$version/" -e 's/@{VERSION_LENGTH}/6/' < pubcloud/main.c.in > pubcloud/main.c
fi

if [ -d "actions_$target" ]
then ARG="actions_$target/*.c"
fi

if [ "$WINDOWS" = '' ]; then
	#if [ "$target" == 'distribute' ]
	#then ARG="$ARG sqlite.o lib/*.a $LIB"
	#elif [ "$target" != 'device' -o "$2" = 'debug' ]
	if [ "$target" != 'device' -o "$2" = 'debug' ]
	then ARG="$ARG sqlite.o ../lib/libminiupnpc_filement.a lib/*.a -o ../bin/$target $LIB"
	else ARG="$ARG -c"
	fi
else
	sed -e "s/@{VERSION}/$version/" -e 's/@{VERSION_LENGTH}/6/' < ../windows/filement_upgrade_lib.c.in > ../windows/filement_upgrade_lib.c
	if [ "$target" != 'device' -o "$2" = 'debug' ]
	then ARG="$ARG ../lib/libepeg.a ../lib/libjpeg.a ../lib/libminiupnpc_filement.a lib/*.a -o ../bin/$target $LIB"
	else ARG="$ARG -c"
	fi
fi

# Switch the next two lines to compile for flmntdev.com
if [ "$target" = 'distribute' -a -z "$dev" ]
then
	echo 'Compile distribute1'
	$CC $OPTIONS $INC $DEF -DDIST1 $F \
	$target/*.c \
	evfs.c \
	io.c \
	security.c \
	http.c \
	http_parse.c \
	http_response.c \
	status.c \
	actions.c \
	access.c \
	server.c \
	download.c \
	zip.c \
	tar.c \
	earchive.c \
	cache.c \
	operations.c \
	magic.c \
	storage_$storage.c \
	$ARG -o ../bin/distribute1

	echo 'Compile distribute2'
	$CC $OPTIONS $INC $DEF -DDIST2 $F \
	$target/*.c \
	evfs.c \
	io.c \
	security.c \
	http.c \
	http_parse.c \
	http_response.c \
	status.c \
	actions.c \
	access.c \
	server.c \
	download.c \
	zip.c \
	tar.c \
	earchive.c \
	cache.c \
	operations.c \
	magic.c \
	storage_$storage.c \
	$ARG -o ../bin/distribute2

	echo 'Compiled'
else $CC $OPTIONS $INC $DEF $F \
	$target/*.c \
	evfs.c \
	io.c \
	security.c \
	http.c \
	http_parse.c \
	http_response.c \
	status.c \
	actions.c \
	access.c \
	server.c \
	download.c \
	zip.c \
	tar.c \
	earchive.c \
	cache.c \
	operations.c \
	magic.c \
	storage_$storage.c \
	$ARG
fi

if [ "$target" != 'device' -o "$2" = 'debug' ]; then cd ..; exit; fi

# Create device static library
echo 'Create library'
case "$OSTYPE" in
	msys*)
		# Windows
		#mv sqlite.o sqlite.o.backup
		gcc $ARG $OPTIONS $INC $DEF
		gcc $INC $DEF $OPTIONS -DOS_WINDOWS -L../windows/ ../windows/filement_device_lib.c -o ../bin/filement_device_lib.dll *.o  ../lib/libepeg.a ../lib/libjpeg.a  ../lib/libminiupnpc_filement.a lib/*.a -lwsock32 -lgnutls -lz -lpthread -lws2_32 -lshlwapi -liphlpapi -lavformat -lavutil -lavcodec -lfilement_indexsearch -L./ -lsqlite3 -s -shared -Wl,--subsystem,windows,--kill-at cache.c
		rm *.o
		
		echo 'Create failsafe'

	OPTIONS='-std=c99 -O2 -Werror -Wchar-subscripts -Wimplicit -Wsequence-point -Wcast-align'
	DEF="-DFAILSAFE -DOS_WINDOWS -DRUN_MODE=4 -D_POSIX_SOURCE -DBIG_ENDIAN=4321 -DLITTLE_ENDIAN=1234 -DBYTE_ORDER=LITTLE_ENDIAN  "
	INC='-I../include/ -I../src/lib/ -I../windows/ -L../windows/'

	echo gcc $OPTIONS $DEF $INC $FS \
		lib/string.c \
		lib/format.c \
		lib/dictionary.c \
		lib/vector.c \
		lib/json.c \
		lib/stream.c \
		lib/log.c \
		io.c \
		http.c \
		http_parse.c \
		storage_sqlite.c \
		../windows/sqlite.o \
		device/distribute.c \
		device/upgrade.c \
		device/startup.c \
		../windows/filement_upgrade_lib.c \
		-o ../bin/filement_upgrade_lib.dll -lwsock32 -lws2_32 -lshlwapi -liphlpapi -s -shared -Wl,--subsystem,windows,--kill-at

	gcc $OPTIONS $DEF $INC $FS \
		lib/string.c \
		lib/format.c \
		lib/dictionary.c \
		lib/vector.c \
		lib/json.c \
		lib/stream.c \
		lib/log.c \
		io.c \
		http.c \
		http_parse.c \
		upload.c \
		storage_sqlite.c \
		../windows/sqlite.o \
		device/distribute.c \
		device/upgrade.c \
		device/startup.c \
		../windows/filement_upgrade_lib.c \
		-o ../bin/filement_upgrade_lib.dll -lwsock32 -lws2_32 -lshlwapi -liphlpapi -s -shared -Wl,--subsystem,windows,--kill-at

		#mv sqlite.o.backup sqlite.o
		;;
	*)
		# UNIX
		# -Wl,-headerpad,10000
		$CC $DEF $INC $OPTIONS $SHARED *.o cache.c lib/types.a lib/stream.a lib/json.a lib/aes.a lib/libepeg.a lib/libjpeg.a lib/libminiupnpc_filement.a -o "../lib/libfilement$SUFFIX" $LIB
		mv sqlite.o sqlite.o.backup
		rm *.o
		mv sqlite.o.backup sqlite.o
		;;
esac

cd ..
