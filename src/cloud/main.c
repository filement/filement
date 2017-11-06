#include <arpa/inet.h>
#include <sys/types.h>
#include <time.h>

#include "types.h"
#include "stream.h"
#include "cache.h"
#include "server.h"

#define NAME "FilementCloudProxy"

const struct string app_name = {.data = NAME, .length = sizeof(NAME) - 1};
const struct string app_version = {.data = "0.3.0", .length = 5};

int main(int argc, char *argv[])
{
	server_daemon();
	server_init();
	server_listen(0);

	return 0;
}
