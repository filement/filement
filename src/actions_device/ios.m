#include <stdlib.h>

#import <AssetsLibrary/ALAssetsLibrary.h>

#include "actions.h"

#define BUFFER_LIMIT (512 * 1024 * 1024) /* 512 MiB */

// Generates real path for an action given a query containing block_id and path parameters.
static struct string *action_path(struct resources *restrict resources, const union json *query, int *status)
{
	union json *block_id, *path;

	struct string key;

	key = string("block_id");
	block_id = dict_get(query->object, &key);
	if (!block_id || (json_type(block_id) != INTEGER))
	{
		*status = ERROR_INPUT;
		return 0;
	}

	key = string("path");
	path = dict_get(query->object, &key);
	if (!path || (json_type(path) != STRING))
	{
		*status = ERROR_INPUT;
		return 0;
	}

	if (!access_auth_check_location(resources, &path->string_node, block_id->integer))
	{
		*status = ERROR_ACCESS;
		return 0;
	}

	block = access_get_blocks(resources, item->integer);
	if (!block)
	{
		*status = ERROR_MISSING;
		return 0;
	}

	struct string *result = access_fs_concat_path(block->location, &path->string_node, 1);
	free(block->location);
	free(block);
	*status = (result ? 0 : ERROR_MEMORY);
	return result;
}

#include <stdio.h>

int ios_upload(off_t length, struct stream *restrict stream)
{
	// Allocate buffer to store file contents.
	size_t index = 0;
	char *file = malloc(length);
	if (!file) return ERROR_MEMORY;

	__block int status;

	// TODO: send this when necessary
	//struct string buffer = string("HTTP/1.1 100 Continue\r\n");
	//stream_write(stream, &buffer) || stream_write_flush(stream);

	// Read file contents and store them in the allocated buffer.
	struct string buffer;
	while (length)
	{
		if (status = stream_read(input, &buffer, (length > BUFFER_SIZE_MAX) ? BUFFER_SIZE_MAX : length))
		{
			free(file);
			return status;
		}
		memcpy(file + index, buffer.data, buffer.length);
		stream_read_flush(input, buffer.length);

		index += buffer.length;
		length -= buffer.length;
	}

	// TODO autorelease pool?

	NSData *data = [NSData dataWithBytesNoCopy: file length: index];
	if (!data) return ERROR_MEMORY; // TODO is this right?

	__block pthread_mutex_t *mutex = malloc(sizeof(*mutex));
    __block pthread_cond_t *condition = malloc(sizeof(*condition));

    if (!mutex || !condition)
    {
        free(condition);
        free(mutex);
        return ERROR_MEMORY;
    }

    pthread_mutex_init(mutex, 0);
    pthread_cond_init(condition, 0);

	__block unsigned pending = 1;

	ALAssetsLibrary *library = [[ALAssetsLibrary alloc] init];
	[library writeImageDataToSavedPhotosAlbum: data metadata: nil completionBlock:
		^(NSURL *url, NSError *error)
		{
			status = (error ? ERROR : 0); // TODO inspect error

			const char *value = [[url absoluteString] UTF8String];
			printf("%s\n", value);

			pthread_mutex_lock(mutex);
			pending -= 1;
			pthread_cond_signal(condition);
			pthread_mutex_unlock(mutex);
		}
	];
	[library release];

	// Wait for the operation to finish.
	pthread_mutex_lock(mutex);
	if (pending)
	{
		while (1)
		{
			pthread_cond_wait(condition, mutex);
			if (!pending) break;
			pthread_mutex_unlock(mutex);
			pthread_mutex_lock(mutex);
		}
	}
    pthread_mutex_unlock(mutex);

    pthread_cond_destroy(condition);
    pthread_mutex_destroy(mutex);

    free(condition);
    free(mutex);

	return 0;
}

int fs_upload(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	int status;
	union json *item;
	struct string key, value;

	if (!resources->session_access && !auth_id_check(resources)) return ERROR_ACCESS;
	if (json_type(query) != OBJECT) return ERROR_INPUT;

	off_t length = content_length(&request->headers);
	if (length < 0) return LengthRequired; // TODO close the connection
	if (length > BUFFER_LIMIT) return RequestEntityTooLarge; // TODO close the connection
	if (!length) return ERROR_INPUT; // deny uploading empty files

	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
	status = ios_upload(length, request, &resources->stream);
	[pool release];
	if (status) return status;

	// redirect if the client specified so
	key = string("redirect");
	if ((item = dict_get(query->object, &key)) && (json_type(item) == STRING))
	{
		key = string("Location");
		if (!response_header_add(response, &key, &item->string_node)) return ERROR_MEMORY;

		response->code = MovedPermanently;
	}
	else response->code = OK;

	if (!response_headers_send(&resources->stream, request, response, 0)) return ERROR;

	return ERROR_CANCEL;
}
