#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "types.h"
#if !defined(OS_WINDOWS)
# include "actions.h"
#else
# include "../actions.h"
#endif
#include "session.h"

// TODO: implement this better
int cache_list(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	// class :: INTEGER

	if (json_type(query) != INTEGER) return BadRequest;
	if (!resources->session_access) return ERROR_ACCESS;

	// Create respons string in JSON format.
	union json *array = cache_keys(htobe16(query->integer));
	if (array)
	{
		struct string *json = json_serialize(array);
		json_free(array);

		remote_json_chunked_start(&resources->stream, request, response);
		bool success = response_content_send(&resources->stream, response, json->data, json->length);
		free(json);
		remote_json_chunked_end(&resources->stream, response);

		/*response->code = OK;
		bool success = response_headers_send(&resources->stream, request, response, json->length);
		if (success) success = response_body(&resources->stream, response, json);
		free(json);*/

		if (success) return 0;
		else return -1;
	}

	return ERROR_MEMORY;
}

int cache_get(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	// cache_key :: STRING

	if ((json_type(query) != STRING) || (query->string_node.length != CACHE_KEY_SIZE)) return ERROR_MISSING;
	if (!resources->session_access && !auth_id_check(resources)) return Forbidden;

	const struct cache *cache = cache_use(query->string_node.data);
	if (!cache) return NotFound;

	struct string *json = json_serialize(cache->value);
	cache_finish(cache);
	if (!json) return InternalServerError;

	remote_json_chunked_start(&resources->stream, request, response);
	response_content_send(&resources->stream, response, json->data, json->length);
	free(json);
	remote_json_chunked_end(&resources->stream, response);

	/*response->code = OK;
	if (!response_headers_send(&resources->stream, request, response, json->length)) goto fatal;
	if (!response_body(&resources->stream, response, json)) goto fatal;
	free(json);*/

	return 0;

fatal:
	free(json);
	return -1;
}

int cache_flush(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	// cache_key :: STRING

	if ((json_type(query) != STRING) || (query->string_node.length != CACHE_KEY_SIZE)) return ERROR_MISSING;
	if (!resources->session_access) return ERROR_ACCESS;

	cache_destroy(query->string_node.data);

	response->code = OK;
	return (response_headers_send(&resources->stream, request, response, 0) ? 0 : -1);
}
