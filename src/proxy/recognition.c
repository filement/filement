#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "types.h"
#include "log.h"
#include "stream.h"
#include "io.h"
#include "http.h"
#include "proxy.h"

// Recognizes a device and allocates memory to store information about it
// Writes pointer to the allocated memory through a pipe
//  <uuid pointer>
void *recognize_device(void *arg)
{
	int *fifo = arg;
	char *uuid;

	// Get device UUID. If the device doesn't behave as expected, just ignore it.
	uuid = malloc(sizeof(char) * (UUID_LENGTH + 1));
	if (!uuid) fail(1, "Memory error");
	if (socket_read(fifo[0], uuid, UUID_LENGTH) != UUID_LENGTH) goto error;

	size_t index;
	for(index = 0; index < UUID_LENGTH; ++index)
		if (!isxdigit(uuid[index]))
			goto error;

	uuid[UUID_LENGTH] = 0;

	// Send pointer to the allocated memory to the other side of the pipe. Send socket number.
	// Free the allocated memory on error.
	char output[sizeof(uuid) + sizeof(fifo[0])];
	*(char **)output = uuid;
	*(int *)(output + sizeof(uuid)) = fifo[0];
	if (writeall(fifo[1], output, sizeof(output)))
	{
		// Success
		close(fifo[1]);
		free(fifo);
		return 0;
	}

error:

	free(uuid);
	close(fifo[0]);
	close(fifo[1]);
	free(fifo);

	return 0;
}

// Recognizes a client and allocates memory to store information about it
// Writes pointers to the allocated memory through a pipe
//  <uuid pointer> <struct request pointer>
void *recognize_client(void *arg)
{
	int *fifo = arg;

	while (true)
	{
		struct request *request = malloc(sizeof(struct request));
		if (!request) break;

		if (stream_init(&request->stream, fifo[0]))
		{
			free(request);
			break;
		}

		char *uuid = http_uuid(&request->stream);
		if (!uuid)
		{
			stream_term(&request->stream);
			free(request);
			break;
		}

		// Send pointers to the allocated memory to the other side of the pipe
		char output[sizeof(uuid) + sizeof(request)];
		*(char **)output = uuid;
		*(struct request **)(output + sizeof(uuid)) = request;
		writeall(fifo[1], output, sizeof(output));
		goto finally;
	}

	close(fifo[0]);

finally:

	close(fifo[1]);
	free(fifo);

	return 0;
}

#if defined(TLS)
// Recognizes a client and allocates memory to store information about it
// Writes pointers to the allocated memory through a pipe
//  <uuid pointer> <struct request pointer>
void *recognize_client_tls(void *arg)
{
	int *fifo = arg;

	while (true)
	{
		struct request *request = malloc(sizeof(struct request));
		if (!request) break;

		if (!stream_init_tls_accept(&request->stream, fifo[0]))
		{
			free(request);
			break;
		}

		char *uuid = http_uuid(&request->stream);
		if (!uuid)
		{
			stream_term(&request->stream);
			free(request);
			break;
		}

		// Send pointers to the allocated memory to the other side of the pipe
		char output[sizeof(uuid) + sizeof(request)];
		*(char **)output = uuid;
		*(struct request **)(output + sizeof(uuid)) = request;
		writeall(fifo[1], output, sizeof(output));
		goto finally;
	}

	close(fifo[0]);

finally:

	close(fifo[1]);
	free(fifo);

	return 0;
}
#endif
