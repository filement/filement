#ifdef OS_WINDOWS
# include <sys/stat.h>
# include <windows.h>
# include <wchar.h>
# include "mingw.h"
#endif

#include <ctype.h>
#include <stdlib.h>
#include <sys/socket.h>

#if defined(OS_MAC) && !defined(OS_IOS)
# include <sys/stat.h>
# include <CoreServices/CoreServices.h>
#endif

#include "types.h"
#include "format.h"
#include "json.h"
#ifdef OS_WINDOWS
# include "../evfs.h"
# include "../operations.h"
# include "../io.h"
# include "../actions.h"
# include "../access.h"
#else
# include "evfs.h"
# include "operations.h"
# include "io.h"
# include "actions.h"
# include "access.h"
#endif
#include "session.h"

#if defined(OS_MAC) && !defined(OS_IOS)

# include "search.h"

// https://developer.apple.com/library/mac/#documentation/Carbon/Conceptual/SpotlightQuery/Concepts/QueryFormat.html#//apple_ref/doc/uid/TP40001849-CJBEJBHH
// https://developer.apple.com/library/mac/#documentation/Carbon/Reference/MetadataAttributesRef/Reference/CommonAttrs.html#//apple_ref/doc/uid/TP40001694-SW1

#define DIRECTORY "public.folder"

// Seconds after the UNIX epoch when CFAbsoluteTime starts.
// (31years * 365days +8days(leap)) * 24hours * 60minutes * 60seconds + 22seconds(leap)
#define TIME_OFFSET ((time_t)((31 * 365 + 8) * 24 * 60 * 60 + 22))

static const bool result_add(MDQueryRef request, const MDItemRef item, void *vector)
{
	CFStringRef keys[] = {CFSTR("kMDItemPath"), CFSTR("kMDItemContentType"), CFSTR("kMDItemFSContentChangeDate"), CFSTR("kMDItemFSSize")};
	CFDictionaryRef values = MDItemCopyAttributeList(item, keys[0], keys[1], keys[2], keys[3]);

	struct search_entry *entry;
	size_t path_length;

	CFStringRef string = CFDictionaryGetValue(values, keys[0]);
	CFRange all = CFRangeMake(0, CFStringGetLength(string));
	CFStringGetBytes(string, all, kCFStringEncodingUTF8, 0, false, 0, 0, &path_length);

	entry = malloc(sizeof(struct search_entry) + sizeof(char) * (path_length + 1));
	if (!entry) return false; // memory error
	entry->path = (char *)(entry + 1);
	CFStringGetBytes(string, all, kCFStringEncodingUTF8, 0, false, (unsigned char *)entry->path, path_length, 0);
	entry->path[path_length] = 0;

	// Set mode.
	// TODO: can this be done better?
	char type[sizeof(DIRECTORY)];
	string = CFDictionaryGetValue(values, keys[1]);
	all = CFRangeMake(0, CFStringGetLength(string));
	CFStringGetBytes(string, all, kCFStringEncodingUTF8, 0, false, type, sizeof(type) - 1, 0);
	entry->info.st_mode = ((memcmp(type, DIRECTORY, sizeof(type))) ? S_IFREG : S_IFDIR);

	// Set mtime.
	CFDateRef mtime_date = CFDictionaryGetValue(values, keys[2]);
	CFAbsoluteTime mtime_time = CFDateGetAbsoluteTime(mtime_date);
	struct timespec mtime = {.tv_sec = (time_t)mtime_time + TIME_OFFSET, .tv_nsec = 0};
	entry->info.st_mtimespec = entry->info.st_ctimespec = entry->info.st_atimespec = mtime;

	// Set size.
	long integer;
	CFNumberRef number = CFDictionaryGetValue(values, keys[3]);
	CFNumberGetValue(number, kCFNumberLongType, (void *)&integer); // TODO: error check
	entry->info.st_size = (off_t)integer;

	vector_add((struct vector *)vector, entry);

	CFRelease(values);

	return true;
}

struct vector *restrict (search_index_results)(const struct string *root, const struct string *name, bool case_sensitive)
{
	// Generate name filter
	CFStringRef query;
	{
		size_t index, size;
		struct string before = string("kMDItemFSName == '"), after = string("'");

		size_t pattern_length = 0;
		for(index = 0; index < name->length; ++index)
			pattern_length += 1 + (name->data[index] == '\'');

		size_t length = before.length + pattern_length + after.length + !case_sensitive;
		char *filter = malloc(sizeof(char) * (length + 1));
		if (!filter) return 0; // memory error

		size = 0;
		memcpy(filter + size, before.data, before.length);
		size += before.length;

		// Copy filename while escaping single quotes.
		for(index = 0; index < pattern_length; ++index)
		{
			if (name->data[index] == '\'') filter[size++ + index] = '\\';
			filter[size + index] = name->data[index];
		}
		size += index;

		memcpy(filter + size, after.data, after.length);
		size += after.length;
		if (!case_sensitive) filter[size++] = 'c';
		filter[size] = 0;

		query = CFStringCreateWithCString(0, filter, kCFStringEncodingUTF8);
		free(filter);
	}

	struct vector *result = malloc(sizeof(struct vector));
	if (!result || !vector_init(result, VECTOR_SIZE_BASE)) return 0;

	const void *value = CFSTR("kMDItemContentType");
	CFArrayRef attributes = CFArrayCreate(0, &value, 1, 0);

	MDQueryRef request = MDQueryCreate(0, query, attributes, 0);
	void *location_item = (void *)CFStringCreateWithCString(0, root->data, kCFStringEncodingUTF8);
	CFArrayRef location = CFArrayCreate(0, (const void **)&location_item, 1, 0);
	MDQuerySetSearchScope(request, location, 0);
	
	Boolean success = MDQueryExecute(request, kMDQuerySynchronous);
	if (!success) return 0;

	MDQueryDisableUpdates(request);

	long length = MDQueryGetResultCount(request);
	size_t index;
	for(index = 0; index < length; ++index)
		if (!result_add(request, (MDItemRef)MDQueryGetResultAtIndex(request, index), result))
			return 0;

	CFRelease(location);
	CFRelease(location_item);
	CFRelease(request);

	CFRelease(query);
	CFRelease(attributes);

	return result;
}

void search_index_free(struct vector *restrict result)
{
	struct search_entry *entry;
	size_t index;

	for(index = 0; index < result->length; ++index)
	{
		entry = vector_get(result, index);
		free(entry);
	}

	vector_term(result);
	free(result);
}

/*int main(int argc, char *argv[])
{
	if (argc != 2)
	{
		printf("No filename specified\n");
		return 0;
	}

	struct string root = string("/Users/martin/Music/"), name = string(argv[1], strlen(argv[1]));
	struct vector *restrict result = search_index_results(&root, &name);

	struct search_entry *entry;
	size_t index;
	for(index = 0; index < result->length; ++index)
	{
		entry = vector_get(result, index);
		printf("%s\n", entry->path);
	}

	search_index_free(result);

	return 0;
}*/

#elif defined(OS_LINUX) || defined(OS_FREEBSD)

// TODO implement this
/*struct vector *restrict (search_index_results)(const struct string *root, const struct string *name, bool case_sensitive)
{
	struct vector *result = malloc(sizeof(struct vector));
	if (!result || !vector_init(result, VECTOR_SIZE_BASE)) return 0;

	size_t index;
	for(index = 0; index < length; ++index)
		if (!result_add(request, (MDItemRef)MDQueryGetResultAtIndex(request, index), result))
			return 0;

	return result;
}*/

#endif /* OS_MAC && !OS_IOS */

struct filters
{
	struct string *name;
	size_t *table;
	off_t startsize, endsize;
	time_t starttime, endtime;
	bool case_sensitive, exact;

	// TODO these below are not filters
	char progress[CACHE_KEY_SIZE];
	int operation_id;
};

static int search_process(const struct file *restrict file, void *argument)
{
	// Each entry is a json array item with the following format:
	// "-ra- 40 1372851942 /dir/name"

	struct filters *filters = argument;

	if (!file->name.length && (file->type != EVFS_DIRECTORY)) return ERROR_CANCEL; // cannot list regular files

	// Skip dotfiles.
	if (file->name.length && (file->name.data[0] == '.')) return 0;

	// Proceed adding the file only if it matches the specified criteria.
	if ((file->mtime < filters->starttime) || (filters->endtime < file->mtime)) return 0;
	if ((file->size < filters->startsize) || (filters->endsize < file->size)) return 0;
	if (filters->name)
	{
		// Make sure file name is compared with the propper case.
		struct string *name;
		if (filters->case_sensitive) name = (struct string *)&file->name; // TODO fix this cast
		else
		{
			size_t index;
			name = string_alloc(file->name.data, file->name.length);
			if (!name) return ERROR_MEMORY;
			for(index = 0; index < name->length; ++index)
				name->data[index] = tolower(name->data[index]);
		}

		// Skip files that don't match the pattern.
		// If exact matching is specified, skip files where the pattern matches only part of the name.
		ssize_t index = kmp_search(filters->name, filters->table, name);
		if (!filters->case_sensitive) free(name);
		if (index < 0) return 0;
		if (filters->exact)
		{
			if ((index > 0) && (file->name.data[index - 1] != '/')) return 0;
			if (((index + filters->name->length) < file->name.length) && (file->name.data[index + filters->name->length] != '/')) return 0;
		}
	}

	// Assume the string representation of size and mtime is at most 20B (always works for 64bit integers).
	struct string entry;
	entry.data = malloc(1 + 1 + 4 + 1 + 20 + 1 + 20 + 1 + 1 + file->name.length + 1);
	if (!entry.data) return ERROR_MEMORY;

	char *start = entry.data;

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
#if !defined(OS_WINDOWS)
	if (file->name.length) start = json_dump_string(start, file->name.data, file->name.length);
#else
	if (file->name.length) 
		{
		char *tmp = strdup(file->name.data);
		int i=file->name.length-1;
		for(;i>=0;i--)if(tmp[i]=='\\')tmp[i]='/';
		start = json_dump_string(start, tmp, file->name.length);
		free(tmp);
		}
#endif
	entry.length = start - entry.data;

	// Update progress for current file.
	struct cache *cache;
	if (cache = cache_load(filters->progress))
	{
		struct string key = string("found");
		union json *found = dict_get(cache->value->object, &key);
		json_array_insert_old(found, json_string_old(&entry)); // TODO error check
		cache_save(filters->progress, cache);
		// TODO maybe store time of last modification?

		if (!operation_progress(filters->operation_id))
		{
			free(entry.data);
			return ERROR_CANCEL;
		}
	}
	free(entry.data);

	return 0;
}

int fs_find(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	/*
	Request : {"block_id" = int , "path" = string, "max_level" = int, ["byname":{"name"=string | "exactname"=string | "case_sensitive":0|1}] ["bydate":{"starttime":inttimestamp,"endtime":inttimestamp|"exacttime":inttimestamp}], "bysize":{"startsize":int,"endsize":int | "exactsize":int}} if case_sensitive is 0 then name must be only with lower cases
	block_id -> block_id from fs.get_block
	path -> path of the stat
	depth -> depth of listing
	*/
	union json *item = 0, *temp = 0;
	struct blocks *block = 0;
	struct string key, *path = 0;
	int depth = 0;
	struct string *location;
	struct filters pattern;

	union json *field;
	struct cache *cache;
	int status;

	pattern.case_sensitive = true;
	pattern.exact = true;
	pattern.name = 0;
	pattern.table = 0;
	pattern.startsize = 0;
#if !defined(OS_ANDROID) && !defined(OS_WINDOWS)
    pattern.endsize = INT64_MAX; // this relies on _FILE_OFFSET_BITS == 64
#else
    // off_t is 32-bit due to a bug in Bionic C Library
    // http://www.netmite.com/android/mydroid/2.0/bionic/libc/docs/OVERVIEW.TXT
    pattern.endsize = INT32_MAX;
#endif
	pattern.starttime = 0;
	pattern.endtime = INT32_MAX; // TODO this will cause problems in year 2038

	//TODO make async data return method and then I can make it with auth_id
	if (!resources->session_access && !auth_id_check(resources)) return ERROR_ACCESS;

	if (json_type(query) != OBJECT) return ERROR_INPUT;

	key = string("max_level");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != INTEGER)) return ERROR_INPUT;
	depth = item->integer;
	if (!depth) return ERROR_INPUT;

	key = string("block_id");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != INTEGER)) return ERROR_INPUT;
	block = access_get_blocks(resources, item->integer);
	if (!block)
	{
		status = ERROR_ACCESS;
		goto error;
	}

	key = string("path");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != STRING))
	{
		status = ERROR_INPUT;
		goto error;
	}

	location = &item->string_node; 

	if (!access_auth_check_location(resources, &item->string_node, block->block_id))
	{
		status = ERROR_ACCESS;
		goto error;
	}
	path = access_path_compose(block->location, &item->string_node, 1);
	if (!path)
	{
		status = ERROR_MEMORY;
		goto error;
	}

	//byname
	key = string("byname");
	if (item = dict_get(query->object, &key))
	{
		key = string("case_sensitive");
		if (temp = dict_get(item->object, &key))
		{
			if (json_type(temp) != INTEGER) goto error;
			pattern.case_sensitive = temp->integer;
		}

		if (json_type(item) != OBJECT) goto error;
		key = string("name");
		temp = dict_get(item->object, &key);
		if (temp) pattern.exact = false;
		else
		{
			key = string("exactname");
			temp = dict_get(item->object, &key);
		}

		if (!temp || (json_type(temp) != STRING)) goto error;

		pattern.name = string_alloc(temp->string_node.data, temp->string_node.length);
		if (!pattern.case_sensitive)
		{
			size_t index;
			for(index = 0; index < pattern.name->length; ++index)
				pattern.name->data[index] = tolower(pattern.name->data[index]);
		}

		pattern.table = kmp_table(pattern.name);
		if (!pattern.table) goto error;
	}

	//bytime
	key = string("bytime");
	if (item = dict_get(query->object, &key))
	{
		if (json_type(item) != OBJECT) goto error;
		key = string("exacttime");
		if (item = dict_get(item->object, &key))
		{
			if (json_type(item) != INTEGER) goto error;
			pattern.endtime = pattern.starttime = item->integer;
		}
		else
		{
			key = string("starttime");
			item = dict_get(item->object, &key);
			if (!item || (json_type(item) != INTEGER))
			{
				status = ERROR_MISSING;
				goto error;
			}
			pattern.starttime = item->integer;
			
			key = string("endtime");
			item = dict_get(item->object, &key);
			if (!item || (json_type(item) != INTEGER))
			{
				status = ERROR_MISSING;
				goto error;
			}
			pattern.endtime = item->integer;
		}
	}

	//bysize
	key = string("bysize");
	if (item = dict_get(query->object, &key))
	{
		if (json_type(item) != OBJECT) goto error;
		key = string("exactsize");
		if (item = dict_get(item->object, &key))
		{
			if (json_type(item) != INTEGER) goto error;
			pattern.endsize = pattern.startsize = item->integer;
		}
		else
		{
			key = string("startsize");
			item = dict_get(item->object, &key);
			if (!item || (json_type(item) != INTEGER))
			{
				status = ERROR_MISSING;
				goto error;
			}
			pattern.startsize = item->integer;
			
			key = string("endsize");
			item = dict_get(item->object, &key);
			if (!item || (json_type(item) != INTEGER))
			{
				status = ERROR_MISSING;
				goto error;
			}
			pattern.endsize = item->integer;
		}
	}

	// Start operation.
	pattern.operation_id = operation_start();
	if (pattern.operation_id < 0) goto error;

	// Initialize cache data.
	union json *progress = json_object();
	// TODO add filters
	key = string("block_id");
	progress = json_object_insert(progress, &key, json_integer(block->block_id));
	key = string("path");
	progress = json_object_insert(progress, &key, json_string_old(location));
	key = string("status");
	progress = json_object_insert(progress, &key, json_integer(STATUS_RUNNING));
	key = string("found");
	progress = json_object_insert(progress, &key, json_array());
	key = string("_oid");
	progress = json_object_insert(progress, &key, json_integer(pattern.operation_id));

	// Create cache and perform the search.
	while (progress)
	{
		if (!cache_create(pattern.progress, CACHE_LIST, progress, 0))
		{
			json_free(progress);
			break;
		}

		if (status = response_cache(pattern.progress, request, response, resources))
		{
			cache_destroy(pattern.progress);
			break;
		}

		status = evfs_browse(path, depth, search_process, &pattern, 0);

		// Update operation data to indicate the operation has finished.
		if (cache = cache_load(pattern.progress))
		{
			key = string("status");
			field = dict_get(((union json *)cache->value)->object, &key);
			field->integer = status;
			cache_save(pattern.progress, cache);
		}
		operation_end(pattern.operation_id);

		free(pattern.table);
		free(pattern.name);
		free(path);
		if (block)
		{
			free(block->location);
			free(block);
		}

		return ERROR_CANCEL;
	}

	operation_end(pattern.operation_id);

error:
	free(pattern.table);
	free(pattern.name);
	free(path);
	if (block)
	{
		free(block->location);
		free(block);
	}
	return remote_json_error(request, response, resources, status);
}
