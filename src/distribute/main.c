#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "types.h"
#include "log.h"
#include "stream.h"
#include "cache.h"
#include "server.h"
#include "protocol.h"
#include "filement.h"
#include "proxies.h"
#include "devices.h"
#include "events.h"
#include "subscribe.h"

#define ERROR_DAEMON "Failed to daemonize\n"
#define ERROR_SOCKET "Socket error\n"
#define ERROR_BIND "Bind error\n"
#define ERROR_LISTEN "Listen error\n"
#define ERROR_EPOLL "Epoll error\n"
#define ERROR_SIGNAL "Signal handling error\n"
#define ERROR_THREAD "Unable to start a thread\n"

#define LISTEN_MAX 10

// TODO remove this (used for core dump)
#include <sys/resource.h>

const struct string app_name = {.data = "FilementDistribute", .length = sizeof("FilementDistribute") - 1};
const struct string app_version = {.data = "0.14.0", .length = 6};

/* Filement master server
This server coordinates the work of the whole filement infrastructure

Signals must be handled by the main thread.
*/

typedef void *handler_t(void *);

handler_t main_http;

#if defined(DIST1)
handler_t *handler[] = {&main_device, &main_device_tls};
static const int port[] = {PORT_DISTRIBUTE_DEVICE, PORT_DISTRIBUTE_DEVICE_TLS};
#elif defined(DIST2)
handler_t *handler[] = {&main_event};
static const int port[] = {PORT_DISTRIBUTE_EVENT};
#else
handler_t *handler[] = {&main_device, &main_device_tls, &main_event};
static const int port[] = {PORT_DISTRIBUTE_DEVICE, PORT_DISTRIBUTE_DEVICE_TLS, PORT_DISTRIBUTE_EVENT};
#endif

#define SERVERS (sizeof(port) / sizeof(*port))

void *main_http(void *arg)
{
	server_listen(0);
	return 0;
}

static int server_socket(void)
{
	int ssocket;
	int optval;

	// Create socket
	ssocket = socket(PF_INET6, SOCK_STREAM, 0);
	if (ssocket < 0) fail(3, ERROR_SOCKET);

	// Disable TCP time_wait
	optval = 1;
	if (setsockopt(ssocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0)
		fail(3, ERROR_SOCKET);

	// TODO: make socket nonblocking

	return ssocket;
}

int main(void)
{
	/////// TODO remove this
	struct rlimit core_limits;
	core_limits.rlim_cur = core_limits.rlim_max = RLIM_INFINITY;
	setrlimit(RLIMIT_CORE, &core_limits);	
	//////////

#if RUN_MODE > Debug
	server_daemon();

	close(0);
	close(1);
	close(2);
	open("/dev/null", O_RDONLY);
	open("/dev/null", O_WRONLY);
	if (open("/var/log/distribute", O_CREAT | O_WRONLY | O_APPEND, 0644) < 0) fail(2, ERROR_DAEMON);
	warning(logs("Server started")); // TODO change this
#endif

	server_init();

	pthread_t thread_id;

#if !defined(DIST1)
	pthread_create(&thread_id, 0, main_http, 0);
	pthread_detach(thread_id);
#endif

	// Support both IPv4 and IPv6
	struct sockaddr_in6 address = {
		.sin6_family = AF_INET6,
		.sin6_addr = in6addr_any
	};
	int socket[SERVERS];
	struct pollfd server[SERVERS];
	size_t index = 0;

	// Initialize the server sockets
	for(index = 0; index < SERVERS; ++index)
	{
		socket[index] = server_socket();

		address.sin6_port = htons(port[index]);
		if (bind(socket[index], (struct sockaddr *)&address, sizeof(address)) < 0)
			fail(4, ERROR_BIND);
		if (listen(socket[index], LISTEN_MAX) < 0)
			fail(5, ERROR_LISTEN);

		server[index].fd = socket[index];
		server[index].events = POLLIN;
		server[index].revents = 0;
	}

	subscribe_init();

	socklen_t length;
	struct connection *info = malloc(sizeof(struct connection));
	if (!info) fail(1, "Memory error");

	// Wait for clients
	while (true)
	{
		if (poll(server, SERVERS, -1) < 0) continue;

		for(index = 0; index < SERVERS; ++index)
		{
			if (server[index].revents)
			{
				if (server[index].revents & POLLIN)
				{
					// Establish a connection with a client
					length = sizeof(info->address);
					if ((info->socket = accept(socket[index], (struct sockaddr *)&info->address, &length)) < 0)
					{
						warning(logs("Unable to accept (errno="), logi(errno), logs(")"));
						continue; // TODO: change this
					}
					warning(logs("accepted: "), logi(info->socket)); // TODO remove this

					// Start a thread to handle the new connection
					pthread_create(&thread_id, 0, handler[index], info);
					pthread_detach(thread_id);

					info = malloc(sizeof(struct connection));
					if (!info) fail(1, "Memory error");
				}

				// TODO: handle error conditions

				server[index].revents = 0;
			}
		}
	}

	// This is never reached

	// free(info);

	// subscribe_term();

	// tls_term();

	// close(server[2]);
	// close(server[1]);
	// close(server[0]);

	return 0;
}
