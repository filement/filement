#include <pthread.h>
#include <stdlib.h>
#include <time.h>

#import "types.h"
#import "format.h"
#import "json.h"
#import "cache.h"
#import "evfs.h"

#import <AssetsLibrary/ALAssetsLibrary.h>
#import <AssetsLibrary/ALAssetsFilter.h>
#import <AssetsLibrary/ALAssetRepresentation.h>

#define PREFIX "IMG_"
#define FILENAME_LENGTH_MIN (sizeof(PREFIX) - 1 + 4 + 1)

#define URI_PREFIX	"assets-library://asset/asset."
#define ID			"id="
#define EXT			"ext="

#define JPEG            "jpeg"
#define JPEG_EXT        "JPG"

#define QUICKTIME_TYPE  ".quicktime-movie"
#define QUICKTIME_EXT   ".qt"

#define QT              "qt"
#define QT_EXT          "MOV"

#define PNG             "png"
#define PNG_EXT         "PNG"

#define PATH_SIZE_MAX 4096

#define MTIME_ZERO 1183168800 /* time when the iPhone went on sale for the first time */

// Use mtime of 0 for directories to make sure no change is implied by the mtime. This is essential for range requests.

/*
http://stackoverflow.com/questions/17585908/how-can-i-get-the-list-of-all-video-files-from-library-in-ios-sdk
http://www.mindfiresolutions.com/Using-Groups-How-to-retrieve-data-from-Photos-Application-in-iOS-device-1720.php
http://www.fiveminutes.eu/accessing-photo-library-using-assets-library-framework-on-iphone/
*/

struct index
{
	// Assume the string representation of size and mtime is at most 20B (always works for 64bit integers).
	char buffer[4 + 1 + 20 + 1 + 20 + 1 + PATH_SIZE_MAX];
	char cache[CACHE_KEY_SIZE];
};

// Generates filename for a given URI.
// example URI: assets-library://asset/asset.JPG?id=ED88D560-76EA-479E-B758-F289AB93E1A2&ext=JPG
// example filename: ED88D56076EA479EB758F289AB93E1A2.jpeg
// WARNING: uri must be NUL-terminated
int url_filename(struct string *restrict result, size_t prefix_length, const char *restrict uri, size_t uri_length, const char *restrict real, size_t real_length)
{
	// Find where filetype starts.
	const char *suffix = real + real_length;
	do if (--suffix < real) return ERROR_INPUT;
	while (*suffix != '.');
	size_t suffix_length = real_length - (suffix - real);

    if ((suffix_length == (sizeof(QUICKTIME_TYPE) - 1)) && !memcmp(suffix, QUICKTIME_TYPE, suffix_length))
    {
        suffix = QUICKTIME_EXT;
        suffix_length = sizeof(QUICKTIME_EXT) - 1;
    }

	const char *end = uri + uri_length;

	// Find the query of the URI.
	while (*uri != '?')
		if (++uri == end)
			return ERROR_INPUT;
	uri += 1;

	// Find where the value of the "id" argument starts in the query.
	// The value of the argument must not be empty.
	while (1)
	{
		// Look if current argument is "id". If not, find the next argument.
		if ((end - uri) < (sizeof(ID) - 1 + 1)) return ERROR_INPUT;
		if (!memcmp(uri, ID, sizeof(ID) - 1))
		{
			uri += sizeof(ID) - 1;
			break;
		}
		else
		{
			uri += sizeof(ID) - 1;
			while (*uri != '&')
				if (++uri == end)
					return ERROR_INPUT;
			uri += 1;
		}
	}

	// Find where the value of the "id" argument ends in the query.
	const char *term = uri;
	while (*term != '&')
		if (++term == end)
			break;

	// Generate NUL-terminated string of the form: <id><suffix>
	result->length = prefix_length + (term - uri) + suffix_length;
	result->data = malloc(result->length + 1);
	if (!result->data) return ERROR_MEMORY;
	char *start = format_bytes(result->data + prefix_length, uri, term - uri);
	*format_bytes(start, suffix, suffix_length) = 0;

	return 0;
}

// WARNING: filename must be NUL-terminated
struct string *filename_url(const char *filename, size_t filename_length)
{
	const char *end = filename + filename_length;
	const char *separator = filename;

	// Find filename separator
	while (*separator != '.')
		if (++separator == end)
			return 0;
	separator += 1;
	size_t length = end - separator;

	// Determine what extension to use for the URI.
	char *ext;
	size_t ext_length;
	if ((length == (sizeof(JPEG) - 1)) && !memcmp(separator, JPEG, length))
	{
		ext = JPEG_EXT;
		ext_length = sizeof(JPEG_EXT) - 1;
	}
	else if ((length == (sizeof(PNG) - 1)) && !memcmp(separator, PNG, length))
	{
		ext = PNG_EXT;
		ext_length = sizeof(PNG_EXT) - 1;
	}
	else if ((length == (sizeof(QT) - 1)) && !memcmp(separator, QT, length))
	{
		ext = QT_EXT;
		ext_length = sizeof(QT_EXT) - 1;
	}
	else return 0;

	length = sizeof(URI_PREFIX) - 1 + ext_length + 1 + sizeof(ID) - 1 + (separator - 1 - filename) + 1 + sizeof(EXT) - 1 + ext_length;
	struct string *result = malloc(sizeof(struct string) + length + 1);
	if (!result) return 0;
	result->data = (char *)(result + 1);
	result->length = length;

	char *start = format_bytes(result->data, URI_PREFIX, sizeof(URI_PREFIX) - 1);
	start = format_bytes(start, ext, ext_length);
	*start++ = '?';
	start = format_bytes(start, ID, sizeof(ID) - 1);
	start = format_bytes(start, filename, (separator - 1 - filename));
	*start++ = '&';
	start = format_bytes(start, EXT, sizeof(EXT) - 1);
	*format_bytes(start, ext, ext_length) = 0;

	return result;
}

static int browse_category(const struct string *restrict location, const struct string *restrict cwd, unsigned depth, ALAssetsFilter *filter, evfs_callback_t callback, void *argument)
{
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

	__block unsigned pending = 0; // number of files pending to be browsed
	__block int status = 0; // return status of the operation

	ALAssetsLibrary *library = [[ALAssetsLibrary alloc] init];
    
	if (location->length <= cwd->length) // category is in the requested path
	{
		// assert(!location->length || (location->length == cwd->length));

		struct file file;
		file.name = (location->length ? string("") : *cwd);
		file.size = 0;
		file.mtime = MTIME_ZERO;
		file.type = EVFS_DIRECTORY;
		file.access = EVFS_READ;
		if (status = callback(&file, argument)) return status;

		// Skip subentries if we've reached depth limit.
		if (!depth) return 0;

		pending += 1;

		[library enumerateGroupsWithTypes: ALAssetsGroupAll usingBlock:
			^(ALAssetsGroup *group, BOOL *stop)
			{
				pthread_mutex_lock(mutex);
				if (group)
				{
					pending += 1;
					pthread_mutex_unlock(mutex);
				}
				else
				{
					pending -= 1;
					pthread_cond_signal(condition);
					pthread_mutex_unlock(mutex);
					return;
				}

				[group setAssetsFilter: filter];
				[group enumerateAssetsUsingBlock:
					^(ALAsset *asset, NSUInteger index, BOOL *stop)
					{
						if (!asset)
						{
							pthread_mutex_lock(mutex);
							pending -= 1;
							pthread_cond_signal(condition);
							pthread_mutex_unlock(mutex);
							return;
						}

						struct file file;

						// set file->name
						ALAssetRepresentation *defaultRepresentation = [asset defaultRepresentation];
                        if (!defaultRepresentation) return; // skip assets that are not available
						NSString *string = [defaultRepresentation UTI];
						size_t prefix_length = (location->length ? 0 : (cwd->length + 1));
						const char *url = [[[defaultRepresentation url] absoluteString] UTF8String];
						//if (filename(&file.name, prefix_length, [string UTF8String], [string lengthOfBytesUsingEncoding: NSUTF8StringEncoding], index + 1) < 0) ; // TODO
						if (url_filename(&file.name, prefix_length, url, strlen(url), [string UTF8String], [string lengthOfBytesUsingEncoding: NSUTF8StringEncoding]) < 0) ; // TODO
						if (prefix_length) *format_bytes(file.name.data, cwd->data, cwd->length) = '/';

						file.size = [defaultRepresentation size];
						file.mtime = (time_t)[[asset valueForProperty: ALAssetPropertyDate] timeIntervalSince1970];
						file.type = EVFS_REGULAR;
						file.access = EVFS_READ;

						// Set file content.
						file._encoding = EVFS_ENCODING_IDENTITY;
						file._content.data = malloc(file.size);
						if (!file._content.data) ; // TODO
						file._content.length = [defaultRepresentation getBytes: file._content.data fromOffset: 0 length: file.size error: 0];
						if (file._content.length != file.size) ; // TODO

						int status;
						if (status = callback(&file, argument)) return; // TODO

						free(file._content.data);
						free(file.name.data);
					}
				];
			}
		failureBlock:
			^(NSError *error)
			{
				// TODO set status to ERROR_ACCESS; set pending to 0
			}
		];
	}
	else // single file browsing
	{
		const char *start = location->data + cwd->length + 1;
		struct string *url = filename_url(start, location->data + location->length - start);

		pending += 1;

		[library assetForURL: [NSURL URLWithString: [NSString stringWithUTF8String: url->data]] resultBlock:
			^(ALAsset *asset)
			{
				struct file file;

				// set file->name
				ALAssetRepresentation *defaultRepresentation = [asset defaultRepresentation];
				//NSString *string = [defaultRepresentation UTI];
				//size_t prefix_length = (location->length ? 0 : (cwd->length + 1));
				//if (url_filename(&file.name, prefix_length, url->data, url->length, [string UTF8String], [string lengthOfBytesUsingEncoding: NSUTF8StringEncoding]) < 0) ; // TODO
				//if (prefix_length) *format_bytes(file.name.data, cwd->data, cwd->length) = '/';

				file.name = string("");
				file.size = [defaultRepresentation size];
				file.mtime = (time_t)[[asset valueForProperty:ALAssetPropertyDate] timeIntervalSince1970];
				file.type = EVFS_REGULAR;
				file.access = EVFS_READ;

				// Set file content.
				file._encoding = EVFS_ENCODING_IDENTITY;
				file._content.data = malloc(file.size);
				if (!file._content.data) ; // TODO
				file._content.length = [defaultRepresentation getBytes: file._content.data fromOffset: 0 length: file.size error: 0];
				if (file._content.length != file.size) ; // TODO

				int status;
				if (status = callback(&file, argument)) return; // TODO

				free(file._content.data);
				//free(file.name.data);

				free(url);

				pthread_mutex_lock(mutex);
				pending -= 1;
				pthread_cond_signal(condition);
				pthread_mutex_unlock(mutex);
			}
		failureBlock:
			^(NSError *error)
			{
				// TODO set status to ERROR_ACCESS; set pending to 0
			}
		];
	}

	// Wait until all files are browsed.
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

	[library release];

	pthread_cond_destroy(condition);
	pthread_mutex_destroy(mutex);

	free(condition);
	free(mutex);

	return status;
}

// WARNING: location must not end in / unless it's just /
int evfs_browse(struct string *restrict location, unsigned depth, evfs_callback_t callback, void *argument, unsigned flags)
{
	struct string videos = string("Videos"), photos = string("Photos");

	if (*location->data != '/') return ERROR_MISSING;
	struct string relative = string((char *)location->data + 1, location->length - 1); // TODO remove this cast

	// TODO better fix for this?
	if ((relative.length == 1) && (relative.data[0] == '.')) relative = string("");

	if (relative.length) // there is a category specified
	{
		// Extract category name from the relative path.
		size_t index = 0;
		do if (relative.data[index] == '/') break;
		while (++index < relative.length);
		struct string category = string(relative.data, index);

		ALAssetsFilter *filter;
		if (string_equal(&category, &videos)) filter = [ALAssetsFilter allVideos];
		else if (string_equal(&category, &photos)) filter = [ALAssetsFilter allPhotos];
		else return ERROR_MISSING;

		NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
		int status = browse_category(&relative, &category, depth, filter, callback, argument);
		[pool release];
		return status;
	}
	else // no category specified; browsing /
	{
		int status;

		struct file file;
		file.name = string("");
		file.size = 0;
		file.mtime = MTIME_ZERO;
		file.type = EVFS_DIRECTORY;
		file.access = EVFS_READ;
		if (status = callback(&file, argument)) return status;

		if (depth--)
		{
			NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
			status = browse_category(&relative, &videos, depth, [ALAssetsFilter allVideos], callback, argument);
			if (status)
			{
				[pool release];
				return status;
			}
			status = browse_category(&relative, &photos, depth, [ALAssetsFilter allPhotos], callback, argument);
			[pool release];
		}

		return status;
	}
}

static int index_process(const struct file *restrict file, void *argument)
{
	struct index *internal = argument;

	// Each entry is a json array item with the following format:
	// "-ra- 40 1372851942 /dir/name"

	if (!file->name.length && (file->type != EVFS_DIRECTORY)) return ERROR_CANCEL; // cannot list regular files

	// Skip dotfiles.
	if (file->name.length && (file->name.data[0] == '.')) return 0;

	char *start = internal->buffer;
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
	if (file->name.length) start = format_bytes(start, file->name.data, file->name.length);

	// Update cache.
	struct cache *cache;
	if (cache = cache_load(internal->cache))
	{
		struct string key = string("found");
		union json *found = dict_get(cache->value->object, &key);
		json_array_insert(found, json_string(internal->buffer, start - internal->buffer)); // TODO error check
		cache_save(internal->cache, cache);
		// TODO maybe store time of last modification?
	}

	return 0;
}

int evfs_index(void)
{
	struct string key;
	struct string location = string("/");
	int block_id = 0;

	// Initialize cache data and create cache.
	struct index internal;
	union json *data = json_object();

	key = string("block_id");
	data = json_object_insert(data, &key, json_integer(block_id));

	key = string("path");
	data = json_object_insert(data, &key, json_string(location.data, location.length));

	key = string("found");
	data = json_object_insert(data, &key, json_array());

	if (!data || !cache_create(internal.cache, CACHE_LIST, data, 0))
	{
		json_free(data);
		return ERROR_MEMORY;
	}

	return evfs_browse(&location, 2, index_process, &internal, 0);
}
