#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if !defined(OS_WINDOWS)
# include <arpa/inet.h>
# include <netdb.h>
# include <poll.h>
#else
#include <sys/stat.h>
#define WINVER 0x0501
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "mingw.h"
#endif

#include "types.h"
#include "log.h"

#ifdef OS_BSD
# include "actions.h"
# include "format.h"
# include "json.h"
# include "device/upgrade.h"
#else
# include "../actions.h"
# include "format.h"
# include "json.h"
# include "../device/upgrade.h"
#include <stdio.h>
#endif

#define REACHABLE_TIMEOUT 1000

int reachable(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	if (json_type(query) != OBJECT) return BadRequest;

	struct string key;
	union json *ip, *port;
	struct string json;

	key = string("ip");
	ip = dict_get(query->object, &key);
	if (!ip || (json_type(ip) != STRING)) return BadRequest;

	key = string("port");
	port = dict_get(query->object, &key);
	if (!port || (json_type(port) != INTEGER)) return BadRequest;

	int fd;
	int flags;
	struct addrinfo hints, *result = 0, *item;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	char buffer[6]; // TODO: don't hardcode this
	*format_uint(buffer, port->integer) = 0;
	getaddrinfo(ip->string_node.data, buffer, &hints, &result);

	// Cycle through the results until the socket is connected successfully
	for(item = result; item; item = item->ai_next)
	{
		fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
		if (fd < 0) continue;

#if !defined(OS_WINDOWS)
		// Make the socket non-blocking for the connect() operation.
		flags = fcntl(fd, F_GETFL, 0);
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);

		// Connect to the server.
		if (!connect(fd, result->ai_addr, result->ai_addrlen)) goto success;
		else if (errno == EINPROGRESS)
		{
			int status;
			struct pollfd pollsock = {.fd = fd, .events = POLLOUT, .revents = 0};

			do status = poll(&pollsock, 1, REACHABLE_TIMEOUT);
			while (status < 0);
			if (pollsock.revents & POLLOUT) goto success;
		}
		close(fd);
#else
		if (!connect(fd, result->ai_addr, result->ai_addrlen)) goto success;
#endif
	}

	freeaddrinfo(result);

	json = string("{status:0}");
	return (remote_json_send(request,response, resources, &json) ? 0 : -1);

success:

	freeaddrinfo(result);
#if !defined(OS_WINDOWS)
	close(fd);
#else
	CLOSE(fd);
#endif
	
	json = string("{status:1}");
	return (remote_json_send(request,response, resources, &json) ? 0 : -1);
}
