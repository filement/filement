#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "types.h"
#include "log.h"
#include "actions.h"
#include "format.h"
#include "protocol.h"
#include "distribute/filement.h"
#include "distribute/devices.h"
#include "distribute/ftp.h"
#include "distribute/cloud.h"

/*
union json *location_device(struct resources *restrict resources, const struct vector *hosts)
{
	union json *root = json_object_old(false), *address;
	if (!root) fail(1, "Memory error");

	// Each array item is UUID. Get it and find its address.
	// Create a dictionary of all UUIDs and their corresponding addresses
	union json *item;
	const struct string *uuid_hex;
	size_t length = hosts->length;
	while (length--)
	{
		item = vector_get(hosts, length);
		if (json_type(item) != STRING)
		{
			json_free(root);
			return 0;
		}
		uuid_hex = &item->string_node;

		// Get device address. Use empty address if the device is not connected
		address = device_address(uuid_hex);
		if (!address) fail(1, "Memory error");

		int status = json_object_insert_old(root, uuid_hex, address);
		if (status < 0) json_free(address); // TODO: this could be memory error
	}

	return root;
}
*/

union json *location_ftp(struct resources *restrict resources, const struct vector *hosts)
{
	union json *root = json_object_old(false), *address;
	if (!root) fail(1, "Memory error");

	union json *item;
	const struct string *hostname;
	size_t length = hosts->length;
	while (length--)
	{
		item = vector_get(hosts, length);
		if (json_type(item) != STRING)
		{
			json_free(root);
			return 0;
		}
		hostname = &item->string_node;

		address = ftp_address(ip4_address((struct sockaddr *)&resources->address), hostname);
		if (!address) fail(1, "Memory error");

		int status = json_object_insert_old(root, hostname, address);
		if (status < 0) json_free(address); // TODO: this could be memory error
	}

	return root;
}

union json *location_cloud(struct resources *restrict resources, const struct vector *hosts)
{
	union json *root = json_object_old(false), *address;
	if (!root) fail(1, "Memory error");

	union json *item;
	const struct string *hostname;
	size_t length = hosts->length;
	while (length--)
	{
		item = vector_get(hosts, length);
		if (json_type(item) != STRING)
		{
			json_free(root);
			return 0;
		}
		hostname = &item->string_node;

		//address = cloud_address(hostname);
		address = cloud_address(ip4_address((struct sockaddr *)&resources->address), hostname);
		if (!address) fail(1, "Memory error");

		int status = json_object_insert_old(root, hostname, address);
		if (status < 0) json_free(address); // TODO: this could be memory error
	}

	return root;
}

int location(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	if (json_type(query) != OBJECT) return BadRequest;

	struct string device = string("device"), ftp = string("ftp"), cloud = string("cloud");

	union json *root = json_object_old(false), *result;
	if (!root) return -1; // memory error

	const union json *value;
	const struct vector *node;

	struct dict_iterator it;
    const struct dict_item *item;
	struct string key;
	for(item = dict_first(&it, query->object); item; item = dict_next(&it, query->object))
	{
		value = item->value;
		if (json_type(value) != ARRAY)
		{
			json_free(root);
			return BadRequest;
		}
		node = &value->array_node;

		key = string((char *)item->key_data, item->key_size);
		if (string_equal(&key, &device))
			result = devices_locations(node); // Each vector item is UUID. Create a dictionary of all UUIDs and their corresponding addresses.
		else if (string_equal(&key, &ftp))
			result = location_ftp(resources, node);
		else if (string_equal(&key, &cloud))
			result = location_cloud(resources, node);
		else continue; // ignore unrecognized arguments

		json_object_insert_old(root, &key, result); // TODO: this can fail. result will be freed by json_object_insert_old() but should something else be done?
	}

	struct string *buffer = json_serialize(root);
	json_free(root);
	if (!buffer) return -1; // memory error

	remote_json_send(request, response, resources, buffer);
	free(buffer);

	return 0;
}
