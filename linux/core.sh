#!/bin/bash

cd filement

version=`head -c -1 version`

arch=$(uname -m)
if [ "$arch" = 'i686' ]; then arch='x86'; fi

target="bin/filement_${version}.${arch}"
prefix='/usr/local'

export CC='gcc44'
export SUFFIX='.so'

./configure &&
make clean libfilement.so &&
$CC -O2 -std=c99 -DRUN_MODE=2 -DPREFIX=\"${prefix}\" -Iinclude/ -Isrc/ -Isrc/lib/ 'linux/main.c' -o "${target}${prefix}/bin/filement" -Llib/ -lfilement -Wl,-unresolved-symbols=ignore-in-shared-libs &&

cd ..
