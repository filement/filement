#include <unistd.h>

#include "types.h"
#include "actions.h"
#include "protocol.h"
#include "distribute/subscribe.h"

int subscribe_client(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *options)
{
	// "db34328641997b3b46441cb9f96e75be"

	struct string key, value;

	if ((json_type(options) != STRING) || (options->string_node.length != UUID_LENGTH)) return ERROR_INPUT;

	key = string("Content-Type");
	value = string("text/javascript");
	if (!response_header_add(response, &key, &value)) return ERROR_MEMORY;

	key = string("Connection");
	value = string("close");
	if (!response_header_add(response, &key, &value)) return ERROR_MEMORY;

	// Send response headers so that body can be sent immediately after message is received.
	response->code = OK;
	if (!response_headers_send(&resources->stream, request, response, RESPONSE_CHUNKED)) return ERROR_NETWORK;

	// TODO: support encrypted events via stream.

	// Subscribe the client for events.
	if (!subscribe_connect(&options->string_node, resources->stream.fd, resources->control, SUBSCRIBE_CLIENT)) return -1; // TODO: fix this

	// File descriptor is now handled by the subscribtion thread so don't do anything with it here.
	return ERROR_PROGRESS;
}

int subscribe_uuid(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *options)
{
	// "db34328641997b3b46441cb9f96e75be"

	struct string key, value;

	if ((json_type(options) != STRING) || (options->string_node.length != UUID_LENGTH)) return ERROR_INPUT;

	key = string("Content-Type");
	value = string("text/javascript");
	if (!response_header_add(response, &key, &value)) return ERROR_MEMORY;

	key = string("Connection");
	value = string("close");
	if (!response_header_add(response, &key, &value)) return ERROR_MEMORY;

	// Send response headers so that body can be sent immediately after message is received.
	response->code = OK;
	if (!response_headers_send(&resources->stream, request, response, RESPONSE_CHUNKED)) ; // TODO: error

	// TODO: support encrypted events via stream.

	// Subscribe the client for events.
	if (!subscribe_connect(&options->string_node, resources->stream.fd, resources->control, SUBSCRIBE_UUID)) return -1; // TODO: fix this

	// File descriptor is now handled by the subscribtion thread so don't do anything with it here.
	return ERROR_PROGRESS;
}

int subscribe(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *options)
{
	// TODO deprecated; use subscribe_client() instead
	return subscribe_client(request, response, resources, options);
}
