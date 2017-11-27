#if defined(OS_ANDROID)

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "types.h"
#include "log.h"
#include "format.h"
#include "actions.h"
#include "distribute.h"

#include "io.h"
#include "storage.h"
#include "access.h"

#include "md5.h"

#define UNSIGNED_LENGTH_MAX 10

char *uuid_get(void *storage)
{
	struct string key = string("uuid");
	return storage_local_settings_get_value(storage, &key);
}

char *uuid_register(const struct string *email, const char *pin, const struct string *devname, const struct string *password, void *storage)
{
	struct string key, value;
	int i=0;
	unsigned client_id;
	struct blocks_array *blocks_array=0;

	char *uuid = distribute_register_email(email, devname, &client_id);
	if (!uuid) return 0;
	// TODO: revert everything on error

	if (!storage_passport_users_add(storage, client_id)) goto error;
	struct string *username=string_alloc("admin",5);
	struct string *salt=string_alloc("8====>-.",8);//TODO The salt must be dynamic from the passport
	struct string *md5password=string_alloc(NULL,40);
	uint32_t hash[4];
	md5(hash, (const uint8_t *)password->data, (uint32_t)password->length);
	format_hex(md5password->data, (const char *)hash, 16);
	memcpy(md5password->data+32,salt->data,salt->length);
	md5(hash, (const uint8_t *)md5password->data, (uint32_t)md5password->length);
	format_hex(md5password->data, (const char *)hash, 16);
	md5password->data[32]='\0';
	md5password->length=32;
	if (!storage_local_users_add(storage,username,md5password)) goto error;
	free(username);
	free(md5password);

	key = string("uuid");
	value = string(uuid, UUID_LENGTH);
	if (!storage_local_settings_add_value(storage, &key, &value)) goto error;
	key = string("blocks_initialized");
	value = string("0");
	if (!storage_local_settings_add_value(storage, &key, &value)) goto error;
 
	blocks_array=access_granted_get_blocks_array();
	if(!blocks_array || !blocks_array->blocks_size)goto error;
	for(i=0;i<blocks_array->blocks_size;i++)
	{
		if(!storage_blocks_add(storage,blocks_array->blocks[i]->block_id, 1, blocks_array->blocks[i]->location, 0))goto error;
	}
	free_blocks_array(blocks_array);blocks_array=0;

	return uuid;

error:
	free(uuid);
	free_blocks_array(blocks_array);
	return 0;	
}

#endif /* OS_ANDROID */
