#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#include <pthread.h>		// libpthread
#include <curl/curl.h>		// libcurl

#ifdef OS_BSD
#include <arpa/inet.h>
#endif

#include "types.h"
#include "log.h"
#include "stream.h"
#include "storage.h"
#include "http.h"
#include "cache.h"
#include "server.h"

#if RUN_MODE <= Debug
#include <stdio.h>
#endif

#define LISTEN_MAX 20

const struct string app_name = {.data = "Filement FTP Proxy", .length = sizeof("Filement FTP Proxy") - 1};
const struct string app_version = {.data = "0.7.0", .length = 5};

static void *storage;

// TODO: storage creation

int main(int argc, char *argv[])
{
	extern void *storage;

	curl_global_init(CURL_GLOBAL_ALL);
	server_daemon();
	server_init();
	storage = storage_init();
	//if (!storage)
	//	{
	//	debug("filement_device_init->storage_init false\n");
		
	//	return false;
	//	}
	server_listen(storage);

	return 0;
}
