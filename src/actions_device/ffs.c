#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "types.h"
#include "arch.h"
#include "format.h"

#if defined(PUBCLOUD)
# include <sys/fsuid.h>
#endif

#if !defined(OS_WINDOWS)
# include "actions.h"
# include "protocol.h"
# include "evfs.h"
# include "magic.h"
# include "download.h"
# include "io.h"
# include "access.h"
# include "upload.h"
# include "operations.h"
#else
# include <windows.h>
# include <wchar.h>
# include "mingw.h"
# include "../protocol.h"
# include "../actions.h"
# include "../evfs.h"
# include "../earchive.h"
# include "../io.h"
# include "../access.h"
# include "../magic.h"
# include "../download.h"
# include "../upload.h"
# include "../operations.h"
#endif

#include "session.h"

struct list
{
	struct stream *stream;
	const struct http_request *request;
	struct http_response *response;
};

struct remove
{
	struct paste *paste;
	struct vector tree;
};

static struct string content_type = {.data = "Content-Type", .length = sizeof("Content-Type") - 1};

static int request_paths(const struct vector *restrict request, struct vector *restrict paths, struct resources *restrict resources)
{
	struct string key, *location;
	size_t index;
	struct blocks *block;
	union json *item, *block_id, *path;

	if (!request->length) return NotFound;

	for(index = 0; index < request->length; ++index)
	{
		item = vector_get(request, index);
		if (json_type(item) != OBJECT) return NotFound;

		key = string("block_id");
		block_id = dict_get(item->object, &key);
		if (!block_id || (json_type(block_id) != INTEGER)) return NotFound;

		key = string("path");
		path = dict_get(item->object, &key);
		if (!path || (json_type(path) != STRING)) return NotFound;

		block = access_get_blocks(resources, block_id->integer);
		if (!block) return NotFound;
		if (!access_auth_check_location(resources, &path->string_node, block->block_id))
		{
			free(block->name);
			free(block->location);
			free(block);
			return NotFound;
		}
		location = access_path_compose(block->location, &path->string_node, 0);
		free(block->name);
		free(block->location);
		free(block);
		if (!location) return NotFound;

		if (!vector_add(paths, location)) return InternalServerError;
	}

	return 0;
}

static int list_process(const struct file *restrict file, void *argument)
{
	// Each entry is a json array item with the following format:
	// "-ra- 40 1372851942 /dir/name"

	struct list *item = argument;

	if (!file->name.length && (file->type != EVFS_DIRECTORY)) return ERROR_CANCEL; // cannot list regular files

	// Skip dotfiles.
	if (file->name.length && (file->name.data[0] == '.')) return 0;

	// Assume the string representation of size and mtime is at most 20B (always works for 64bit integers).
	struct string chunk;
	chunk.data = malloc(1 + 1 + 4 + 1 + 20 + 1 + 20 + 1 + file->name.length + 1);
	if (!chunk.data) return ERROR_MEMORY;

	char *start = chunk.data;

	// Start response if this is the first entry.
	if (item->response->content_encoding < 0)
	{
		remote_json_chunked_start(item->stream, item->request, item->response);
		*start++ = '[';
	}
	else *start++ = ',';

	*start++ = '"';
	*start++ = ((file->type == EVFS_DIRECTORY) ? 'd' : '-');
	*start++ = ((file->access & EVFS_READ) ? 'r' : '-');
	*start++ = ((file->access & EVFS_APPEND) ? 'a' : '-');
	*start++ = ((file->access & EVFS_MODIFY) ? 'm' : '-');
	*start++ = ' ';
	start = format_uint(start, file->size);
	*start++ = ' ';
	start = format_uint(start, file->mtime);
	*start++ = ' ';
	*start++ = '/';
	start = json_dump_string(start, file->name.data, file->name.length);
	*start++ = '"';

	bool success = response_content_send(item->stream, item->response, chunk.data, start - chunk.data);
	free(chunk.data);
	if (success) return 0;
	else return -1; // TODO
}

static int indexed_browse(const union json *restrict all, const struct string *restrict prefix, unsigned depth, void *argument)
{
	struct string location;
	location = string(prefix->data + 1, prefix->length - 1);
	if ((location.length == 1) && (location.data[0] == '.')) location.length = 0;

	// TODO more checks for validity of the cache?

	struct file file;
	int status;

	size_t index;
	const struct string *entry;
	const char *path;
	char *end;
	size_t length;

	for(index = 0; index < all->array_node.length; ++index)
	{
		entry = &((union json *)vector_get(&all->array_node, index))->string_node;

		// Determine entry path.
		path = entry->data;
		while (*path++ != '/') ;
		length = (entry->data + entry->length) - path;

		if ((length >= location.length) && !memcmp(path, location.data, location.length)) // if the entry is in the specified location
		{
			file.name = string((char *)path + location.length, length - location.length); // TODO fix this cast
			if (location.length && (length > location.length)) file.name = string(file.name.data + 1, file.name.length - 1); // don't include the initial / in the length

			file.type = ((entry->data[0] == 'd') ? EVFS_DIRECTORY : EVFS_REGULAR);
			file.access = 0;
			if (entry->data[1] != '-') file.access |= EVFS_READ;
			if (entry->data[2] != '-') file.access |= EVFS_APPEND;
			if (entry->data[3] != '-') file.access |= EVFS_MODIFY;

			file.size = strtoll(entry->data + 4, &end, 10);
			file.mtime = strtoll(end + 1, 0, 10);

			status = list_process(&file, argument);
			if (status) return status;
		}
	}

	return 0;
}

int ffs_list(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	/*
	Request : {"block_id" = int , "path" = string, "depth" = int}
	block_id -> block_id from fs.get_block
	path -> path of the stat
	depth -> max depth of listing
	*/
	struct string *json_serialized=NULL;
	union json *item,*root,*temp;
	struct blocks *block=0;
	struct string *path=0,key;
	unsigned depth = 0;
	int local_errno=0;
	int status;

	//firstly get the block_id from the request
	if(resources->auth_id)auth_id_check(resources);
	if(!resources->auth && !session_is_logged_in(resources)) return Forbidden;

	//polzvam query za da vzema arguments
	if (json_type(query) != OBJECT) return NotFound;

	key = string("depth");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != INTEGER)) return NotFound;
	depth = item->integer;

	key = string("block_id");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != INTEGER)) return NotFound;
	block = access_get_blocks(resources,item->integer);
	if (!block) return NotFound;

	key = string("path");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != STRING)) return NotFound;

	if(!access_auth_check_location(resources,&item->string_node,block->block_id)){local_errno=1101;goto error;}  // Auth check
	path=access_path_compose(block->location,&item->string_node,0);
	if(!path)goto error;

	free(block->name);
	free(block->location);
	free(block);

	struct list ffs_data = {.stream = &resources->stream, .request = request, .response = response};

#if defined(OS_IOS)
	// Use listing cache if there is one.
	union json *array = cache_keys(CACHE_LIST);
    if (!array) return ERROR_MEMORY;
	const struct vector *keys = &array->array_node;
	size_t index;
	char *cache_key;
	const struct cache *cache;
	for(index = 0; index < keys->length; ++index)
	{
		cache_key = ((union json *)vector_get(keys, index))->string_node.data;
		cache = cache_use(cache_key);
		if (!cache) continue; // ignore non-existing cache keys

		root = cache->value;
		if (json_type(root) == OBJECT)
		{
			key = string("status");
			if (!dict_get(root->object, &key)) // the file list is the one entry that doesn't have status
			{
				struct string *a = json_serialize(root);
                free(a);

				key = string("found");
				status = indexed_browse(dict_get(root->object, &key), path, depth, &ffs_data);
				// TODO check status
				cache_finish(cache);
				goto finally;
			}
		}

		cache_finish(cache);
	}
	json_free(array);
#endif

	status = evfs_browse(path, depth, list_process, &ffs_data, EVFS_NESTED | EVFS_EXTRACT);
	// TODO check status

finally:
	if (response->content_encoding < 0) return NotFound;
	response_content_send(&resources->stream, response, "]", 1);
	remote_json_chunked_end(&resources->stream, response);

	return 0;

error:
	if(!local_errno)local_errno=errno;
	return remote_json_error(request, response, resources, local_errno);
}

int ffs_download(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	// [{"block_id": INTEGER, "path": STRING}, ...]

	struct vector paths;
	size_t index;
	int status;

	if (resources->auth_id) auth_id_check(resources);
	if (!resources->auth && !session_is_logged_in(resources)) return ERROR_ACCESS;

	if (json_type(query) != ARRAY) return ERROR_MISSING;
	if (!vector_init(&paths, VECTOR_SIZE_BASE)) return ERROR_MEMORY;
	if (status = request_paths(&query->array_node, &paths, resources)) return status;

	struct download info;
	info.stream = &resources->stream;
	info.request = request;
	info.response = response;
	info.offset = 0;
	info.total = 22; // end of central directory

	status = download(&paths, &info, ARCHIVE_ZIP);

	for(index = 0; index < paths.length; ++index)
		free(vector_get(&paths, index));
	vector_term(&paths);

	return status;
}

static int size_process(const struct file *restrict file, void *argument)
{
	struct string key = string("size");
	struct cache *cache = cache_load(argument);

	if (cache)
	{
		union json *value = dict_get(((union json *)cache->value)->object, &key);
		value->integer += file->size;
		cache_save(argument, cache);
	}

	return (cache ? 0 : ERROR_CANCEL);
}

int ffs_size(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	// {"block_id": INTEGER, "path": STRING}

	union json *item;
	struct blocks *block = 0;
	struct string key, *path = 0;
	int status = -1;

	//firstly get the block_id from the request
	if (resources->auth_id) auth_id_check(resources);
	if (!resources->auth && !session_is_logged_in(resources)) return Forbidden;

	if (json_type(query) != OBJECT) return BadRequest;

	key = string("block_id");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != INTEGER)) return BadRequest;
	block = access_get_blocks(resources, item->integer);
	if (!block) return NotFound;

	key = string("path");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != STRING))
	{
		status = BadRequest;
		goto error;
	}
	if (!access_auth_check_location(resources, &item->string_node, block->block_id))
	{
		status = Forbidden; // Auth check
		goto error;
	}
	path = access_path_compose(block->location, &item->string_node, 0); 
	if (!path)
	{
		status = Forbidden;
		goto error;
	}

	if (block)
	{
		free(block->name);
		free(block->location);
		free(block);
		block = 0;
	}

	union json *progress = json_object();
	key = string("size");
	progress = json_object_insert(progress, &key, json_integer(0));
	key = string("status");
	json_object_insert(progress, &key, json_integer(1));
	if (!progress) goto error;

	char cache[CACHE_KEY_SIZE];
	if (!cache_create(cache, CACHE_PROGRESS, progress, 0))
	{
		json_free(progress);
		goto error;
	}

	if (status = response_cache(cache, request, response, resources))
	{
		cache_destroy(cache);
		goto error;
	}

	status = evfs_browse(path, 63, size_process, cache, 0);

	free(path);

	// Update cache to indicate that the operation has finished.
	key = string("status");
	struct cache *data = cache_load(cache);
	if (data)
	{
		union json *value = dict_get(((union json *)data->value)->object, &key);
		value->integer = status;
		cache_save(cache, data);
	}

	return ERROR_CANCEL;

error:
	if (block)
	{
		if (block->location) free(block->location);
		free(block);
	}
	free(path);

	return status;
}

// TODO remove this; it's for compatibility with versions < 0.18.
#if !defined(OS_WINDOWS)
# include "earchive.h"
#endif
#include "ffs_transfer_old"

int ffs_archive(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	// [{"block_id": INTEGER, "path": STRING}, ...]

	struct vector paths;
	size_t index;
	int status;

	if (resources->auth_id) auth_id_check(resources);
	if (!resources->auth && !session_is_logged_in(resources)) return ERROR_ACCESS;

	// TODO remove this
	// Support old transfer for versions < 0.18.
	struct string key = string("range");
	if ((request->method == METHOD_GET) && !dict_get(&request->headers, &key)) return ffs_archive_old(request, response, resources, query);

	if (json_type(query) != ARRAY) return ERROR_MISSING;
	if (!vector_init(&paths, VECTOR_SIZE_BASE)) return ERROR_MEMORY;
	if (status = request_paths(&query->array_node, &paths, resources)) return status;

	struct download info;
	info.stream = &resources->stream;
	info.request = request;
	info.response = response;
	info.offset = 8; // header
	info.total = 8; // header

	status = download(&paths, &info, ARCHIVE_EARCHIVE);

	for(index = 0; index < paths.length; ++index)
		free(vector_get(&paths, index));
	vector_term(&paths);

	return status;
}

static int move_filename(const struct string *restrict source, const struct string *restrict filename, struct string *restrict name)
{
	// Retrieve last path component of source.
	size_t index = source->length - 1;
	while (source->data[index] != *SEPARATOR) index -= 1;
	index += 1;

	// Allocate memory for relative path to the file.
	name->length = source->length - index;
	if (filename->length) name->length += 1 + filename->length;
	name->data = malloc(name->length + 1);
	if (!name->data) return ERROR_MEMORY;

	// Generate relative path to the file.
	char *start = format_bytes(name->data, source->data + index, source->length - index);
	if (filename->length)
	{
		*start++ = *SEPARATOR;
		start = format_bytes(start, filename->data, filename->length);
	}
	*start = 0;

	return 0;
}

static int copy_process(const struct file *restrict file, void *argument)
{
	struct paste *copy = argument;

	// TODO this should be executed only if there is cache
	if (!operation_progress(copy->operation_id)) return ERROR_CANCEL;

	// No sensible way to copy if the root source entry is a directory and the destination is a regular file.
	if (!file->name.length && (file->type == EVFS_DIRECTORY) && (copy->mode == EVFS_REGULAR))
		return ERROR_CANCEL;

#if !defined(OS_WINDOWS)
	struct string name;
	if (move_filename(copy->source, &file->name, &name)) return ERROR_MEMORY;
#else
	// Retrieve last path component of source.
	size_t index = copy->source->length - 1;
	while (copy->source->data[index] != '/') index -= 1;
	index += 1;

	// Allocate memory for relative path to the file.
	struct string name;
	char *start;
	name.length = copy->source->length - index;
	if (file->name.length) name.length += 1 + file->name.length;
	name.data = malloc(name.length + 1);
	if (!name.data) return ERROR_MEMORY;

	// Generate relative path to the file.
	start = format_bytes(name.data, copy->source->data + index, copy->source->length - index);
	if (file->name.length)
	{
		*start++ = '/';
		format_bytes(start, file->name.data, file->name.length);
	}
	// TODO nul terminator?
#endif

	paste_create(&name, file->type, copy);
	free(name.data);
	if (copy->fd < 0) return copy->fd;

	if (file->type == EVFS_DIRECTORY) return 0; // nothing to do with a directory

	int status = evfs_file(file, paste_write, argument);
	close(copy->fd);
	return status;
}

int ffs_copy(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	struct string key, value;
	size_t index;
	struct vector sources;
	struct string *destination;

	union json *node, *item;
	unsigned block_id;
	struct string *path;

	struct blocks *block;

	struct cache *cache;
	union json *field;

	int status;

#if defined(PUBCLOUD)
	unsigned uid = 0;
	unsigned gid = 0;
#endif
	
	if (resources->auth_id) auth_id_check(resources);
	if (!resources->auth && !session_is_logged_in(resources)) return Forbidden;
	if (!access_check_write_access(resources)) return Forbidden;

	if (json_type(query) != OBJECT) return NotFound;

	// Generate destination path.
	{
		key = string("dest");
		node = dict_get(query->object, &key);
		if (!node || (json_type(node) != OBJECT)) return NotFound;

		key = string("path");
		item = dict_get(node->object, &key);
		if (!item || (json_type(item) != STRING)) return NotFound;
		destination = path = &item->string_node;

		key = string("block_id");
		item = dict_get(node->object, &key);
		if (!item || (json_type(item) != INTEGER)) return NotFound;
		block_id = item->integer;
		block = access_get_blocks(resources, block_id);
		if (!block) return NotFound;

		if (!access_auth_check_location(resources, destination, block->block_id))
		{
			free(block->name);
			free(block->location);
			free(block);
			return Forbidden;
		}
		destination = access_path_compose(block->location, destination, 1);

#if defined(PUBCLOUD)
		uid = block->uid;
		gid = block->gid;
#endif

		free(block->name);
		free(block->location);
		free(block);
		if (!destination) return InternalServerError;
	}

	// Generate source path.
	{
		key = string("src");
		node = dict_get(query->object, &key);
		if (!node || (json_type(node) != ARRAY))
		{
			free(destination);
			return NotFound;
		}

		if (!vector_init(&sources, VECTOR_SIZE_BASE))
		{
			free(destination);
			return InternalServerError;
		}
		if (status = request_paths(&node->array_node, &sources, resources)) goto finally;
	}
	
#ifdef PUBCLOUD
	setfsuid(uid);
	setfsgid(gid);	
#endif

#ifdef OS_BSD
	extern struct string UUID;
#else
	extern struct string UUID_WINDOWS;
	struct string UUID = UUID_WINDOWS;
#endif

	struct paste copy;
	status = paste_init(&copy, &sources, destination, &UUID, block_id, path);
	if (status) goto finally;

	if (status = response_cache(copy.progress, request, response, resources))
	{
		cache_destroy(copy.progress);
		goto finally;
	}

	// Copy each source item.
	for(index = 0; index < sources.length; ++index)
	{
		copy.source = vector_get(&sources, index);
		status = evfs_browse(copy.source, 63, copy_process, &copy, EVFS_STRICT);
	}

	// Update operation data to indicate the operation has finished.
	if (cache = cache_load(copy.progress))
	{
		key = string("status");
		field = dict_get(((union json *)cache->value)->object, &key);
		field->integer = status;
		cache_save(copy.progress, cache);
	}
	operation_end(copy.operation_id);

	status = ERROR_CANCEL;

finally:
	for(index = 0; index < sources.length; ++index) free(vector_get(&sources, index));
	vector_term(&sources);
	free(destination);

	return status;
}

int ffs_transfer(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	struct string key, *value;
	size_t index;
	struct vector sources;
	struct string *destination;

	struct string buffer;
	char *start;

	union json *node, *item;
	unsigned block_id;
	struct string *path;

	struct blocks *block;

	struct string *uuid, *host, *args;
	int port;

	struct cache *cache;
	union json *field;

	int status;
	
#if defined(PUBCLOUD)
	unsigned uid = 0;
	unsigned gid = 0;
#endif

	if (resources->auth_id) auth_id_check(resources);
	if (!resources->auth && !session_is_logged_in(resources)) return Forbidden;
	if (!access_check_write_access(resources)) return Forbidden;

	if (json_type(query) != OBJECT) return NotFound;

	key = string("uuid");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != STRING)) return NotFound;
	uuid = &item->string_node;

	key = string("host");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != STRING)) return NotFound;
	host = &item->string_node;

	key = string("port");
	item=dict_get(query->object, &key);
	if (!item || (json_type(item) != INTEGER)) return NotFound;
	port = item->integer;

	key = string("args");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != STRING)) return NotFound;
	args = &item->string_node;

	// Generate destination path.
	{
		key = string("dest");
		node = dict_get(query->object, &key);
		if (!node || (json_type(node) != OBJECT)) return NotFound;

		key = string("path");
		item = dict_get(node->object, &key);
		if (!item || (json_type(item) != STRING)) return NotFound;
		destination = path = &item->string_node;

		key = string("block_id");
		item = dict_get(node->object, &key);
		if (!item || (json_type(item) != INTEGER)) return NotFound;
		block_id = item->integer;
		block = access_get_blocks(resources, block_id);
		if (!block) return NotFound;

		if (!access_auth_check_location(resources, destination, block->block_id))
		{
			free(block->name);
			free(block->location);
			free(block);
			return Forbidden;
		}
		destination = access_path_compose(block->location, destination, 1);

#if defined(PUBCLOUD)
		uid = block->uid;
		gid = block->gid;
#endif

		free(block->name);
		free(block->location);
		free(block);
		if (!destination) return InternalServerError;
	}

	// Generate source path.
	{
		key = string("src");
		node = dict_get(query->object, &key);
		if (!node || (json_type(node) != ARRAY))
		{
			free(destination);
			return NotFound;
		}

		if (!vector_init(&sources, VECTOR_SIZE_BASE))
		{
			free(destination);
			return InternalServerError;
		}

		struct vector *src = &node->array_node;
		union json *location;
		size_t index;
		for(index = 0; index < src->length; ++index)
		{
			location = vector_get(src, index);
			if (json_type(location) != STRING)
			{
				status = NotFound;
				goto finally;
			}
			if (!vector_add(&sources, &location->string_node))
			{
				status = ERROR_MEMORY;
				goto finally;
			}
		}
	}

#if defined(PUBCLOUD)
	setfsuid(uid);
	setfsgid(gid);	
#endif

	struct paste copy;
	if (sources.length == 1) copy.source = vector_get(&sources, 0); // TODO why is this initialized so early in the code?
	status = paste_init(&copy, &sources, destination, uuid, block_id, path);
	if (status) goto finally;

	if (status = response_cache(copy.progress, request, response, resources))
	{
		cache_destroy(copy.progress);
		goto finally;
	}

	status = http_transfer_persist(host, port, args, &copy);

	// Update operation data to indicate the operation has finished.
	if (cache = cache_load(copy.progress))
	{
		key = string("status");
		field = dict_get(((union json *)cache->value)->object, &key);
		if (status > 0) field->integer = -1;
		else if (status != ERROR_CANCEL) field->integer = status; // TODO this fixes a bug that field->integer is first set to ERROR_CANCELLED by the operation and then overwritten; think if this will work
		cache_save(copy.progress, cache);
	}
	operation_end(copy.operation_id);

	status = ERROR_CANCEL;

finally:
	vector_term(&sources);
	free(destination);

	return status;
}

// If source and destination are in the same filesystem, just rename the root entry.
static int move_process(const struct file *restrict file, void *argument)
{
	struct remove *remove = argument;
	struct paste *move = remove->paste;

	// TODO this should be executed only if there is cache
	if (!operation_progress(move->operation_id)) return ERROR_CANCEL;

	// No sensible way to move if the root source entry is a directory and the destination is a regular file.
	if (!file->name.length && (file->type == EVFS_DIRECTORY) && (move->mode == EVFS_REGULAR))
		return ERROR_CANCEL;

	struct string name;
	if (move_filename(move->source, &file->name, &name)) return ERROR_MEMORY;

	paste_create(&name, file->type, move);
	free(name.data);
	if (move->fd < 0) return move->fd;

	struct string separator = string("/"), *source;
	if (file->name.length) source = string_concat(move->source, &separator, &file->name);
	else source = string_alloc(move->source->data, move->source->length);
	if (!source) return ERROR_MEMORY;

	// Copy file content if the file is regular. Remove source file.
	if (file->type == EVFS_DIRECTORY)
	{
		// TODO fix this hack: i set source->length to 0 to indicate that the file is directory
		source->length = 0;
		if (!vector_add(&remove->tree, source)) return ERROR_MEMORY;
		return 0;
	}
	else
	{
		// assert(file->type == EVFS_REGULAR);

		int status = evfs_file(file, paste_write, move);
		close(move->fd);

		if (!vector_add(&remove->tree, source)) return ERROR_MEMORY;
		return status;
	}
}

int ffs_move(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	struct string key;
	size_t index;
	struct vector sources;
	struct string *destination;

	union json *node, *item;
	unsigned block_id;
	struct string *path;

	struct blocks *block;

	struct cache *cache;
	union json *field;

	int status;

#if defined(PUBCLOUD)
	unsigned uid = 0;
	unsigned gid = 0;
#endif
	
	if (resources->auth_id) auth_id_check(resources);
	if (!resources->auth && !session_is_logged_in(resources)) return Forbidden;
	if (!access_check_write_access(resources)) return Forbidden;

	if (json_type(query) != OBJECT) return NotFound;

	// Generate destination path.
	{
		key = string("dest");
		node = dict_get(query->object, &key);
		if (!node || (json_type(node) != OBJECT)) return NotFound;

		key = string("path");
		item = dict_get(node->object, &key);
		if (!item || (json_type(item) != STRING)) return NotFound;
		destination = path = &item->string_node;

		key = string("block_id");
		item = dict_get(node->object, &key);
		if (!item || (json_type(item) != INTEGER)) return NotFound;
		block_id = item->integer;
		block = access_get_blocks(resources, block_id);
		if (!block) return NotFound;

		if (!access_auth_check_location(resources, destination, block->block_id))
		{
			free(block->name);
			free(block->location);
			free(block);
			return Forbidden;
		}
		destination = access_path_compose(block->location, destination, 1);

#if defined(PUBCLOUD)
		uid = block->uid;
		gid = block->gid;
#endif

		free(block->name);
		free(block->location);
		free(block);
		if (!destination) return InternalServerError;
	}

	// Generate source path.
	{
		key = string("src");
		node = dict_get(query->object, &key);
		if (!node || (json_type(node) != ARRAY))
		{
			free(destination);
			return NotFound;
		}

		if (!vector_init(&sources, VECTOR_SIZE_BASE))
		{
			free(destination);
			return InternalServerError;
		}
		if (status = request_paths(&node->array_node, &sources, resources)) goto finally;
	}
	
#ifdef PUBCLOUD
	setfsuid(uid);
	setfsgid(gid);	
#endif

#ifdef OS_BSD
	extern struct string UUID;
#else
	extern struct string UUID_WINDOWS;
	struct string UUID = UUID_WINDOWS;
#endif

	struct paste move;
	status = paste_init(&move, &sources, destination, &UUID, block_id, path);
	if (status) goto finally;

	if (status = response_cache(move.progress, request, response, resources))
	{
		cache_destroy(move.progress);
		goto finally;
	}

	struct remove remove;
	size_t last;
	remove.paste = &move;

#if !defined(OS_WINDOWS)
    struct stat info;
#else
    struct _stati64 info;
#endif

	// Move each source item.
	for(index = 0; index < sources.length; ++index)
	{
		// Just rename the source if it is on the same filesystem as the destination.
		move.source = vector_get(&sources, index);
		if (!stat(move.source->data, &info) && (info.st_dev == move.dev))
		{
			if (status = paste_rename(&move)) break;
		}
		else
		{
			if (!vector_init(&remove.tree, VECTOR_SIZE_BASE)) ; // TODO ERROR_MEMORY
			status = evfs_browse(move.source, 63, move_process, &remove, EVFS_STRICT);
			if (status) break;

			// Remove source directory tree.
			last = remove.tree.length;
			while (last)
			{
				path = vector_get(&remove.tree, --last);
				if (path->length) unlink(path->data); // TODO fix this ugly hack
				else rmdir(path->data);
				free(path);
			}
			vector_term(&remove.tree);
		}
	}

	// Update operation data to indicate the operation has finished.
	if (cache = cache_load(move.progress))
	{
		key = string("status");
		field = dict_get(((union json *)cache->value)->object, &key);
		field->integer = status;
		cache_save(move.progress, cache);
	}
	operation_end(move.operation_id);

	status = ERROR_CANCEL;

finally:
	for(index = 0; index < sources.length; ++index) free(vector_get(&sources, index));
	vector_term(&sources);
	free(destination);

	return status;
}
