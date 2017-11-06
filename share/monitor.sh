#!/bin/bash

export PATH='/usr/local/bin:/usr/bin:/bin'
export LD_LIBRARY_PATH='/usr/local/lib:/usr/lib:/lib'

TIMEOUT=5
track="proxy_device proxy_cloud proxy_ftp"

while :
do
	for item in $track
	do
		if ! pgrep "$item" > /dev/null
		then "$item"
		fi
	done

	sleep "$TIMEOUT"
done
