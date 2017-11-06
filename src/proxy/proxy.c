#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "types.h"
#include "stream.h"
#include "io.h"
#include "proxy.h"

#define DEVICE 0
#define CLIENT 1

// TODO: use setsockopt and SO_RCVTIMEO

void *main_proxy(void *arg)
{
	struct thread_info *info = arg;
	struct request *request = info->request;
	struct stream response_stream;

	short ready[2] = {0, 0};
	struct pollfd proxyfd[2] = {
		[DEVICE] = {.events = POLLIN | POLLOUT, .fd = info->device->fd},
		[CLIENT] = {.events = POLLIN | POLLOUT, .fd = request->stream.fd}
	};

	unsigned char byte;

	struct string buffer;

	// Notify the device that a request has arrived.
	// 1 == HTTP
	// 2 == HTTPS
#if defined(TLS)
	//byte = (request->stream._tls ? 2 : 1); // TODO: change this line
	byte = (request->stream._tls ? 4 : 3); // TODO: change this line
#else
	//byte = 1;
	byte = 3;
#endif
	if (socket_write(proxyfd[DEVICE].fd, &byte, 1) != 1) goto finally;

#if defined(TLS)
	if (request->stream._tls)
	{
		if (stream_init_tls_accept(&response_stream, proxyfd[DEVICE].fd)) goto finally;
	}
	else
#endif
	{
		if (stream_init(&response_stream, proxyfd[DEVICE].fd)) goto finally;
	}

	// Transfer data until a peer closes its connection or error occurs.
	while (true)
	{
		if ((proxyfd[CLIENT].events & POLLIN) && stream_cached(&request->stream)) proxyfd[CLIENT].revents |= POLLIN;
		else if (poll(proxyfd, 2, TIMEOUT) < 0)
		{
			if (errno == EINTR) continue;
			else break;
		}

		// Check both client and the device for newly arrived events.
		if (proxyfd[DEVICE].revents)
		{
			if (proxyfd[DEVICE].revents & POLLERR) break;
			ready[DEVICE] |= proxyfd[DEVICE].revents;
			proxyfd[DEVICE].revents = 0;
		}
		if (proxyfd[CLIENT].revents)
		{
			if (proxyfd[CLIENT].revents & POLLERR) break;
			ready[CLIENT] |= proxyfd[CLIENT].revents;
			proxyfd[CLIENT].revents = 0;
		}

		// Do any data transfer that will not block.
		if ((ready[DEVICE] & POLLIN) && ((ready[CLIENT] & (POLLOUT | POLLHUP)) == POLLOUT)) // device -> client
		{
			if (stream_read(&response_stream, &buffer, 1)) break;
			if (stream_write(&request->stream, &buffer) || stream_write_flush(&request->stream)) break;
			stream_read_flush(&response_stream, buffer.length);

			ready[DEVICE] &= ~POLLIN;
			ready[CLIENT] &= ~POLLOUT;
		}
		if ((ready[CLIENT] & POLLIN) && ((ready[DEVICE] & (POLLOUT | POLLHUP)) == POLLOUT)) // client -> device
		{
			if (stream_read(&request->stream, &buffer, 1)) break;
			if (stream_write(&response_stream, &buffer) || stream_write_flush(&response_stream)) break;
			stream_read_flush(&request->stream, buffer.length);

			ready[CLIENT] &= ~POLLIN;
			ready[DEVICE] &= ~POLLOUT;
		}

		// No more transfers are possible if one of the peers closed the connection.
		if ((ready[DEVICE] | ready[CLIENT]) & POLLHUP) break;

		// Make poll ignore all events marked as ready.
		proxyfd[DEVICE].events = (POLLIN | POLLOUT) & ~ready[DEVICE];
		proxyfd[CLIENT].events = (POLLIN | POLLOUT) & ~ready[CLIENT];
	}

	// Data transfer is finished. Shut the connections down and make epoll handle their termination.

	stream_term(&response_stream);
finally:
	stream_term(&request->stream);

	free(request);

	shutdown(proxyfd[DEVICE].fd, SHUT_RDWR);
	shutdown(proxyfd[CLIENT].fd, SHUT_RDWR);

	struct epoll_event event = {.events = EPOLLRDHUP, .data.ptr = info->device};
	epoll_ctl(info->epoll, EPOLL_CTL_ADD, proxyfd[DEVICE].fd, &event); // TODO: maybe check for error here
	event.data.ptr = info->client;
	epoll_ctl(info->epoll, EPOLL_CTL_ADD, proxyfd[CLIENT].fd, &event); // TODO: maybe check for error here

	free(info);

	return 0;
}
