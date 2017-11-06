#include <stdlib.h>
#include <unistd.h>

#include "types.h"
#include "actions.h"
#include "protocol.h"
#include "format.h"
#include "distribute/filement.h"
#include "distribute/uuid.h"
#include "distribute/authorize.h"

int login(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *options)
{
	// {"token":"db34328641997b3b46441cb9f96e75be"}

	struct string key;
	union json *item;

	if (json_type(options) != OBJECT) return BadRequest;

	key = string("token");
	item = dict_get(options->object, &key);
	if (!item || (json_type(item) != STRING)) return BadRequest;

	int32_t client_id = authorize_id(item->string_node.data, 0);
	if (client_id < 0) return Forbidden;

	// Generate UUID with platform_id 0.
	struct string *uuid = uuid_alloc(client_id, 0);
	if (!uuid) return InternalServerError; // TODO: memory error
	char data[UUID_LENGTH + 1];
	*format_hex(data, uuid->data, UUID_SIZE) = 0;
	struct string uuid_hex = string(data, UUID_LENGTH);

	union json *result = json_string_old(&uuid_hex);
	free(uuid);
	if (!result) return InternalServerError; // TODO: memory error
	struct string *json_serialized = json_serialize(result);
	json_free(result);
	if (!json_serialized) return InternalServerError; // TODO: memory error

	remote_json_send(request, response, resources, json_serialized); // TODO: check return code
	free(json_serialized);

	return 0;
}
