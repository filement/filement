#if !defined(OS_WINDOWS)
# include <arpa/inet.h>
# include <netinet/in.h>
# include <poll.h>
# include <sys/ioctl.h>
# include <sys/socket.h>
#endif
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#if defined(OS_WINDOWS)
# include <sys/stat.h>
# define WINVER 0x0501
# include <windows.h>
# include <winsock2.h>
# include <ws2tcpip.h>
# include "mingw.h"
# include <stdio.h>
# undef close
# define close CLOSE
#endif

#include "types.h"
#include "arch.h"
#include "stream.h"
#include "format.h"
#include "log.h"
#if !defined(OS_WINDOWS)
# include "io.h"
# include "cache.h"
# include "server.h"
#else
# include "../io.h"
# include "../cache.h"
# include "../server.h"
#endif
#include "proxy.h"
#include "distribute.h"

#define SLEEP_THRESHOLD		(TIMEOUT + 3000) /* TIMEOUT + 3s */
#define REFRESH_TIMEOUT		240000 /* 240s */

#define PROXY_WAIT_MIN		15
#define PROXY_WAIT_MAX		240

#define PROXIES_COUNT		3

#define DOMAIN_LENGTH_MAX	127

#if !defined(OS_WINDOWS)
extern struct string UUID;
#else
extern struct string UUID_WINDOWS;
#endif

extern int control[2];
extern int pipe_proxy[2];

struct host
{
	// TODO add name length?
	char name[DOMAIN_LENGTH_MAX + 1];
	uint16_t port;
};

static struct host *restrict proxy_discover(size_t *restrict count)
{
	struct host *proxies = 0;
	size_t index;

	uint32_t size;

	struct string buffer;
	struct stream *stream = distribute_request_start(0, CMD_PROXY_LIST, 0);
	if (!stream) return 0;

	int status;

	// Get proxies count.
	if (status = stream_read(stream, &buffer, sizeof(uint32_t))) goto error;
	endian_big32(&size, buffer.data);
	if ((size < 1) || (64 < size)) goto error; // TODO: don't hardcode these
	*count = size;
	stream_read_flush(stream, sizeof(uint32_t));

	proxies = malloc(sizeof(struct host) * *count);
	if (!proxies) goto error;

	// Read proxies list.
	for(index = 0; index < *count; ++index)
	{
		if (stream_read(stream, &buffer, sizeof(uint32_t))) goto error;
		endian_big32(&size, buffer.data);
		if (size > DOMAIN_LENGTH_MAX) goto error;
		stream_read_flush(stream, sizeof(uint32_t));

		if (stream_read(stream, &buffer, size)) goto error;
		*format_string(proxies[index].name, buffer.data, size) = 0;
		stream_read_flush(stream, size);

		if (stream_read(stream, &buffer, sizeof(uint16_t))) goto error;
		endian_big16(&proxies[index].port, buffer.data);
		stream_read_flush(stream, sizeof(uint16_t));
	}

	distribute_request_finish(true);
	return proxies;

error:
	distribute_request_finish(false);
	free(proxies);
	return 0;
}

static int proxy_connect(const struct host *proxy)
{
	// Connect to a proxy.
	int fd = socket_connect(proxy->name, proxy->port);
	if (fd < 0) return -1;

	// Send identification to the proxy.
#if !defined(OS_WINDOWS)
	if (socket_write(fd, UUID.data, UUID.length) != UUID.length)
#else
	if (socket_write(fd, UUID_WINDOWS.data, UUID_WINDOWS.length) != UUID_WINDOWS.length)
#endif
	{
		close(fd);
		return -1;
	}

	return fd;
}

// TODO the device should close the connection and start a new one
// http://blog.netherlabs.nl/articles/2009/01/18/the-ultimate-so_linger-page-or-why-is-my-tcp-not-reliable
// http://www.serverframework.com/asynchronousevents/2011/01/time-wait-and-its-design-implications-for-protocols-and-scalable-servers.html
// http://stackoverflow.com/questions/3757289/tcp-option-so-linger-zero-when-its-required

// Waits for an incoming request from the proxy.
// Returns the protocol for the connection (). On timeout, returns 0. On error returns error code.
static int proxy_request(int fd, int control)
{
	struct pollfd wait[2];
	int status;
	time_t start, last, now;

	wait[0].fd = fd;
	wait[0].events = POLLIN;
	wait[0].revents = 0;

	wait[1].fd = control;
	wait[1].events = POLLIN;
	wait[1].revents = 0;

	// Wait until a client establishes a connection or an error occurs.
	// Close the connection after REFRESH_TIMEOUT or when computer suspension is detected.
	last = start = time(0);
	while (true)
	{
#if !defined(OS_WINDOWS)
		status = poll(wait, 2, TIMEOUT);
#else
		status = poll(wait, 1, TIMEOUT); //TODO make pipe implementation from socket on port 0 for windows
#endif
		if (status > 0)
		{
			if ((wait[0].revents & (POLLERR | POLLHUP)) || wait[1].revents) break;

			// Read a byte from the proxy to detect whether to use HTTP or HTTPS.
			unsigned char protocol;
			if (socket_read(fd, &protocol, 1) != 1) break;
			if (protocol > 0x02) protocol -= 2; // compatibility with 0.15 and 0.16 as explained in doc/versions
			return protocol;
		}
		else if (status < 0) return -1;
		else
		{
			now = time(0);
			if (((now - last) * 1000 >= SLEEP_THRESHOLD) || ((now - start) * 1000 >= REFRESH_TIMEOUT))
			{
				close(fd);
				return 0;
			}
			last = now;
		}
	}

	close(fd);
	return -1;
}

void *main_proxy(void *storage)
{
	int bytes;
	unsigned wait = PROXY_WAIT_MIN;

	int fd;
	int protocol;
	struct host *proxies = 0;
	size_t proxy_index, count;

	struct connection_proxy connection;
	struct sockaddr_in *address;

#if defined(OS_WINDOWS)
	unsigned long iMode = 0;
	int iResult = 0;
#endif

	// TODO design thread cancellation better

	// Connect to a discovered proxy server. Wait for a client.
	// On client request, invoke a thread to handle it and use the current thread to start a new connection to wait for more clients.
	while (1)
	{
		// Make sure proxy list is initialized.
		if (!proxies)
		{
			count = PROXIES_COUNT;
			proxies = proxy_discover(&count);
			if (!proxies)
			{
				warning_("Unable to discover proxies.");
				goto error;
			}
			proxy_index = 0;
		}

		while (1)
		{
			// Connect the device to a proxy.
			fd = proxy_connect(proxies + proxy_index);
			if (fd < 0)
			{
				warning(logs("Unable to connect to "), logs(proxies[proxy_index].name, strlen(proxies[proxy_index].name)), logs(":"), logi(proxies[proxy_index].port));
				if (++proxy_index < count) continue;
				else
				{
					// All proxies have failed. Wait a while and get new proxy list.
					warning_("All proxies failed.");
					free(proxies);
					proxies = 0;
					break;
				}
			}

			debug(logs("Connected to "), logs(proxies[proxy_index].name, strlen(proxies[proxy_index].name)), logs(":"), logi(proxies[proxy_index].port));
			wait = PROXY_WAIT_MIN;

			proxy_index = 0;

			// Wait for a request. On timeout, make sure the connection is still established.
			protocol = proxy_request(fd, control[0]);
			if (!protocol)
			{
				debug_("Reconnect to the proxy.");
				continue; // connection expired; start a new one
			}

			memset(&connection.address, 0, sizeof(connection.address));
			address = (struct sockaddr_in *)&connection.address;
			address->sin_family = AF_INET;
			address->sin_addr.s_addr = INADDR_ANY; // TODO put proxy address here
			address->sin_port = htons(proxies[proxy_index].port);

			connection.client = fd;
			connection.protocol = protocol;

			write(pipe_proxy[1], (void *)&connection, sizeof(connection));
		}

error:

		// TODO read the bytes and check their value when necessary to support different commands
#if !defined(OS_WINDOWS)
		ioctl(control[0], FIONREAD, &bytes);
		if (bytes)
		{
			close(control[0]);
			warning_("Filement stopped.");
			break;
		}
#else
		iResult = ioctlsocket(control[0], FIONREAD, &iMode);
		if (iResult != NO_ERROR)
		  printf("ioctlsocket failed with error: %ld\n", iResult);
		  
		 if (iMode)
		{
			close(control[0]);
			warning("Filement stopped.");
			break;
		}
#endif

		// Wait before trying to connect to the proxy again
		warning_("Wait and reconnect.");
		sleep(wait);
		if (wait < PROXY_WAIT_MAX) wait *= 2;
	}

	free(proxies);

	return 0;
}

#if defined(OS_WINDOWS)
void *main_proxy(void *storage)
{
	int bytes;
	unsigned wait = PROXY_WAIT_MIN;

	int fd;
	int protocol;
	struct host *proxies = 0;
	size_t proxy, count;

#if defined(OS_WINDOWS)
	unsigned long iMode = 0;
	int iResult = 0;
#endif

	struct resources *info = 0;
	pthread_t thread;

	// TODO design thread cancellation better

	// Connect to a discovered proxy server. Wait for a client.
	// On client request, invoke a thread to handle it and use the current thread to start a new connection to wait for more clients.
	while (true)
	{
		// Make sure proxy list is initialized.
		if (!proxies)
		{
			count = PROXIES_COUNT;
			proxies = proxy_discover(&count);
			if (!proxies)
			{
				warning_("Unable to discover proxies.");
				goto error;
			}
			proxy = 0;
		}

		while (true)
		{
			// Connect the device to a proxy.
			fd = proxy_connect(proxies + proxy);
			if (fd < 0)
			{
				warning(logs("Unable to connect to "), logs(proxies[proxy].name, strlen(proxies[proxy].name)), logs(":"), logi(proxies[proxy].port));
				if (++proxy < count) continue;
				else
				{
					// All proxies have failed. Wait a while and get new proxy list.
					warning_("All proxies failed.");
					free(proxies);
					proxies = 0;
					break;
				}
			}

			debug(logs("Connected to "), logs(proxies[proxy].name, strlen(proxies[proxy].name)), logs(":"), logi(proxies[proxy].port));
			wait = PROXY_WAIT_MIN;

			proxy = 0;

			if (!info)
			{
				info = malloc(sizeof(struct resources));
				if (!info) fail(1, "Memory error");
				memset(info, 0, sizeof(struct resources));
				info->storage = storage;
			}

			// Wait for a request. On timeout, make sure the connection is still established.
			protocol = proxy_request(fd, control[0]);
#if defined(TLS)
			if (protocol == PROXY_HTTPS)
			{
				debug_("Incoming HTTPS request");
				if (stream_init_tls_connect(&info->stream, fd, 0)) // TODO: set third argument
				{
					close(fd);
					continue; // TODO should this continue?
				}
			}
			else
#endif
			if (protocol == PROXY_HTTP)
			{
				debug_("Incoming HTTP request");
				if (stream_init(&info->stream, fd)) fail(1, "Memory error");
			}
			else if (!protocol)
			{
				debug_("Reconnect to the proxy.");
				continue; // connection expired; start a new one
			}
			else
			{
				warning_("Broken proxy connection.");
				break;
			}

			// Start a thread to handle the established connection.
			// TODO: check for errors; stream_term() on error
			pthread_create(&thread, 0, &server_serve_windows, info);
			pthread_detach(thread);
			info = 0;
		}

error:

		// TODO read the bytes and check their value when necessary to support different commands
#if !defined(OS_WINDOWS)
		ioctl(control[0], FIONREAD, &bytes);
		if (bytes)
		{
			close(control[0]);
			warning_("Filement stopped.");
			break;
		}
#else
		iResult = ioctlsocket(control[0], FIONREAD, &iMode);
		if (iResult != NO_ERROR)
		  printf("ioctlsocket failed with error: %ld\n", iResult);
		  
		 if (iMode)
		{
			close(control[0]);
			warning("Filement stopped.");
			break;
		}
#endif

		// Wait before trying to connect to the proxy again
		warning_("Wait and reconnect.");
		sleep(wait);
		if (wait < PROXY_WAIT_MAX) wait *= 2;
	}

	free(info);
	free(proxies);

	return 0;
}
#endif
