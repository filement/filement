#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "types.h"

#if !defined(OS_WINDOWS)
# include "actions.h"
# include "operations.h"
#else
# include "../actions.h"
# include "../operations.h"
#endif

static int process_control(struct resources *restrict resources, const union json *query, void (*operation)(unsigned), int status)
{
	if (json_type(query) != STRING) return ERROR_MISSING;
	if (!resources->session_access) return ERROR_ACCESS;

	struct string key;
	union json *node;

	struct cache *cache = cache_load(query->string_node.data);
	if (!cache) return ERROR_MISSING;

	key = string("status");
	node = dict_get(((union json *)cache->value)->object, &key);
	node->integer = status;

	key = string("_oid");
	node = dict_get(((union json *)cache->value)->object, &key);
	operation(node->integer);

	cache_save(query->string_node.data, cache);

	return 0;
}

int process_pause(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	// key :: STRING
	response->code = OK;
	return process_control(resources, query, operation_pause, STATUS_PAUSED);
}

int process_resume(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	// key :: STRING
	response->code = OK;
	return process_control(resources, query, operation_resume, STATUS_RUNNING);
}

int process_cancel(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	// key :: STRING
	response->code = OK;
	return process_control(resources, query, operation_cancel, STATUS_CANCELLED);
}
