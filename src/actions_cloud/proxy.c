#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "types.h"
#include "actions.h"
#include "io.h"

#define SERVER 0
#define CLIENT 1

#define SECRET_KEY "{secret_key}"
#define SECRET_VALUE "fi8wxqdqpaljkjq"

#define CONSUMER_KEY "{consumer_key}"
#define CONSUMER_VALUE "aob7jflrm7e4kub"

#define STATUS_POSITION (sizeof("HTTP/1.1 ") - 1)

#include <stdio.h>

// TODO: send 502 BadGateway when appropriate

// WARNING: The total length of the keys must be at most equal to the total length of the values. Otherwise malicious clients can crash the server.
static const struct string secret_key = {.data = SECRET_KEY, .length = sizeof(SECRET_KEY) - 1};
static const struct string secret_value = {.data = SECRET_VALUE, .length = sizeof(SECRET_VALUE) - 1};
static const struct string consumer_key = {.data = CONSUMER_KEY, .length = sizeof(CONSUMER_KEY) - 1};
static const struct string consumer_value = {.data = CONSUMER_VALUE, .length = sizeof(CONSUMER_VALUE) - 1};

// TODO: think about return statuses from this function and how to handle them
static int response_start(struct stream *restrict client, struct stream *restrict cloud)
{
	struct string buffer;

	// Start reading the response. Find where the first line ends.
	size_t index = 1;
	buffer.length = 0;
	do
	{
		if (index >= buffer.length)
			if (stream_read(cloud, &buffer, index + 1))
				return NotFound;

		if (index == (BUFFER_SIZE_MIN - 1)) return NotFound; // response line too long
		++index;
	} while ((buffer.data[index - 1] != '\r') || (buffer.data[index] != '\n'));
	buffer.length = index + 1;
	stream_read_flush(cloud, buffer.length);

	// Parse response code.
	if (buffer.length <= STATUS_POSITION) return NotFound;
	int status = (int)strtol(buffer.data + STATUS_POSITION, 0, 10);
	if ((status < 200) || (599 < status)) return NotFound;

	// Send the request line and several headers.
	struct string allow_string = string(
		"Access-Control-Allow-Origin: *\r\n"
		"Access-Control-Expose-Headers: Server, UUID\r\n"
		"Access-Control-Allow-Headers: Cache-Control, X-Requested-With, Content-Type, Content-Length, Authorization\r\n"
		"Access-Control-Allow-Methods: GET, POST, OPTIONS, PUT, DELETE\r\n"
	);
	const struct string allow[] = {string("access-control-allow-origin"), string("access-control-allow-headers"), string("access-control-allow-methods")};
	if (stream_write(client, &buffer) || stream_write(client, &allow_string) || stream_write_flush(client)) return -1;
	//printf("<<<<\n%.s%s\n<<<<\n", buffer.length, buffer.data, allow_string.data);

	// Parse response headers and send the one that are not sent yet.

	bool success;
	struct dict headers;
	success = !http_parse_header(&headers, cloud);
	if (!success)
	{
		// TODO: maybe check return code from http_parse_header
		return NotFound;
	}

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
		return InternalServerError; // TODO: change this
	}

	index = 0;
	for(item = dict_first(&it, &headers); item; item = dict_next(&it, &headers))
	{
		key = string((char *)item->key_data, item->key_size);
		if (string_equal(&key, allow) || string_equal(&key, allow + 1) || string_equal(&key, allow + 2))
			continue;

		value = item->value;

		memcpy(buffer.data + index, item->key_data, item->key_size);
		index += item->key_size;
		buffer.data[index++] = ':';
		buffer.data[index++] = ' ';
		memcpy(buffer.data + index, value->data, value->length);
		index += value->length;
		buffer.data[index++] = '\r';
		buffer.data[index++] = '\n';
	}

	buffer.data[index++] = '\r';
	buffer.data[index++] = '\n';
	buffer.data[index] = 0;

	dict_term(&headers);

	//printf("<<<<\n%s\n<<<<\n", buffer.data);
	success = (!stream_write(client, &buffer) && !stream_write_flush(client));
	free(buffer.data);

	return (success ? status : -1);
}

// TODO: ? optimize this
static int proxy(struct stream *restrict client, struct stream *restrict cloud, struct string *restrict request)
{
	// Send the data already read.
	bool success = !stream_write(cloud, request) && !stream_write_flush(cloud);
	printf(">>>>\n%s\n>>>>\n", request->data);
	free(request);
	if (!success) goto finally;

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

		// Do any data transfer that will not block.
		// TODO: fix the writes below
		if ((ready[SERVER] & POLLIN) && ((ready[CLIENT] & (POLLOUT | POLLHUP)) == POLLOUT)) // cloud -> client
		{
			if (response_started)
			{
				if (stream_read(cloud, &buffer, 1)) break;
				if (stream_write(client, &buffer) || stream_write_flush(client)) break;
				printf("<<<<\n%.s\n<<<<\n", buffer.length, buffer.data);
				stream_read_flush(cloud, buffer.length);
			}
			else
			{
				response_start(client, cloud); // TODO: check for errors
				response_started = true;
			}

			ready[SERVER] &= ~POLLIN;
			ready[CLIENT] &= ~POLLOUT;
		}
		if ((ready[CLIENT] & POLLIN) && ((ready[SERVER] & (POLLOUT | POLLHUP)) == POLLOUT)) // client -> cloud
		{
			if (stream_read(client, &buffer, 1)) break;
			if (stream_write(cloud, &buffer) || stream_write_flush(cloud)) break;
			printf(">>>>\n%.s\n>>>>\n", buffer.length, buffer.data);
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

	return -1;
}

// Generates authorization header in header from template.
// Returns how many bytes were written (0 == invalid template; -1 == memory error).
static ssize_t authorize(char *restrict dest, const struct string *template)
{
	size_t *secret_table, *consumer_table;
	secret_table = kmp_table(&secret_key);
	if (!secret_table) return -1;
	consumer_table = kmp_table(&consumer_key);
	if (!consumer_table)
	{
		free(secret_table);
		return -1;
	}

	ssize_t secret_position = kmp_search(&secret_key, secret_table, template);
	ssize_t consumer_position = kmp_search(&consumer_key, consumer_table, template);

	free(secret_table);
	free(consumer_table);

	if ((secret_position < 0) || (consumer_position < 0)) return 0;

	size_t index = 0, position;

	#define APPEND(start, length) do \
		{ \
			memcpy(dest + index, (start), (length)); \
			index += (length); \
		} while (false)

	if (secret_position < consumer_position)
	{
		APPEND(template->data, secret_position);
		APPEND(secret_value.data, secret_value.length);
		position = secret_position + secret_key.length;
		APPEND(template->data + position, consumer_position - position);
		APPEND(consumer_value.data, consumer_value.length);
		position = consumer_position + consumer_key.length;
		APPEND(template->data + position, template->length - position);
	}
	else
	{
		APPEND(template->data, consumer_position);
		APPEND(consumer_value.data, consumer_value.length);
		position = consumer_position + consumer_key.length;
		APPEND(template->data + position, secret_position - position);
		APPEND(secret_value.data, secret_value.length);
		position = secret_position + secret_key.length;
		APPEND(template->data + position, template->length - position);
	}

	#undef APPEND

	return index;
}

static ssize_t response_string_header(char *restrict buffer, const struct string *key, const struct string *value, bool authorized)
{
	size_t index = 0;
	struct string authorization = string("authorization");

	memcpy(buffer + index, key->data, key->length);
	index += key->length;

	buffer[index++] = ':';
	buffer[index++] = ' ';

	if (authorized && string_equal(key, &authorization))
	{
		ssize_t length = authorize(buffer + index, value);
		if (length < 0) return -1; // memory error
		else if (!length) return 0; // invalid header
		index += length;
	}
	else
	{
		memcpy(buffer + index, value->data, value->length);
		index += value->length;
	}

	buffer[index++] = '\r';
	buffer[index++] = '\n';

	return index;
}

static struct string *string_lower(const struct dict_item *restrict item)
{
	struct string *result = string_alloc(item->key_data, item->key_size);
	size_t index;
	for(index = 0; index < item->key_size; ++index)
		result->data[index] = tolower(item->key_data[index]);
	return result;
}

// Generate string from the data read from the client so that it can be sent to a server.
static struct string *request_string(const struct http_request *request, const struct string *URI, struct stream *restrict stream, const struct dict *headers, bool authorized)
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

	size_t length, cached = stream_cached(stream);

	struct dict_iterator it;
	const struct dict_item *item;
	struct string key, *lower;
	const struct string *value;

	struct string connection = string("connection"), close = string("close");

	// Generate response string. First determine how much memory is required, then allocate it and then fill it with the data.
	// Headers are iterated two times - once to count their length, another one to store their value in the allocated string.

	struct string *encoded = uri_encode(URI->data, URI->length);
	if (!encoded) return 0;

	// Calculate request string length.
	length = methods[request->method].length + sizeof(" ") - 1 + encoded->length + sizeof(" ") - 1 + version.length + sizeof("\r\n") - 1;
	for(item = dict_first(&it, &request->headers); item; item = dict_next(&it, &request->headers))
	{
		key = string((char *)item->key_data, item->key_size);
		if (string_equal(&key, &connection)) continue; // skip connection header
		if (dict_get(headers, &key)) continue; // ignore overwritten headers
		value = item->value;
		length += item->key_size + sizeof(": ") - 1 + value->length + sizeof("\r\n") - 1;
	}
	for(item = dict_first(&it, headers); item; item = dict_next(&it, headers))
	{
		lower = string_lower(item); // key may contain upper case letters
		if (string_equal(lower, &connection)) // skip connection header
		{
			free(lower);
			continue;
		}
		free(lower);
		value = item->value;
		length += item->key_size + sizeof(": ") - 1 + value->length + sizeof("\r\n") - 1;
	}
	length += connection.length + sizeof(": ") - 1 + close.length + sizeof("\r\n") - 1;
	if (authorized) length += secret_value.length + consumer_value.length - secret_key.length - consumer_key.length;
	length += sizeof("\r\n") - 1;
	length += cached;

	struct string *result = malloc(sizeof(struct string) + sizeof(char) * (length + 1));
	if (!result)
	{
		free(encoded);
		return 0;
	}
	result->data = (char *)(result + 1);
	result->length = length;

	size_t index = 0;
	ssize_t size;

	// Set method
	memcpy(result->data + index, methods[request->method].data, methods[request->method].length);
	index += methods[request->method].length;

	// Set URI
	result->data[index++] = ' ';
	memcpy(result->data + index, encoded->data, encoded->length);
	index += encoded->length;
	free(encoded);

	// Set version
	result->data[index++] = ' ';
	memcpy(result->data + index, version.data, version.length);
	index += version.length;

	// Add request line terminator.
	result->data[index++] = '\r';
	result->data[index++] = '\n';

	// TODO: check authorized to get proper results

	// Add headers.
	for(item = dict_first(&it, &request->headers); item; item = dict_next(&it, &request->headers))
	{
		key = string((char *)item->key_data, item->key_size);
		if (string_equal(&key, &connection)) continue; // skip connection header
		if (dict_get(headers, &key)) continue; // ignore overwritten headers

		size = response_string_header(result->data + index, &key, item->value, authorized);
		if (size <= 0)
		{
			free(result);
			return 0;
		}
		index += size;
	}
	for(item = dict_first(&it, headers); item; item = dict_next(&it, headers))
	{
		key = string((char *)item->key_data, item->key_size);
		if (string_equal(&key, &connection)) continue; // skip connection header

		lower = string_lower(item); // key may contain upper case letters
		size = response_string_header(result->data + index, lower, item->value, authorized);
		free(lower);
		if (size <= 0)
		{
			free(result);
			return 0;
		}
		index += size;
	}
	struct string header = string("Connection: close\r\n");
	memcpy(result->data + index, header.data, header.length);
	index += header.length;

	// Add headers terminator.
	result->data[index++] = '\r';
	result->data[index++] = '\n';

	// Add the data cached in the stream.
	struct string buffer;
	stream_read(stream, &buffer, cached);
	stream_read_flush(stream, cached);
	memcpy(result->data + index, buffer.data, cached);
	index += cached;

	result->data[index] = 0;
	return result;
}

static int proxy_request(const struct http_request *request, struct http_response *restrict response, struct stream *restrict client, const union json *query, bool authorized)
{
	// hostname port URI [headers]
	// hostname :: string
	// port :: integer
	// URI :: string
	// headers :: object

	struct string key, *value;
	union json *item;

	struct string *URI, *hostname, *buffer;
	unsigned port;
	const struct dict *headers = 0;
	bool tls;
	int status;

	key = string("hostname");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != STRING)) return BadRequest;
	hostname = &item->string_node;

	key = string("port");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != INTEGER)) return BadRequest;
	port = item->integer;

	key = string("URI");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != STRING)) return BadRequest;
	URI = &item->string_node;

	key = string("headers");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != OBJECT)) return BadRequest;
	headers = item->object;

	key = string("notls");
	item = dict_get(query->object, &key);
	if (item && (json_type(item) == BOOLEAN)) tls = !item->boolean;
	else tls = true;

	buffer = request_string(request, URI, client, headers, authorized);
	if (!buffer) return InternalServerError;

	// Establish connection with the cloud.
	struct stream cloud;
	int server = socket_connect(hostname->data, port);
	if (server < 0)
	{
		free(buffer);
		return NotFound;
	}
	//if (tls ? stream_init_tls_connect(&cloud, server, hostname->data) : stream_init(&cloud, server))
	if (tls ? stream_init_tls_connect(&cloud, server, 0) : stream_init(&cloud, server))
	{
		close(server);
		free(buffer);
		return NotFound;
	}

	proxy(client, &cloud, buffer);
	response->code = OK;
	return 0;
}

int proxy_anonymous(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	return proxy_request(request, response, &resources->stream, query, false);
}

int proxy_authorized(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	return proxy_request(request, response, &resources->stream, query, true);
}
