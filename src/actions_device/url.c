#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "types.h"
#if !defined(OS_WINDOWS)
# include "actions.h"
#else
# include "../actions.h"
#endif

int tinyurl(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	// expire :: INTEGER

	if (!resources->session_access) return ERROR_ACCESS;
	if ((json_type(query) != INTEGER) || (query->integer < 0)) return ERROR_MISSING;

	struct string key, value;
	int status;

	// This actions supports only POST.
	key = string("accept");
	value = string("POST");
	if (!response_header_add(response, &key, &value)) return ERROR_MEMORY;
	if (request->method != METHOD_POST) return MethodNotAllowed;

	// Get content length.
	off_t length = content_length(&request->headers);
	switch (length)
	{
	case ERROR_INPUT:
		return BadRequest; // TODO fix this return value
	case ERROR_MISSING:
		return LengthRequired; // TODO fix this return value
	default:
		return length;
	case 0:
		break;
	}
	if (length > BUFFER_SIZE_MAX) return RequestEntityTooLarge; // TODO fix this return value

	// Read the long URL. It's stored in the request entity.
	if (status = stream_read(&resources->stream, &value, length)) return status;
	union json *url = json_string(value.data, value.length);
	if (!url) return ERROR_MEMORY;

	// Generate tiny URL and store it in the cache.
	// example tiny URL: /~Z3b4fy62b17AW_1p/
	char tinyurl[1 + 1 + CACHE_KEY_SIZE + 1];
	tinyurl[0] = '/';
	tinyurl[1] = '~';
	if (!cache_create(tinyurl + 2, CACHE_URL, url, query->integer))
	{
		json_free(url);
		return ERROR_MEMORY;
	}
	tinyurl[1 + 1 + CACHE_KEY_SIZE] = '/';

	// TODO destory tiny URL cache if sending it to the client fails?

	if (!response_headers_send(&resources->stream, request, response, sizeof(tinyurl))) return -1; // TODO
    response_entity_send(&resources->stream, response, tinyurl, sizeof(tinyurl));
	return status;
}
