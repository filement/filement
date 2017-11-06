#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <resolv.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "types.h"
#include "format.h"
#include "io.h"

// TODO move this somewhere else
// Supported functions:
// open()
// stat(), fstat(), lstat()
// read(), readv(), write()
// opendir()
// readdir_r()
// mmap()
// mkdir()
// socket(), poll(), connect()
int errno_error(int code)
{
	switch (code)
	{
	case ENOMEM:
	case EMFILE:
	case ENFILE:
	case EDQUOT:
	case ENOBUFS:
	case EMLINK:
	case EISCONN: // TODO is this right (doesn't look right for connect())
	case EADDRNOTAVAIL:
		return ERROR_MEMORY;

	case EACCES:
	case EPERM:
		return ERROR_ACCESS;

	case EEXIST:
		return ERROR_EXIST;

	case ELOOP:
	case ENAMETOOLONG:
	case ENOENT:
	case ENOTDIR:
	case ENXIO: // TODO this doesn't seem right for write()
		return ERROR_MISSING;

	case EFAULT:
	case EINVAL:
	case EBADF:
	case ENOTSOCK:
	case EALREADY:
	case EOPNOTSUPP:
		return ERROR_INPUT;

	case ETXTBSY:
	case ETIMEDOUT:
	case EINTR: // TODO is this right?
	case EAGAIN:
# if (EWOULDBLOCK != EAGAIN)
	case EWOULDBLOCK:
# endif
		return ERROR_AGAIN;

	case EIO:
	case ENOSPC: // TODO this does not seem right for rename()
	case EBUSY:
	case ENOTEMPTY:
		return ERROR_EVFS;

	case EPIPE:
		return ERROR_WRITE;

	case EAFNOSUPPORT:
	case EPROTONOSUPPORT:
	case EPROTOTYPE:
	case EXDEV:
		return ERROR_UNSUPPORTED;

	case EHOSTUNREACH:
	case ENETDOWN:
	case ENETUNREACH:
	case ECONNREFUSED:
	case ECONNRESET:
		return ERROR_NETWORK;

	case EADDRINUSE:
		return ERROR_EXIST;

	case EINPROGRESS:
		return ERROR_PROGRESS;

	default:
		// TODO ENOTDIR
		// TODO EROFS
		// TODO EISDIR
		// TODO ENOTCONN
		// TODO EFBIG EDQUOT ENOSPC EDESTADDRREQ 
		// TODO rmdir() can return EBUSY ENOTEMPTY; consider returning a different error for them
		// TODO rmdir() and rename() may return EEXIST to mean that the directory is not empty on some POSIX systems
		return ERROR;
	}
}

bool readall(int file, char *restrict buffer, size_t total)
{
	size_t index;
	ssize_t size;
	for(index = 0; index < total; index += size)
	{
		size = read(file, buffer + index, total - index);
		if (size <= 0) return false;
	}
	return true;
}

bool writeall(int file, const char *buffer, size_t total)
{
	size_t index;
	ssize_t size;
	for(index = 0; index < total; index += size)
	{
		size = write(file, buffer + index, total - index);
		if (size < 0)
		{
			if (errno == EINTR) continue;
			else if (errno == EAGAIN) continue;
			return false;
		}
	}
	return true;
}

int socket_connect(const char *hostname, unsigned port)
{
	int fd;
	int flags;
	struct addrinfo hints, *result = 0, *item;
	char buffer[sizeof(unsigned) * 3 + 1]; // a byte always fits in 3 decimal digits; one more byte for NUL
	int status = ERROR_RESOLVE;

    res_init(); // ensure we use the most recent version of resolv.conf

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	*format_uint(buffer, port) = 0;
	getaddrinfo(hostname, buffer, &hints, &result);

	// Cycle through the results until the socket is connected successfully
	for(item = result; item; item = item->ai_next)
	{
		fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
		if (fd < 0) continue;

		// Make the socket non-blocking for the connect() operation.
		flags = fcntl(fd, F_GETFL, O_NONBLOCK);
		if ((flags == -1) || (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1))
		{
			close(fd);
			continue;
		}

		// Connect to the server.
		if (!connect(fd, result->ai_addr, result->ai_addrlen)) goto success;
		else if (errno == EINPROGRESS)
		{
			int status;
			struct pollfd pollsock = {.fd = fd, .events = POLLOUT, .revents = 0};

			do status = poll(&pollsock, 1, TIMEOUT);
			while (status < 0);
			if (pollsock.revents & POLLOUT) goto success;
		}

		close(fd);
	}

	freeaddrinfo(result);
	return -1;

success:
	fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
	freeaddrinfo(result);
	return fd;
}

// Try to read total bytes. If stream ends before that, read until the end.
// Returns how much data was read. Returns -1 on timeout or other error.
ssize_t socket_read(int fd, char *restrict buffer, size_t total)
{
	struct pollfd pollsock;
	int status;
	size_t index = 0;
	ssize_t size;

	pollsock.fd = fd;
	pollsock.events = POLLIN;

	do
	{
		pollsock.revents = 0;
retry:
		status = poll(&pollsock, 1, TIMEOUT);
		if (!status) return index;
		else if (status < 0)
		{
			if ((errno == EAGAIN) || (errno == EINTR)) goto retry;
			else return -1;
		}

		if (pollsock.revents & POLLERR)
		{
			errno = ECONNRESET; // TODO: change this error
			return -1;
		}
		if (pollsock.revents & POLLIN)
		{
			size = read(fd, buffer + index, total - index);
			if (size < 0) return -1;
			if (!size) return index;
			index += size;
		}
		if (pollsock.revents & POLLHUP) return index;
	} while (index < total);

	return total;
}

// Tries to write total bytes to a socket. Returns how much was written or -1 on timeout or other error.
ssize_t socket_write(int fd, char *restrict buffer, size_t total)
{
	struct pollfd pollsock;
	int status;
	size_t index = 0;
	ssize_t size;

	pollsock.fd = fd;
	pollsock.events = POLLOUT;

	do
	{
		pollsock.revents = 0;
retry:
		status = poll(&pollsock, 1, TIMEOUT);
		if (!status) return index;
		else if (status < 0)
		{
			if ((errno == EAGAIN) || (errno == EINTR)) goto retry;
			else return -1;
		}

		if (pollsock.revents & POLLERR)
		{
			errno = ECONNRESET; // TODO: change this error
			return -1;
		}
		if (pollsock.revents & POLLOUT)
		{
			size = write(fd, buffer + index, total - index);
			if (size <= 0) return -1;
			index += size;
		}
		if (pollsock.revents & POLLHUP) return index;
	} while (index < total);

	return total;
}
