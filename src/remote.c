#include <ctype.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#if !defined(OS_WINDOWS)
# include <poll.h>
#endif

#if defined(OS_WINDOWS)
# define _WIN32_WINNT 0x0501
# define WINVER 0x0501
# include <sys/stat.h>
# include <windows.h>
# include <winsock2.h>
# include <ws2tcpip.h>
# include "mingw.h"
#endif

#include "types.h"
#include "buffer.h"
#include "format.h"
#include "stream.h"
#include "json.h"
#include "io.h"
#include "cache.h"
#include "http.h"
#include "http_parse.h"
#include "http_response.h"
#include "protocol.h"
#include "remote.h"

#define RESPONSE_LENGTH_MAX 256

#define BODY_SIZE_BASE 256

#define STORAGE_SIZE_MAX (256 * 1024) /* 256KiB */

#define RESPONSE_SIZE_MAX 1024

#define RETRY 15

//#define EVENT_ON "changeproxy"
//#define EVENT_OFF "noproxy"

//#define SUBSCRIBE_0 "GET /?%7B%22actions%22%3A%20%7B%22subscribe_uuid%22%3A%20%22"
//#define SUBSCRIBE_1 "%22%7D%2C%20%22r%22%3A%20%22v1i4yFezSo%22%7D HTTP/1.1\r\nHost: " HOST_DISTRIBUTE_HTTP "\r\n\r\n"

#define DLNA_REFRESH 60

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
// pthread_mutex_destroy(&mutex);

static time_t dlna_online_update;

#if !defined(OS_WINDOWS)
extern struct string UUID;
#else
extern struct string UUID_WINDOWS;
#endif

extern struct string SECRET;

static int http_parse_response(struct stream *restrict stream)
{
	short version[2];
	unsigned code;
	size_t index;
	struct string buffer;
	int status;

	if (http_parse_version(version, stream)) return ERROR_INPUT;

	// Get HTTP response status code.
	if (status = stream_read(stream, &buffer, sizeof(" ??? \r\n") - 1)) return status;
	if ((buffer.data[0] != ' ') || !isdigit(buffer.data[1]) || !isdigit(buffer.data[2]) || !isdigit(buffer.data[3]) || (buffer.data[4] != ' '))
		return ERROR_INPUT;
	code = strtol(buffer.data + 1, 0, 10); // the check above assures that this is always successful
	if (code != OK) return ERROR_INPUT;
	stream_read_flush(stream, 5);

	// Find the end of the response line.
	index = 1;
	while (1)
	{
		if (status = stream_read(stream, &buffer, index + 1)) return status;

next:
		if (buffer.data[index] == '\n')
		{
			if (buffer.data[index - 1] == '\r') break;
			else return ERROR_INPUT;
		}

		if (index >= RESPONSE_LENGTH_MAX) return ERROR_INPUT;

		if (++index < buffer.length) goto next;
	}
	stream_read_flush(stream, index + 1);

	return 0;
}

static char *response_body(struct stream *restrict stream, size_t *restrict body_length)
{
	short version[2];
	unsigned code;
	struct dict headers;

	struct string buffer;
	size_t index;
	struct string key, *value;
	off_t length;

	char *response;

	if (http_parse_version(version, stream)) return 0;

	// Get HTTP response status code.
	if (stream_read(stream, &buffer, sizeof(" ??? \r\n") - 1)) return 0;
	if ((buffer.data[0] != ' ') || !isdigit(buffer.data[1]) || !isdigit(buffer.data[2]) || !isdigit(buffer.data[3]) || (buffer.data[4] != ' '))
		return 0;
	code = strtol(buffer.data + 1, 0, 10); // the check above assures that this is always successful
	if (code != OK) return 0;
	stream_read_flush(stream, 5);

	// Find the end of the response line.
	index = 1;
	while (1)
	{
		if (stream_read(stream, &buffer, index + 1)) return 0;

next:
		if (buffer.data[index] == '\n')
		{
			if (buffer.data[index - 1] == '\r') break;
			else return 0;
		}

		if (index >= RESPONSE_LENGTH_MAX) return 0;

		if (++index < buffer.length) goto next;
	}
	stream_read_flush(stream, index + 1);

	if (http_parse_header(&headers, stream)) return 0;

	// Check whether the length is known or we have to parse chunks.
	length = content_length(&headers);
	if (length == ERROR_MISSING)
	{
		off_t size_chunk, size_left, size_read;
		size_t size;

		static const char terminator[2] = "\r\n";

		#define CHUNKED "chunked"
		key = string("transfer-encoding");
		value = dict_get(&headers, &key);
		if ((value->length != (sizeof(CHUNKED) - 1)) || memcmp(value->data, CHUNKED, sizeof(CHUNKED) - 1))
		{
			dict_term(&headers);
			return 0;
		}
		dict_term(&headers);
		#undef CHUNKED

		// chunked response

		size = BODY_SIZE_BASE;
		response = malloc(size);
		if (!response) return 0;
		length = 0;

		// Copy each chunk into a single contiguous buffer.
		do
		{
			// Get chunk size.
			if (stream_read(stream, &buffer, sizeof(terminator) + 1)) goto error;
			if (!isxdigit(buffer.data[0])) goto error; // invalid chunk format
			index = 2;
			while (1)
			{
				if (buffer.data[index] == terminator[1])
				{
					if (buffer.data[index - 1] == terminator[0]) break; // chunk size terminator found
					else goto error; // invalid chunk format
				}
				else if (!isxdigit(buffer.data[index - 1])) goto error; // invalid chunk format

				// Abort if chunk size is too big to be file size.
				if (index >= (sizeof(off_t) * 2)) goto error;

				// Read more data if necessary.
				if (++index >= buffer.length)
					if (stream_read(stream, &buffer, buffer.length + 1))
						goto error;
			}

			// Previous checks ensure that chunk size is valid.
			size_chunk = (off_t)strtoll(buffer.data, 0, 16);
			stream_read_flush(stream, index + 1);

			// Adjust response buffer so it can fit the chunk.
			if ((length + size_chunk) > size)
			{
				// make sure size is a power of 2
				char *buffer;
				size = length + size_chunk; // TODO detect type wrapping?
				size_t size_new = 4;
				while (size_new <= size) size_new *= 2; // TODO can this be done faster?
				size = size_new;

				if (size > STORAGE_SIZE_MAX) goto error;
				buffer = realloc(response, size);
				if (!buffer) goto error;
				response = buffer;
			}

			// Copy chunk body.
			size_left = size_chunk;
			while (size_left)
			{
				size_read = ((size_left > BUFFER_SIZE_MAX) ? BUFFER_SIZE_MAX : size_left);
				if (stream_read(stream, &buffer, size_read)) goto error;

				format_bytes(response + length, buffer.data, size_read);
				stream_read_flush(stream, size_read);
				length += size_read;
				size_left -= size_read;
			}

			// Check whether there is a valid chunk terminator.
			if (stream_read(stream, &buffer, sizeof(terminator))) goto error;
			if ((buffer.data[0] != terminator[0]) || (buffer.data[1] != terminator[1])) goto error; // invalid chunk format
			stream_read_flush(stream, sizeof(terminator));
		} while (size_chunk); // break if this was the last chunk
	}
	else
	{
		dict_term(&headers);
		if ((length <= 0) || (STORAGE_SIZE_MAX < length)) return 0;

		response = malloc(length + 1);
		if (!response) return 0;
		if (stream_read(stream, &buffer, length))
		{
			free(response);
			return 0;
		}
		*format_bytes(response, buffer.data, length) = 0;
		stream_read_flush(stream, length);
	}

	*body_length = length;
	return response;

error:
	free(response);
	return 0;
}

union json *storage_json(const char *target, size_t length, char uuid[UUID_SIZE * 2])
{
	int fd;
	struct stream stream;
	struct string request;

	// Generate request string.

	#define REQUEST_0 "GET /private/requests/devices/"
	#define REQUEST_1 "?uuid="
	#define REQUEST_2 "&secret="
	#define REQUEST_3 " HTTP/1.1\r\nHost: "
	#define REQUEST_4 "\r\n\r\n"

	request.length = sizeof(REQUEST_0) - 1 + length + sizeof(REQUEST_1) - 1 + UUID_SIZE * 2 + sizeof(REQUEST_2) - 1 + SECRET.length * 2 + sizeof(REQUEST_3) - 1 + sizeof(HOST_DISTRIBUTE_REMOTE) - 1 + sizeof(REQUEST_4) - 1;
	request.data = malloc(request.length);
	if (!request.data) return 0;

	{
		char *start = request.data;
		start = format_bytes(start, REQUEST_0, sizeof(REQUEST_0) - 1);
		start = format_bytes(start, target, length);
		start = format_bytes(start, REQUEST_1, sizeof(REQUEST_1) - 1);
		start = format_bytes(start, uuid, UUID_SIZE * 2);
		start = format_bytes(start, REQUEST_2, sizeof(REQUEST_2) - 1);
		start = format_hex(start, SECRET.data, SECRET.length);
		start = format_bytes(start, REQUEST_3, sizeof(REQUEST_3) - 1);
		start = format_bytes(start, HOST_DISTRIBUTE_REMOTE, sizeof(HOST_DISTRIBUTE_REMOTE) - 1);
		start = format_bytes(start, REQUEST_4, sizeof(REQUEST_4) - 1);
		request.length = start - request.data;
	}

	#undef REQUEST_4
	#undef REQUEST_3
	#undef REQUEST_2
	#undef REQUEST_1
	#undef REQUEST_0

	// Connect to the remote storage server and fetch the JSON.
	do
	{
#if defined(TLS)
		// TODO change this
		/*fd = socket_connect(HOST_DISTRIBUTE_REMOTE, PORT_HTTPS);
		if (fd < 0) continue;
		if (stream_init_tls_connect(&stream, fd, HOST_DISTRIBUTE_REMOTE))*/

		fd = socket_connect(HOST_DISTRIBUTE_REMOTE, PORT_HTTP);
		if (fd < 0) continue;
		if (stream_init(&stream, fd))
#else
		fd = socket_connect(HOST_DISTRIBUTE_REMOTE, PORT_HTTP);
		if (fd < 0) continue;
		if (stream_init(&stream, fd))
#endif
		{
#if !defined(OS_WINDOWS)
			close(fd);
#else
			CLOSE(fd);
#endif
			continue;
		}

		if (!stream_write(&stream, &request) && !stream_write_flush(&stream))
		{
			// Get storage json.
			struct string buffer;
			buffer.data = response_body(&stream, &buffer.length);
			if (buffer.data)
			{
				union json *json_storage = json_parse(&buffer);
				free(buffer.data);
				if (json_storage)
				{
					stream_term(&stream);
					#if !defined(OS_WINDOWS)
					close(fd);
					#else
					CLOSE(fd);
					#endif
					return json_storage; // success
				}
			}
		}

		stream_term(&stream);
#if !defined(OS_WINDOWS)
		close(fd);
#else
		CLOSE(fd);
#endif
	} while (sleep(RETRY), 1);
}

static bool valid_targets(const union json *root)
{
	if (json_type(root) != OBJECT) return false;

	union json *category;
	size_t index;

	struct dict_iterator it;
	const struct dict_item *item;
	for(item = dict_first(&it, root->object); item; item = dict_next(&it, root->object))
	{
		category = item->value;
		if (json_type(category) != ARRAY) return false;
		for(index = 0; index < category->array_node.length; ++index)
			if (json_type(vector_get(&category->array_node, index)) != OBJECT)
				return false;
	}

	return true;
}

/*bool storage_cache(void)
{
	union json *storage = storage_json();
	if (!storage) return false;

	if (!valid_targets(storage))
	{
		json_free(storage);
		return false;
	}

	size_t count;
	free(remote_locations(storage, false, &count)); // TODO error check

	char cache_key[CACHE_KEY_SIZE];
	if (!cache_create(cache_key, CACHE_PROXY, storage, 0))
	{
		json_free(storage);
		return false;
	}

	return true;
}*/

static int device_on(union json *restrict entry, union json *restrict location, bool tls)
{
	struct string key, *address = &location->string_node;
	union json *value;

	// Initialize device hostname and port.
	// value has the following format: "bgproxy.filement.com:80,443"
	// WARNING: strchr() requires that value be NUL-terminated

	char *end = strchr(address->data, ':');
	if (!end) return ERROR_INPUT;
	key = string("host");
	value = json_string(address->data, end - address->data);
	if (!value) return ERROR_MEMORY;
	if (dict_add(entry->object, &key, value))
	{
		json_free(value);
		return ERROR_MEMORY;
	}

	if (tls)
	{
		end = strchr(end, ',');
		if (!end)
		{
			json_free(dict_remove(entry->object, &key));
			return ERROR_INPUT;
		}
	}
	value = json_integer(strtol(end + 1, 0, 10));
	if (!value)
	{
		json_free(dict_remove(entry->object, &key));
		return ERROR_MEMORY;
	}
	key = string("port");
	if (dict_add(entry->object, &key, value))
	{
		json_free(value);
		key = string("host");
		json_free(dict_remove(entry->object, &key));
		return ERROR_MEMORY;
	}

	return 0;
}

static union json *distribute_request_dynamic(const struct string *buffer, bool tls)
{
	struct stream distribute;

	union json *response;
	struct string json;

	// Connect to the distribute server.
	int sock;
	int status;
	if (tls)
	{
		// TODO implement this
	}
	else
	{
		sock = socket_connect(HOST_DISTRIBUTE_HTTP, PORT_HTTP);
		if (sock < 0) return 0;
		status = stream_init(&distribute, sock);
	}
	if (status)
	{
#if !defined(OS_WINDOWS)
		close(sock);
#else
		CLOSE(sock);
#endif
		return 0;
	}

	// Send request.
	if (stream_write(&distribute, buffer)) goto error;
	if (stream_write_flush(&distribute)) goto error;

	struct dict headers;
	if (http_parse_response(&distribute)) goto error;
	if (http_parse_header(&headers, &distribute)) goto error;

#if !defined(OS_WINDOWS)
	off_t length;
#else
	int64_t length;
#endif
	length = content_length(&headers);
	dict_term(&headers);
	if (length < 0) goto error;

	// Read the response.
	if (!length || (length > RESPONSE_SIZE_MAX)) goto error;
	if (stream_read(&distribute, &json, length)) goto error;
	json = string(json.data + sizeof("_(\"0\",") - 1, length - (sizeof("_(\"0\",") - 1) - (sizeof(");") - 1)); // we need only the JSON
	response = json_parse(&json);
	if (!response || (json_type(response) != OBJECT)) goto error;
	stream_read_flush(&distribute, length);

	stream_term(&distribute);
#if !defined(OS_WINDOWS)
	close(sock);
#else
	CLOSE(sock);
#endif

	return response;

error:
	stream_term(&distribute);
#if !defined(OS_WINDOWS)
	close(sock);
#else
	CLOSE(sock);
#endif
	return 0;
}

static void free_device(void *device)
{
	if (device)
	{
		vector_term(device);
		free(device);
	}
}
// Sets remote host location in storage for each device that is online.
int remote_locations(union json *restrict storage, bool tls)
{
	#define LOCATION_0 "GET /?{\"actions\":{\"location\":{\"device\":"
	#define LOCATION_1 "]}},\"protocol\":{\"name\":\"n\",\"function\":\"_\",\"request_id\":\"0\"}} HTTP/1.1\r\nHost: " HOST_DISTRIBUTE_HTTP "\r\n\r\n"

	// An array of UUIDs will be inserted between the two strings above when writing them to the buffer request.
	// The dictionary devices will store an array of the storage entries corresponding to a given UUID.

	struct string key;
	union json *value;

	struct vector *shares;
	struct dict devices;
	if (!dict_init(&devices, DICT_SIZE_BASE)) return ERROR_MEMORY;

	struct buffer request = {.data = 0};
	if (!buffer_adjust(&request, sizeof(LOCATION_0) - 1 + sizeof(LOCATION_1) - 1))
	{
		dict_term_custom(&devices, free_device);
		return ERROR_MEMORY;
	}
	request.length = sizeof(LOCATION_0) - 1;

	union json *category, *entry, *node;
	size_t index;

	struct dict_iterator it;
	const struct dict_item *item;

	for(item = dict_first(&it, storage->object); item; item = dict_next(&it, storage->object))
	{
		category = item->value;
		for(index = 0; index < category->array_node.length; ++index)
		{
			entry = vector_get(&category->array_node, index);

			key = string("UUID");
			node = dict_get(entry->object, &key);
			if (node && (json_type(node) == STRING))
			{
				// Assume the local device is always online and skip it.
#if !defined(OS_WINDOWS)
				if ((node->string_node.length == (UUID_SIZE * 2)) && !memcmp(node->string_node.data, UUID.data, UUID_SIZE * 2))
#else
				if ((node->string_node.length == (UUID_SIZE * 2)) && !memcmp(node->string_node.data, UUID_WINDOWS.data, UUID_SIZE * 2))
#endif
				{
					// TODO write this better

					key = string("host");
					if (dict_get(entry->object, &key)) continue; // only add host and port once
					value = json_string("127.0.0.1", sizeof("127.0.0.1") - 1);
					dict_add(entry->object, &key, value);

					key = string("port");
					value = json_integer(PORT_HTTP_MIN);
					dict_add(entry->object, &key, value);

					continue;
				}

				// Remove old location data.
				key = string("host");
				value = dict_remove(entry->object, &key);
				if (value)
				{
					json_free(value);
					key = string("port");
					free(dict_remove(entry->object, &key));
				}

				shares = dict_get(&devices, &node->string_node);
				if (shares)
				{
					if (!vector_add(shares, entry))
					{
						free(request.data);
						dict_term_custom(&devices, free_device);
						return ERROR_MEMORY;
					}
				}
				else
				{
					shares = malloc(sizeof(*shares));
					if (!shares)
					{
						free(request.data);
						dict_term_custom(&devices, free_device);
						return ERROR_MEMORY;
					}
					if (!vector_init(shares, VECTOR_SIZE_BASE))
					{
						free(shares);
						free(request.data);
						dict_term_custom(&devices, free_device);
						return ERROR_MEMORY;
					}
					if (!vector_add(shares, entry) || dict_add(&devices, &node->string_node, shares))
					{
						vector_term(shares);
						free(shares);
						free(request.data);
						dict_term_custom(&devices, free_device);
						return ERROR_MEMORY;
					}

					// Add current UUID to the request string.
					if (!buffer_adjust(&request, request._size + 1 + (1 + node->string_node.length + 1)))
					{
						free(request.data);
						dict_term_custom(&devices, free_device);
						return ERROR_MEMORY;
					}
					request.data[request.length++] = ((devices.count > 1) ? ',' : '[');
					request.data[request.length++] = '"';
					format_bytes(request.data + request.length, node->string_node.data, node->string_node.length);
					request.length += node->string_node.length;
					request.data[request.length++] = '"';
				}
			}
		}
	}

	if (!devices.count)
	{
		free(request.data);
		dict_term_custom(&devices, free_device);
		return 0; // nothing to do
	}

	format_bytes(request.data, LOCATION_0, sizeof(LOCATION_0) - 1);
	format_bytes(request.data + request.length, LOCATION_1, sizeof(LOCATION_1) - 1);
	request.length += sizeof(LOCATION_1) - 1;

	#undef LOCATION_0
	#undef LOCATION_1

	// Fetch location data from the distribute server.
	struct string buffer = string(request.data, request.length);
	union json *response = distribute_request_dynamic(&buffer, tls);
	free(request.data);
	if (!response || (json_type(response) != OBJECT)) goto error;

	// Set the location of each of the devices.
	key = string("device");
	node = dict_get(response->object, &key);
	if (!node || (json_type(node) != OBJECT)) goto error;
	for(item = dict_first(&it, node->object); item; item = dict_next(&it, node->object))
	{
		// Get device UUID.
		key = string((char *)item->key_data, item->key_size); // TODO fix this cast

		shares = dict_get(&devices, &key);
		for(index = 0; index < shares->length; ++index)
		{
			entry = vector_get(shares, index);
			if (json_type(item->value) != STRING) goto error;
			if (device_on(entry, item->value, tls)) goto error;
		}
	}

	json_free(response);

	// Generate and return array of all UUIDs.
	{
		/*device_uuid_t *uuids = malloc(devices.count * sizeof(*uuids));
		if (!uuids) goto error; // memory error

		index = 0;
		for(item = dict_first(&it, &devices); item; item = dict_next(&it, &devices))
			format_bytes(uuids[index++], item->key_data, item->key_size);
		*count = devices.count;*/

		dict_term_custom(&devices, free_device);
		//return uuids;
	}

	return 0;

error:
	json_free(response);
	dict_term_custom(&devices, free_device);
#if !defined(OS_WINDOWS)
	return ERROR; // TODO
#else
	return -32767;
#endif
}

/*static off_t http_chunked(struct stream *restrict input, struct buffer *restrict output)
{
	static const char terminator[2] = "\r\n";

	struct string buffer;
	size_t index;

	off_t size_total = 0, size_chunk, size_left, size;

	// Copy each chunk.
	do
	{
		// Get chunk size.
		if (stream_read(input, &buffer, sizeof(terminator) + 1)) return -1; // TODO: error
		if (!isxdigit(buffer.data[0])) return -1; // invalid chunk format
		index = 2;
		while (1)
		{
			if (buffer.data[index] == terminator[1])
			{
				if (buffer.data[index - 1] == terminator[0]) break; // chunk size terminator found
				else return -1; // invalid chunk format
			}
			else if (!isxdigit(buffer.data[index - 1])) return -1; // invalid chunk format

			// Abort if chunk size is too big to be file size.
			if (index >= (sizeof(off_t) * 2)) return -1; // TODO: error

			// Read more data if necessary.
			if (++index >= buffer.length)
				if (stream_read(input, &buffer, buffer.length + 1))
					return -1; // TODO: set this
		}

		// Previous checks ensure that chunk size is valid.
		size_chunk = (off_t)strtol(buffer.data, 0, 16);
		stream_read_flush(input, index + 1);

		// Write chunk body.
		size_left = size_chunk;
		while (size_left)
		{
			size = ((size_left > BUFFER_SIZE_MAX) ? BUFFER_SIZE_MAX : size_left);
			if (stream_read(input, &buffer, size)) return -1; // TODO: set this

			if (!buffer_adjust(output, output->length + size)) return -1; // TODO
			format_bytes(output->data + output->length, buffer.data, size);
			output->length += size;

			stream_read_flush(input, size);
			size_left -= size;
		}
		size_total += size_chunk;

		// Check whether there is a valid chunk terminator.
		if (stream_read(input, &buffer, sizeof(terminator))) return -1; // TODO: error
		if ((buffer.data[0] != terminator[0]) || (buffer.data[1] != terminator[1])) return -1; // invalid chunk format
		stream_read_flush(input, sizeof(terminator));
	} while (size_chunk); // break if this was the last chunk

	return size_total;
}*/

// WARNING: This function is not reentrant.
// Subscribes for the given uuid. May block indefinitely.
/*static void subscribe(struct pollfd *restrict wait, struct stream *restrict stream, const char *restrict uuid)
{
	static char message[] = SUBSCRIBE_0 "00000000" "00000000" "00000000" "00000000" SUBSCRIBE_1;
	struct string request = string(message);
	struct dict headers;

	// Set subscription UUID.
	format_bytes(message + sizeof(SUBSCRIBE_0) - 1, uuid, UUID_SIZE * 2);

	do
	{
		// Connect to the event server.
		wait->fd = socket_connect(HOST_DISTRIBUTE_HTTP, PORT_DISTRIBUTE_HTTP);
		if (wait->fd < 0) continue;
		if (stream_init(stream, wait->fd))
		{
#if !defined(OS_WINDOWS)
			close(wait->fd);
#else
			CLOSE(wait->fd);
#endif
			continue;
		}
		wait->events = POLLIN;
		wait->revents = 0;

		// Write subscription request to the event server.
		if (stream_write(stream, &request) || stream_write_flush(stream))
			goto error;

		printf("%*s\n", (int)request.length, request.data);

		if (http_parse_response(stream)) goto error;
		if (http_parse_header(&headers, stream)) goto error;
		dict_term(&headers);

		break;

error:
		stream_term(stream);
#if !defined(OS_WINDOWS)
		close(wait->fd);
#else
		CLOSE(wait->fd);
#endif
	} while (sleep(RETRY), 1);
}*/

int dlna_init(void)
{
	// Fetch proxy cache from the remote server.
#if !defined(OS_WINDOWS)
	union json *storage = storage_json(REMOTE_DLNA, sizeof(REMOTE_DLNA) - 1, UUID.data);
#else
	union json *storage = storage_json(REMOTE_DLNA, sizeof(REMOTE_DLNA) - 1, UUID_WINDOWS.data);
#endif
	if (!storage) return ERROR_MEMORY;
	if (!valid_targets(storage))
	{
		json_free(storage);
		return ERROR_INPUT;
	}

	// Create proxy cache entry.
	char cache_key[CACHE_KEY_SIZE];
	if (!cache_create(cache_key, CACHE_PROXY, storage, 0))
	{
		json_free(storage);
		return ERROR_MEMORY;
	}

	return 0;
}

int dlna_reset(void)
{
	// Find and remove the old cache data.
	union json *keys = cache_keys(CACHE_PROXY), *key;
	if (!keys) return ERROR_MEMORY;
	// assert(keys->array_node.length == 1);
	key = vector_get(&keys->array_node, 0);
	struct cache *cache = cache_load(key->string_node.data);
	if (!cache) return ERROR_MEMORY; // TODO
	json_free(cache->value);

	// Fetch the new proxy cache from the remote server.
#if !defined(OS_WINDOWS)
	cache->value = storage_json(REMOTE_DLNA, sizeof(REMOTE_DLNA) - 1, UUID.data);
#else
	cache->value = storage_json(REMOTE_DLNA, sizeof(REMOTE_DLNA) - 1, UUID_WINDOWS.data);
#endif
	if (!cache->value)
	{
		cache_discard(key->string_node.data, cache);
		return ERROR_MEMORY;
	}
	if (!valid_targets(cache->value))
	{
		cache_discard(key->string_node.data, cache);
		return ERROR_INPUT;
	}

	cache_save(key->string_node.data, cache);
	json_free(keys);

	pthread_mutex_lock(&mutex);
	dlna_online_update = 0;
	pthread_mutex_unlock(&mutex);
	return 0;
}

int dlna_key(char key[CACHE_KEY_SIZE])
{
	// Find cache key.
	union json *keys = cache_keys(CACHE_PROXY);
	if (!keys) return ERROR_MEMORY;
	// assert(keys->array_node.length == 1);
	format_bytes(key, ((union json *)vector_get(&keys->array_node, 0))->string_node.data, CACHE_KEY_SIZE);
	json_free(keys);

	// Refresh which devices are online if DLNA_REFRESH seconds have passed since the last refresh.
	time_t now = time(0);
	pthread_mutex_lock(&mutex);
	if ((now - dlna_online_update) >= DLNA_REFRESH)
	{
		int status;
		struct cache *cache = cache_load(key);
#if !defined(OS_WINDOWS)
		if (!cache) return ERROR; // TODO
#else
		if (!cache)return -32767;
#endif
		if (status = remote_locations(cache->value, false))
		{
			cache_discard(key, cache);
			return status;
		}

		cache_save(key, cache);
		dlna_online_update = now;
	}
	pthread_mutex_unlock(&mutex);

	return 0;
}

void dlna_term(void)
{
	// Find and remove the cache data.
	union json *keys = cache_keys(CACHE_PROXY), *key;
	if (!keys) return ;
	// assert(keys->array_node.length == 1);
	key = vector_get(&keys->array_node, 0);
	cache_destroy(key->string_node.data);
	json_free(keys);
}

/*void *main_event(void *argument)
{
	int *ready = argument; // TODO remove this

	device_uuid_t *uuids;
	size_t current, uuids_count;

	// Generate proxy data.
	union json *storage = storage_json();
	if (!storage) return 0; // TODO
	if (!valid_targets(storage))
	{
		json_free(storage);
		return 0; // TODO
	}

	uuids = remote_locations(storage, false, &uuids_count); // TODO error check
	/ *if (!uuids)
	{
		write(*ready, "", 1); // TODO remove this
		return 0; // TODO this could be either error or uuids_count == 0
	}* /

	// Create proxy cache.
	char cache_key[CACHE_KEY_SIZE];
	if (!cache_create(cache_key, CACHE_PROXY, storage, 0))
	{
		free(uuids);
		json_free(storage);
		return 0; // TODO
	}
	int status;
	struct buffer event = {.data = 0};

	// TODO remove this
	if (!uuids)
	{
		write(*ready, "", 1); // TODO remove this
		return 0; // TODO this could be either error or uuids_count == 0
	}

	struct string pattern_action = string("\"action\":\""), pattern_uuid = string("\"device_id\":\""), pattern_host = string("\"proxy\":\"");
	size_t *table_action = kmp_table(&pattern_action), *table_uuid = kmp_table(&pattern_uuid), *table_host = kmp_table(&pattern_host);
	if (!table_action || !table_uuid || !table_host)
	{
		free(table_action);
		free(table_uuid);
		free(table_host);
		return 0; // TODO think of something better to do here
	}
	ssize_t start;
	char *uuid, *end;
	struct cache *cache;
	union json *category, *entry, *node;
	struct dict_iterator it;
	const struct dict_item *item;
	size_t index;
	struct string key;

	struct dict headers;
	struct pollfd *wait = malloc(uuids_count * sizeof(*wait));
	struct stream *stream = malloc(uuids_count * sizeof(*stream));
	if (!wait || !stream)
	{
		free(wait);
		free(stream);

		free(table_action);
		free(table_uuid);
		free(table_host);

		return 0; // TODO
	}

	// Subscribe for the events we are interested in.
	// TODO this could block; how to fix it?
	for(current = 0; current < uuids_count; ++current)
	{
		subscribe(wait + current, stream + current, uuids[current]);

//#if !defined(OS_WINDOWS)
		// TODO remove this partial hacky race condition fix

		if (ready)
		{
			write(*ready, "", 1);
			ready = 0;
		}
//#endif
	}
	// Wait for response from the event server.
	while (1)
	{
		status = poll(wait, uuids_count, -1);
		if (status < 0)
		{
			continue;
		}

		for(current = 0; current < uuids_count; ++current)
		{
			event.length = 0;
			while ((wait[current].revents & POLLIN) && (http_chunked(stream + current, &event) >= 0))
			{
				// WARNING: The cast below relies on the layout similarities between struct buffer and struct string.
				start = kmp_search(&pattern_action, table_action, (struct string *)&event);
				if (start < 0) break;
				start += pattern_action.length;
				if ((((ssize_t)event.length - start) >= (sizeof(EVENT_ON) - 1)) && !memcmp(event.data + start, EVENT_ON, sizeof(EVENT_ON) - 1)) status = 1;
				else if ((((ssize_t)event.length - start) >= (sizeof(EVENT_OFF) - 1)) && !memcmp(event.data + start, EVENT_OFF, sizeof(EVENT_OFF) - 1)) status = 0;
				else break;
				
				// WARNING: The cast below relies on the layout similarities between struct buffer and struct string.
				start = kmp_search(&pattern_uuid, table_uuid, (struct string *)&event);
				if (start < 0) break;
				start += pattern_uuid.length;
				if ((start + UUID_SIZE * 2) >= event.length) break;
				uuid = event.data + start;

				// Find device location data if the device is online.
				if (status)
				{
					// WARNING: The cast below relies on the layout similarities between struct buffer and struct string.
					start = kmp_search(&pattern_host, table_host, (struct string *)&event);
					if (start < 0) break;
					start += pattern_host.length;
					if (start >= event.length) break;
					// host data starts at (event.data + start)
				}

				// TODO remove this
				if (status)
				{
					end = memchr(event.data + start, '"', event.length - start);
					printf("+ %.*s %.*s\n", UUID_SIZE * 2, uuid, (int)(event.length - start), event.data + start);
				}
				else printf("- %.*s\n", UUID_SIZE * 2, uuid);

				// Load proxy cache and update device location.
				cache = cache_load(cache_key);
				// assert(cache);
				for(item = dict_first(&it, cache->value->object); item; item = dict_next(&it, cache->value->object))
				{
					category = item->value;
					for(index = 0; index < category->array_node.length; ++index)
					{
						entry = vector_get(&category->array_node, index);
						key = string("UUID");
						node = dict_get(entry->object, &key);
						if (node && (json_type(node) == STRING))
						{
							if ((node->string_node.length == (UUID_SIZE * 2)) && !memcmp(node->string_node.data, uuid, UUID_SIZE * 2))
							{
								// Remove old location data.
								key = string("host");
								json_free(dict_remove(entry->object, &key));
								key = string("port");
								json_free(dict_remove(entry->object, &key));

								// Add new device location if the device is online.
								if (status)
								{
									end = memchr(event.data + start, '"', event.length - start);
									if (node = json_string(event.data + start, event.length - start))
									{
										device_on(entry, node, false); // TODO error check?
										json_free(node);
									}
								}
							}
						}
					}
				}

				cache_save(cache_key, cache);

				break;
			}

			// Possible events are POLLIN, POLLHUP and POLLERR. In all cases, close the socket and reconnect.
			if (wait[current].revents)
			{
				stream_term(stream + current);
#if !defined(OS_WINDOWS)
				close(wait[current].fd);
#else
				CLOSE(wait[current].fd);
#endif
				subscribe(wait + current, stream + current, uuids[current]);
			}
		}
	}

	/ *for(current = 0; current < uuids_count; ++current)
	{
		stream_term(stream + current);
		close(wait[current].fd);
	}* /

	// free(stream);
	// free(wait);

	// free(table_host);
	// free(table_uuid);
	// free(table_action);

	// free(event.data);

	// return 0;
}*/
