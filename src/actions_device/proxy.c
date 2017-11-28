#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if !defined(OS_WINDOWS)
# include <poll.h>
# include <sys/socket.h>
#endif

#ifdef OS_WINDOWS
#define _WIN32_WINNT 0x0501
#define WINVER 0x0501
#include <sys/stat.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "mingw.h"
#endif

#include "types.h"
#include "format.h"
#ifdef OS_WINDOWS
# include "../actions.h"
# include "../io.h"
# include "../protocol.h"
# include "../remote.h"
# include "../dlna/ssdp.h"
#else
# include "actions.h"
# include "io.h"
# include "protocol.h"
# include "remote.h"
# include "dlna/ssdp.h"
#endif

#define SERVER 0
#define CLIENT 1

#define STATUS_POSITION (sizeof("HTTP/1.1 ") - 1)

#define RESPONSE_LENGTH_MAX 256
#define RESPONSE_SIZE_MAX 1024

#if !defined(OS_WINDOWS)
extern struct string UUID;
#else
extern struct string UUID_WINDOWS;
#endif

// TODO make sure to work with RST properly

static bool valid_targets(const union json *root)
{
	if (json_type(root) != OBJECT) return false;

	union json *category;

	struct dict_iterator it;
	const struct dict_item *item;
	for(item = dict_first(&it, root->object); item; item = dict_next(&it, root->object))
		if (json_type(item->value) != ARRAY)
			return false;

	return true;
}

// TODO: think about return statuses from this function and how to handle them
static int response_start(struct stream *restrict client, struct stream *restrict cloud)
{
	struct string buffer;

	int status;

	// Start reading the response. Find where the first line ends.
	size_t index = 1;
	buffer.length = 0;
	do
	{
		if (index >= buffer.length)
			if (stream_read(cloud, &buffer, index + 1))
				return -1; // TODO

		if (index == (BUFFER_SIZE_MIN - 1)) return ERROR_GATEWAY; // response line too long
		++index;
	} while ((buffer.data[index - 1] != '\r') || (buffer.data[index] != '\n'));
	buffer.length = index + 1;
	stream_read_flush(cloud, buffer.length);

	// Parse response code.
	if (buffer.length <= STATUS_POSITION) return ERROR_GATEWAY;
	int code = (int)strtol(buffer.data + STATUS_POSITION, 0, 10);
	if ((code < 200) || (599 < code)) return ERROR_GATEWAY;

	// Send the request line and several headers.
	struct string allow_string = string(
		"Access-Control-Allow-Origin: *\r\n"
		"Access-Control-Expose-Headers: Server, UUID\r\n"
		"Access-Control-Allow-Headers: Cache-Control, X-Requested-With, Content-Type, Content-Length, Authorization\r\n"
		"Access-Control-Allow-Methods: GET, POST, OPTIONS, PUT, DELETE\r\n"
	);
	const struct string allow[] = {string("access-control-allow-origin"), string("access-control-allow-headers"), string("access-control-allow-methods")};
	if (stream_write(client, &buffer) || stream_write(client, &allow_string) || stream_write_flush(client)) return -1; // TODO

	// Parse response headers and send the ones that are not sent yet.

	struct dict headers;
	if (status = http_parse_header(&headers, cloud)) return -1; // TODO

	struct dict_iterator it;
	const struct dict_item *item;

	struct string key;
	const struct string *value;
	buffer.length = 0;
	for(item = dict_first(&it, &headers); item; item = dict_next(&it, &headers))
	{
		key = string((char *)item->key_data, item->key_size);
		if (string_equal(&key, allow) || string_equal(&key, allow + 1) || string_equal(&key, allow + 2))
			continue;
		value = item->value;
		buffer.length += item->key_size + sizeof(": ") - 1 + value->length + sizeof("\r\n") - 1;
	}
	buffer.length += sizeof("\r\n") - 1;
	buffer.data = malloc(sizeof(char) * (buffer.length + 1));
	if (!buffer.data)
	{
		dict_term(&headers);
		return ERROR_MEMORY;
	}

	index = 0;
	for(item = dict_first(&it, &headers); item; item = dict_next(&it, &headers))
	{
		key = string((char *)item->key_data, item->key_size);
		if (string_equal(&key, allow) || string_equal(&key, allow + 1) || string_equal(&key, allow + 2))
			continue;

		value = item->value;

		format_bytes(buffer.data + index, item->key_data, item->key_size);
		index += item->key_size;
		buffer.data[index++] = ':';
		buffer.data[index++] = ' ';
		format_bytes(buffer.data + index, value->data, value->length);
		index += value->length;
		buffer.data[index++] = '\r';
		buffer.data[index++] = '\n';
	}

	dict_term(&headers);

	buffer.data[index++] = '\r';
	buffer.data[index++] = '\n';
	buffer.data[index] = 0;

	if (status = stream_write(client, &buffer)) goto finally;
	if (status = stream_write_flush(client)) goto finally;

finally:
	free(buffer.data);
	return status;
}

// TODO: optimize this - there must be no blocking
static int proxy(struct stream *restrict client, struct stream *restrict cloud, struct string *restrict request)
{
	int status;

	// Send request buffer.
	if (status = stream_write(cloud, request)) goto finally;
	if (status = stream_write_flush(cloud)) goto finally;
	free(request);

	// ready stores events that have already arrived
	short ready[2] = {
		[SERVER] = stream_cached(cloud) ? POLLIN : 0,
		[CLIENT] = stream_cached(client) ? POLLIN : 0
	};
	struct pollfd proxyfd[2] = {
		[SERVER] = {.events = (POLLIN ^ ready[SERVER]) | POLLOUT, .fd = cloud->fd},
		[CLIENT] = {.events = (POLLIN ^ ready[CLIENT]) | POLLOUT, .fd = client->fd}
	};

	bool response_started = false;
	struct string buffer;

	// Transfer data until a peer closes its connection or error occures.
	while (true)
	{
		if (poll(proxyfd, 2, TIMEOUT) < 0)
		{
			if (errno == EINTR) continue;
			else break;
		}

		// Check both client and the cloud for newly arrived events.
		if (proxyfd[SERVER].revents)
		{
			if (proxyfd[SERVER].revents & POLLERR) break;
			ready[SERVER] |= proxyfd[SERVER].revents;
			proxyfd[SERVER].revents = 0;
		}
		if (proxyfd[CLIENT].revents)
		{
			if (proxyfd[CLIENT].revents & POLLERR) break;
			ready[CLIENT] |= proxyfd[CLIENT].revents;
			proxyfd[CLIENT].revents = 0;
		}

		// TODO: fix the writes below (they should not block)

		// Do any data transfer that will not block.
		if ((ready[SERVER] & POLLIN) && ((ready[CLIENT] & (POLLOUT | POLLHUP)) == POLLOUT)) // cloud -> client
		{
			if (response_started)
			{
				if (stream_read(cloud, &buffer, 1)) break;
				if (stream_write(client, &buffer) || stream_write_flush(client)) break;
				stream_read_flush(cloud, buffer.length);
			}
			else
			{
				if (status = response_start(client, cloud)) break;
				response_started = true;
			}

			ready[SERVER] &= ~POLLIN;
			ready[CLIENT] &= ~POLLOUT;
		}
		if ((ready[CLIENT] & POLLIN) && ((ready[SERVER] & (POLLOUT | POLLHUP)) == POLLOUT)) // client -> cloud
		{
			if (stream_read(client, &buffer, 1)) break;
			if (stream_write(cloud, &buffer) || stream_write_flush(cloud)) break;
			stream_read_flush(client, buffer.length);

			ready[CLIENT] &= ~POLLIN;
			ready[SERVER] &= ~POLLOUT;
		}

		// No more transfers are possible if one of the ends closed the connection.
		if ((ready[SERVER] | ready[CLIENT]) & POLLHUP) break;

		// Make poll ignore all events marked as ready.
		proxyfd[SERVER].events = (POLLIN | POLLOUT) & ~ready[SERVER];
		proxyfd[CLIENT].events = (POLLIN | POLLOUT) & ~ready[CLIENT];
	}

finally:

	// Data transfer is finished. Shut the connections down.

	stream_term(cloud);
	http_close(cloud->fd);
	stream_term(client);
	http_close(client->fd);
	client->fd = -1;

	return -1; // TODO change this
}

// Generate string from the data read from the client so that it can be sent to a server.
static struct string *request_string(const struct http_request *request, const struct string *restrict URI, struct stream *stream, const struct string *restrict hostname)
{
	static const struct string methods[] = {
		[METHOD_HEAD] = {"HEAD", 4},
		[METHOD_GET] = {"GET", 3},
		[METHOD_POST] = {"POST", 4},
		[METHOD_OPTIONS] = {"OPTIONS", 7},
		[METHOD_PUT] = {"PUT", 3},
		[METHOD_DELETE] = {"DELETE", 6}
	};
	struct string version = string("HTTP/1.1");

	size_t length;

	struct string key;

	struct dict_iterator it;
	const struct dict_item *item;
	const struct string *value;

	struct string connection = string("connection"), close = string("close");
	struct string host = string("host");

	// Generate response string. First determine how much memory is required, then allocate it and then fill it with the data.
	// Headers are iterated two times - once to count their length, another one to store their value in the allocated string.

	// Calculate request string length.
	length = methods[request->method].length + sizeof(" ") - 1 + URI->length + sizeof(" ") - 1 + version.length + sizeof("\r\n") - 1;
	for(item = dict_first(&it, &request->headers); item; item = dict_next(&it, &request->headers))
	{
		key = string((char *)item->key_data, item->key_size);
		if (string_equal(&key, &connection)) continue; // skip connection header
		if (string_equal(&key, &host)) continue; // skip host header

		value = item->value;
		length += item->key_size + sizeof(": ") - 1 + value->length + sizeof("\r\n") - 1;
	}
	length += connection.length + sizeof(": ") - 1 + close.length + sizeof("\r\n") - 1;
	length += host.length + sizeof(": ") - 1 + hostname->length + sizeof("\r\n") - 1;
	length += sizeof("\r\n") - 1;

	struct string *result = malloc(sizeof(struct string) + sizeof(char) * (length + 1));
	if (!result) return 0;
	result->data = (char *)(result + 1);
	result->length = length;

	char *start = result->data;

	// Generate request line.
	start = format_bytes(start, methods[request->method].data, methods[request->method].length);
	*start++ = ' ';
	start = format_bytes(start, URI->data, URI->length);
	*start++ = ' ';
	start = format_bytes(start, version.data, version.length);
	*start++ = '\r';
	*start++ = '\n';

	// Add headers.
	for(item = dict_first(&it, &request->headers); item; item = dict_next(&it, &request->headers))
	{
		key = string((char *)item->key_data, item->key_size);
		if (string_equal(&key, &connection)) continue; // skip connection header
		if (string_equal(&key, &host)) continue; // skip host header

		value = item->value;
		start = format_bytes(start, key.data, key.length);
		*start++ = ':';
		*start++ = ' ';
		start = format_bytes(start, value->data, value->length);
		*start++ = '\r';
		*start++ = '\n';
	}
	struct string header = string("Connection: close\r\n");
	start = format_bytes(start, header.data, header.length);
	start = format_bytes(start, host.data, host.length);
	*start++ = ':';
	*start++ = ' ';
	start = format_bytes(start, hostname->data, hostname->length);
	*start++ = '\r';
	*start++ = '\n';

	// Add headers terminator.
	*start++ = '\r';
	*start++ = '\n';

	*start = 0;
	return result;
}

// TODO not used; is it necessary?
static struct string *proxy_location(const char uuid[UUID_LENGTH], bool tls, unsigned *port)
{
	#define LOCATION_0 "GET /?{\"actions\":{\"location\":{\"device\":[\""
	#define LOCATION_1 "\"]}},\"protocol\":{\"name\":\"n\",\"function\":\"_\",\"request_id\":\"0\"}} HTTP/1.1\r\nHost: " HOST_DISTRIBUTE_HTTP "\r\n\r\n"
	#define REQUEST_LENGTH (sizeof(LOCATION_0) - 1 + UUID_LENGTH + sizeof(LOCATION_1) - 1)

	char request[REQUEST_LENGTH], *start = request;
	start = format_bytes(start, LOCATION_0, sizeof(LOCATION_0) - 1);
	start = format_bytes(start, uuid, UUID_LENGTH);
	format_bytes(start, LOCATION_1, sizeof(LOCATION_1) - 1);

	struct string buffer = string(request, REQUEST_LENGTH);
	struct stream distribute;

	int status;

	short version[2];
	int code;

	// Connect to the distribute server.
	// TODO https
	int sock = socket_connect(HOST_DISTRIBUTE_HTTP, PORT_HTTP);
	if (sock < 0) return 0;
	if (stream_init(&distribute, sock))
	{
		close(sock);
		return 0;
	}

	// Send request.
	if (stream_write(&distribute, &buffer)) goto error;
	if (stream_write_flush(&distribute)) goto error;

	if (http_parse_version(version, &distribute)) goto error;

	// Get HTTP response status code.
	if (stream_read(&distribute, &buffer, sizeof(" ??? \r\n") - 1)) goto error;
	if ((buffer.data[0] != ' ') || !isdigit(buffer.data[1]) || !isdigit(buffer.data[2]) || !isdigit(buffer.data[3]) || (buffer.data[4] != ' '))
		goto error;
	code = strtol(buffer.data + 1, 0, 10); // the check above assures that this is always successful
	if (code != OK)
		goto error;
	stream_read_flush(&distribute, 5);

	// Find the end of the response line.
	size_t index = 1;
	while (1)
	{
		if (stream_read(&distribute, &buffer, index + 1)) goto error;

next:
		if (buffer.data[index] == '\n')
		{
			if (buffer.data[index - 1] == '\r') break;
			else goto error;
		}

		if (index >= RESPONSE_LENGTH_MAX) goto error;

		if (++index < buffer.length) goto next;
	}
	stream_read_flush(&distribute, index + 1);

	struct dict headers;
	struct string key, *value;
	#if !defined(OS_WINDOWS)
	off_t length;
	#else
	int64_t length;
	#endif

	// Get the length of the response string.
	if (http_parse_header(&headers, &distribute)) goto error;
	key = string("content-length");
	value = dict_get(&headers, &key);
	if (!value)
	{
		dict_term(&headers);
		goto error;
	}
	length = strtoll(value->data, 0, 10);
	dict_term(&headers);

	union json *response, *node;

	// Read the response.
	if (!length || (length > RESPONSE_SIZE_MAX)) goto error;
	if (stream_read(&distribute, &buffer, length)) goto error;
	buffer = string(buffer.data + sizeof("_(\"0\",") - 1, length - (sizeof("_(\"0\",") - 1) - (sizeof(");") - 1)); // we need only the JSON
	response = json_parse(&buffer);
	if (!response || (json_type(response) != OBJECT)) goto error;
	stream_read_flush(&distribute, length);

	stream_term(&distribute);
	close(sock);

	// Find device location.
	key = string("device");
	node = dict_get(response->object, &key);
	if (!node || (json_type(node) != OBJECT))
	{
		json_free(response);
		return 0;
	}
	key = string((char *)uuid, UUID_LENGTH);
	node = dict_get(node->object, &key);
	if (!node || (json_type(node) != STRING))
	{
		json_free(response);
		return 0;
	}
	value = &node->string_node;

	// WARNING: strchr() requires that value be NUL-terminated

	// Initialize device hostname and port.
	// value has the following format: "bgproxy.filement.com:80,443"
	char *end = strchr(value->data, ':');
	if (!end)
	{
		json_free(response);
		return 0;
	}
	value = string_alloc(node->string_node.data, end - node->string_node.data);
	if (!value)
	{
		json_free(response);
		return 0;
	}
	if (tls)
	{
		end = strchr(end, ',');
		if (!end)
		{
			free(value);
			json_free(response);
			return 0;
		}
	}
	*port = strtol(end + 1, 0, 10);

	json_free(response);

	// TODO config.info and reachable

	return value;

error:
	stream_term(&distribute);
	close(sock);
	return 0;
}

static int proxy_init(const struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources, const union json *restrict cache, const struct string *restrict category, size_t target)
{
	/*
GET /591ddcf7d109ff6f4d5084e3eaace687/?%7B%22actions%22%3A%7B%22proxy.forward%22%3A%7B%22category%22%3A%22music%22%2C%22id%22%3A0%7D%7D%2C%22protocol%22%3A%7B%22name%22%3A%22remoteJson%22%2C%22function%22%3A%22kernel.sockets.receiveJson%22%2C%22request_id%22%3A%227%22%7D%7D HTTP/1.1
Host: greuhregierg
	*/

	bool tls;
	struct string hostname;
	unsigned port;
	struct string *URI = 0;

	struct string *cache_uuid, *cache_query;

	int status;

	// Find proxy target in the cache.
	union json *entry = dict_get(cache->object, category);
	if (!entry || (target >= entry->array_node.length)) return ERROR_MISSING;
	entry = vector_get(&entry->array_node, target);

	struct string key = string("UUID");
	union json *node = dict_get(entry->object, &key);
	if (node) // proxy to a device
	{
		if ((json_type(node) != STRING) || (node->string_node.length != UUID_LENGTH)) return ERROR_MISSING;
		cache_uuid = &node->string_node;

		key = string("host");
		node = dict_get(entry->object, &key);
		if (!node) return ERROR_GATEWAY;
		hostname = node->string_node;

		key = string("port");
		node = dict_get(entry->object, &key);
		port = node->integer;

		key = string("query");
		node = dict_get(entry->object, &key);
		if (!node || (json_type(node) != STRING)) return ERROR_MISSING;
		cache_query = &node->string_node;

		key = string("auth_id");
		node = dict_get(entry->object, &key);
		if (!node || (json_type(node) != STRING)) return ERROR_MISSING;
		// node->integer;

		tls = false; // TODO change this

		char *start;
		size_t length = 1 + cache_uuid->length + 1 + 1 + cache_query->length;
		URI = malloc(sizeof(*URI) + length + 1);
		if (!URI) return ERROR_MISSING;
		start = URI->data = (char *)(URI + 1);
		URI->length = length;

		*start++ = '/';
		start = format_bytes(start, cache_uuid->data, cache_uuid->length);
		*start++ = '/';
		*start++ = '?';
		*format_bytes(start, cache_query->data, cache_query->length) = 0;
#if !defined(OS_WINDOWS)
		if (string_equal(&UUID, cache_uuid))
#else
		if (string_equal(&UUID_WINDOWS, cache_uuid))
#endif
		{
			struct http_request request_real;

			struct string decoded;
			decoded.data = malloc(cache_query->length + 1);
			if (!decoded.data) goto error;
			decoded.length = url_decode(cache_query->data, decoded.data, cache_query->length);
			request_real.query = json_parse(&decoded);
			free(decoded.data);
			if (!request_real.query) goto error;

			request_real.method = request->method;
			request_real.URI = *URI;
			request_real.version[0] = request->version[0];
			request_real.version[1] = request->version[1];
			request_real.headers = request->headers;
			request_real.hostname = request->hostname;

			if (tls)
			{
				request_real.protocol = PROTOCOL_HTTPS;
				request_real.port = PORT_HTTPS;
			}
			else
			{
				request_real.protocol = PROTOCOL_HTTP;
				request_real.port = PORT_HTTP;
			}

			request_real.path = string(""); // TODO change this

			status = handler_dynamic(&request_real, response, resources);

			free(URI);
			json_free(request_real.query);
			return status;
		}
	}
	else // proxy to external resource
	{
		key = string("URL");
		node = dict_get(entry->object, &key);
		if (!node || (json_type(node) != STRING)) return ERROR_MISSING;

		// WARNING: strchr() requires that the URL be NUL-terminated

		size_t length = node->string_node.length;

		char *host, *path;
		size_t host_length;

		size_t index;

		// Parse URI scheme.
		const struct string http = string("http"), separator = string("://");
		if ((length < (http.length + separator.length)) || memcmp(node->string_node.data, http.data, http.length)) return ERROR_MISSING;
		if (node->string_node.data[http.length] == 's')
		{
			index = http.length + 1;
			tls = true;
		}
		else
		{
			index = http.length;
			tls = false;
		}
		if (((length - index) < separator.length) || memcmp(node->string_node.data + index, separator.data, separator.length)) return ERROR_MISSING;
		index += separator.length;

		host = node->string_node.data + index;
		path = strchr(host, '/');
		if (!path) return ERROR_MISSING;

		// Set port.
		char *sep = strchr(host, ':');
		if (sep)
		{
			char *end;
			port = strtol(sep + 1, &end, 10);
			if (end != path) return ERROR_MISSING;
			host_length = sep - host;
		}
		else
		{
			port = PORT_HTTP;
			host_length = path - host;
		}

		hostname = string(host, host_length);

		URI = string_alloc(path, node->string_node.length - (path - node->string_node.data));
		if (!URI) goto error;
	}

	struct stream cloud;
	struct string *buffer;
	int server;

	// Establish connection with the specified server.
	server = socket_connect(hostname.data, port);
	if (server < 0)
	{
		status = server;
		goto error;
	}
#if defined(FILEMENT_TLS)
	status = (tls ? stream_init_tls_connect(&cloud, server, hostname.data) : stream_init(&cloud, server));
#else
	status = stream_init(&cloud, server);
#endif
	if (status)
	{
		close(server);
		goto error;
	}
	http_open(server);

	buffer = request_string(request, URI, &resources->stream, &hostname);
	free(URI);
	if (!buffer) return ERROR_MEMORY;

	status = proxy(&resources->stream, &cloud, buffer); // proxy() frees buffer

	return 0;

error:
	free(URI);
	return status;
}

int proxy_reset(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	if (!DLNAisEnabled()) return ERROR_AGAIN;

	if (!resources->session_access) return ERROR_ACCESS;

	dlna_reset();

	response->code = OK;
	return 0;
}

int proxy_forward(const struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	// category :: string
	// id :: integer

	if (!DLNAisEnabled()) return ERROR_AGAIN;

	// TODO race conditions: this action could be called from two different places at the same time; proxy_reset can be called while this action is executing

	struct string key;
	union json *item;
	int status;

	struct string *category;

	key = string("category");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != STRING)) return ERROR_MISSING;
	category = &item->string_node;

	key = string("id");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != INTEGER)) return ERROR_MISSING;

#if defined(OS_ANDROID)
	const struct cache *cache;
	char cache_key[CACHE_KEY_SIZE];

	if (status = dlna_key(cache_key)) return status;
	cache = cache_use(cache_key);
	if (!cache) return ERROR_AGAIN; // TODO better solution for this race condition?

	status = proxy_init(request, response, resources, cache->value, category, item->integer);

	cache_finish(cache);
#else
	const struct cache *cache;

	union json *keys = cache_keys(CACHE_PROXY), *cache_key;
	if (!keys) return ERROR_MEMORY;

	// Retrieve proxy parameters from cache.
	// assert(keys->array_node.length == 1);
	cache_key = vector_get(&keys->array_node, 0);
	cache = cache_use(cache_key->string_node.data);
	if (!cache) // TODO better solution for this race condition?
	{
		json_free(keys);
		return ERROR_AGAIN;
	}

	status = proxy_init(request, response, resources, cache->value, category, item->integer);

	cache_finish(cache);

	json_free(keys);
#endif

	// TODO check status

	return ERROR_CANCEL;
}
