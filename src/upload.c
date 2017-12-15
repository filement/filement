#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#if !defined(OS_WINDOWS)
# include <poll.h>
# include <sys/mman.h>
#else
#define WINVER 0x0501
# define WINDOWS_BLOCK_SIZE 0x400000
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <sys/stat.h>
#include <WinIoCtl.h>
#include <io.h>
#include "mingw.h"
#endif

#include "types.h"
#include "format.h"
#include "log.h"
#include "io.h"
#include "actions.h"
#include "evfs.h"
#include "earchive.h"
#include "upload.h"
#include "status.h"
#include "operations.h"

#ifdef OS_WINDOWS
# define ELOOP -1
# define unlink remove
#define ETIMEDOUT WSAETIMEDOUT
#define random rand
#endif

#if defined(FAILSAFE)
# define status_init(...) true
# define status_set(...)
# define status_get(...) 0
# define status_term(...)
#endif

// TODO: check this value
#define PATH_LIMIT 4096

#define CHUNK_MAX 32768

// TODO: write a function that decodes chunk and has API similar to the API of write()

#define string_static(s) {(s), sizeof(s) - 1}
static struct string request_middle = string_static(" HTTP/1.1\r\nHost: "), request_end = string_static("\r\n\r\n");
#undef string_static

struct transfer_context
{
	enum {Header, Metadata, Filename, Size, Data} state;
	struct metadata metadata;
	int output; // currently opened file
	uint64_t size_left; // bytes left until the end of the file
#if !defined(OS_WINDOWS)
	off_t progress, total;
#else
	int64_t progress, total;
#endif
};

#if !defined(OS_WINDOWS)
static int transfer(struct stream *restrict input, int output, off_t length)
#else
static int transfer(struct stream *restrict input, int output, int64_t length)
#endif
{
	struct string buffer;
	int status;
	while (length)
	{
		if (status = stream_read(input, &buffer, (length > BUFFER_SIZE_MAX) ? BUFFER_SIZE_MAX : length)) return status;
		if (!writeall(output, buffer.data, buffer.length)) return ERROR_WRITE;
		stream_read_flush(input, buffer.length);
		length -= buffer.length;
	}
	return 0;
}

#if !defined(OS_WINDOWS)
// WARNING: filename must be non-empty NUL-terminated string
static int create_directory(const struct string *restrict filename)
{
	char *path = memdup(filename->data, filename->length);
	if (!path)
		return ERROR_MEMORY;

	size_t index = 1;
	while (true)
	{
		switch (path[index])
		{
		case '/':
			path[index] = 0;
		case 0:
			if ((mkdir(path, 0755) < 0) && (errno != EEXIST))
			{
				free(path);
				return errno_error(errno);
			}
			if (index == filename->length)
			{
				free(path);
				return 0;
			}
			path[index] = '/';
		}
		index += 1;
	}
}
#else
// WARNING: filename must be NUL-terminated
static int create_directory(struct string *restrict filename)
{
	int i=0;
	struct string *tmp_str=0;
	
	tmp_str=string_alloc(filename->data,filename->length);
	for(i=0;i<tmp_str->length;i++)if(tmp_str->data[i]=='/')tmp_str->data[i]='\\';
	
	while (true)
	{
		switch (tmp_str->data[i])
		{
		case '\\':
			tmp_str->data[i] = 0;
		case 0:
			mkdir(tmp_str->data, 0755);
			if (i == tmp_str->length){free(tmp_str); return 0;}
			else tmp_str->data[i] = '\\';
		}
		i += 1;
	}
	
	free(tmp_str);
}
#endif

// Creates file named filename and opens it for writing.
// WARNING: filename must be NUL-terminated
// TODO better errors
static int create_file(struct string *restrict filename)
{
#if !defined(OS_WINDOWS)
	// TODO: this may truncate an existing file
	int file = creat(filename->data, 0644);
	if (file >= 0) return file;

	switch (errno)
	{
	case ENOENT: // path component does not exist
		break;
	case ENOTDIR: // TODO: change this when there is evfs write support
	default:
		return -1;
	}

	// Create file's parent directory.
	char *separator = strrchr(filename->data, '/');
	size_t length = filename->length;
	*separator = 0;
	filename->length = separator - filename->data;
	int status = create_directory(filename);
	*separator = '/';
	filename->length = length;
	if (status < 0) return status;

finally:
	return creat(filename->data, 0644);
#else
	int i=0;
	struct string *tmp_str=0;
	
	tmp_str=string_alloc(filename->data,filename->length);
	for(i=0;i<tmp_str->length;i++)if(tmp_str->data[i]=='/')tmp_str->data[i]='\\';
	
	
	if(tmp_str->data[tmp_str->length-1]=='\\')tmp_str->data[tmp_str->length-1]=0;
	int file = open(tmp_str->data, O_CREAT | O_WRONLY | O_TRUNC | O_BINARY, 0644);
	
	if (file >= 0){free(tmp_str); return file;}

	switch (errno)
	{
	case ENOENT: // path component does not exist
		break;
	case ENOTDIR: // TODO: change this when there is evfs write support
	default: // TODO: better errors
		{free(tmp_str); return -1; }
	}

	// Create file's parent directory.
	int status = create_directory(filename);
	if (status < 0){free(tmp_str); return status;}

finally:
	// TODO better errors here
	
	i = open(tmp_str->data, O_CREAT | O_WRONLY | O_TRUNC | O_BINARY, 0644);
	free(tmp_str);
	return i;
#endif
}

#if !defined(FAILSAFE)

// TODO: rewrite this function
static struct string *post_filename(const struct string *root, const struct string *content_disposition, bool overwrite)
{
	struct dict options;
	if (!dict_init(&options, DICT_SIZE_BASE)) return 0;
	if (http_parse_content_disposition(&options, content_disposition))
	{
		dict_term(&options);
		return 0;
	}

	struct string field = string("filename");
	struct string *filename = dict_remove(&options, &field);
	dict_term(&options);
	if (!filename) return 0;

	// Keep 5 bytes more than necessary to change filename if necessary. The value is chosen based on INDEX_MAX: (index) = 1 + 3 + 1 = 5

	struct string *path = malloc(sizeof(struct string) + sizeof(char) * (root->length + 1 + filename->length + 5 + 1));
	if (!path)
	{
		free(filename);
		return 0;
	}
	path->length = root->length + 1 + filename->length;
	path->data = (char *)(path + 1);
	memcpy(path->data, root->data, root->length);
#ifdef OS_WINDOWS
	path->data[root->length] = '\\';
#else
	path->data[root->length] = '/';
#endif
	memcpy(path->data + root->length + 1, filename->data, filename->length);
	path->data[path->length] = 0;

	// If overwriting is disabled and such file already exists, choose a different filename.
	// Names are generated by appending (index) before the extension. If there's no extension, (index) is appended at the end
	if (!overwrite && !access(path->data, F_OK))
	{
		// Find extension position
		unsigned index = 1;
		size_t length;
		size_t extension = filename->length - 1;
		do if (filename->data[extension] == '.') goto found;
		while (--extension);
		extension = filename->length;

found:

		#define INDEX_MAX 999

		// Try different filenames until finding a free one.
		// Example: test.c, test(1).c, test(2).c, etc.
		while (true)
		{
			sprintf(path->data + root->length + 1 + extension, "(%u)%s", index, filename->data + extension);
			if (access(path->data, F_OK)) break;

			index += 1;
			if (index == INDEX_MAX)
			{
				free(path);
				free(filename);
				return 0;
			}
		}

		#undef INDEX_MAX
	}

	free(filename);

	return path;
}

static inline void file_finish(struct string *restrict filename, int file, bool keep)
{
	close(file);
	if (!keep) unlink(filename->data);
	free(filename);
}

// TODO: this can be optimized for faster upload using ioctl
static int upload_multipart(const struct string *root, const struct dict *options, struct stream *restrict stream, const struct string *status_key)
{
	struct string buffer, field;

	// Get boundary. We don't need any other options.
	struct string key = string("boundary");
	struct string *boundary = dict_get(options, &key);
	if (!boundary) return UnsupportedMediaType; // TODO: check whether this is the right response

	// Generate pattern
	struct string pattern;
	pattern.length = 2 + boundary->length; // 2 for initial --
	pattern.data = malloc(sizeof(char) * (pattern.length + 1));
	if (!pattern.data) return InternalServerError;
	pattern.data[0] = '-';
	pattern.data[1] = '-';
	memcpy(pattern.data + 2, boundary->data, boundary->length);
	pattern.data[pattern.length] = 0;

	// TODO: check for Expect header
	/*key = string("expect");
	value = dict_get(&request->headers, &key);
	if ()
	buffer = string("HTTP/1.1 100 Continue\r\n");
	stream_write(stream, &buffer) || stream_write_flush(stream);*/

	int status = 0;
	size_t *table = 0;

	size_t progress = 0;
	status_set(status_key, progress, STATE_PENDING);

	if (stream_read(stream, &buffer, pattern.length + 4)) // \r\n and possibly -- before that (marking end of body)
	{
		status = NotFound; // TODO: this should not be not found
		goto finally;
	}

	// Body should start with boundary
	if (memcmp(buffer.data, pattern.data, pattern.length))
	{
		status = BadRequest;
		goto finally;
	}

	stream_read_flush(stream, pattern.length + 2); // There are 2 unnecessary characters after the boundary
	progress += pattern.length + 2;
	status_set(status_key, progress, STATE_PENDING);

	// Generate table for boundary search
	table = kmp_table(&pattern);
	if (!table)
	{
		status = InternalServerError;
		goto finally;
	}

	// TODO: make this work for more general cases

	ssize_t size = 0; // Size of the data before the boundary
	struct dict part_header;
	int file;
	while (memcmp(buffer.data + size + pattern.length, "--\r\n", 4)) // Parse and handle each part of the body
	{
		if ((buffer.data[size + pattern.length] != '\r') || (buffer.data[size + pattern.length + 1] != '\n'))
		{
			status = BadRequest; // TODO: invalid request. is this the right response code?
			goto finally;
		}

		// Check whether filename is specified
		// TODO: use content disposition to determinte what to do
		status = http_parse_header(&part_header, stream);
		if (status) goto finally;
		field = string("content-disposition");
		struct string *content_disposition = dict_get(&part_header, &field);
		if (!content_disposition)
		{
			dict_term(&part_header);
			status = BadRequest; // TODO
			goto finally;
		}
		struct string *filename = post_filename(root, content_disposition, false);
		status_set_name(status_key, filename);

		// Free all part headers and their dictionary
		dict_term(&part_header);

		// TODO: uploading multiple files can lead to undeleted files on error

		// If this part is file contents, download the file to the server. Otherwise, just find the next boundary
		if (filename)
		{
#ifdef OS_BSD
			file = creat(filename->data, 0644);
#else
			file = open(filename->data, O_CREAT | O_WRONLY | O_TRUNC | O_BINARY, 0644);
#endif
			if (file < 0)
			{
				// TODO: maybe just skip current file (don't cancel the whole upload?)
				free(filename);
				status = Forbidden;
				goto finally;
			}

			while (true)
			{
				if (stream_read(stream, &buffer, pattern.length + 4)) // \r\n and possibly -- before that (marking end of body)
				{
					file_finish(filename, file, false);
					status = NotFound; // TODO: this should not be not found
					goto finally;
				}

				// Look for end boundary
				size = kmp_search(&pattern, table, &buffer);
				if (size >= 0)
				{
					// Content should end with \r\n
					// Check for it and don't write it to the file

					if ((buffer.data[size - 2] != '\r') || (buffer.data[size - 1] != '\n'))
					{
						file_finish(filename, file, false);
						status = BadRequest;
						goto finally;
					}

					//if (!writeall(file, buffer.data, size - 2)) ; // TODO: error
					writeall(file, buffer.data, size - 2); // TODO: error
					stream_read_flush(stream, size + pattern.length + 2); // There are 2 unnecessary characters after the boundary
					progress += size + pattern.length + 2;
					status_set(status_key, progress, STATE_PENDING);
					break;
				}
				else
				{
					// Write the data that is guaranteed to be in the content which is
					//  everything except the terminating \r\n and a proper prefix of the boundary
					//  as they can be in the buffer
					//if (!writeall(file, buffer.data, buffer.length - pattern.length - 1)) ; // TODO: error
					writeall(file, buffer.data, buffer.length - pattern.length - 1); // TODO: error
					stream_read_flush(stream, buffer.length - pattern.length - 1);
					progress += buffer.length - pattern.length - 1;
					status_set(status_key, progress, STATE_PENDING);
				}
			}

			file_finish(filename, file, true);
		}
		else while (true)
		{
			// Skip data with no filename specified.

			// TODO: do something with non-file data

			if (stream_read(stream, &buffer, pattern.length + 4)) // \r\n and possibly -- before that (marking end of body)
			{
				status = NotFound; // TODO: this should not be not found
				goto finally;
			}

			// Look for end boundary
			size = kmp_search(&pattern, table, &buffer);
			if (size >= 0)
			{
				// Check for \r\n after the value
				if ((buffer.data[size - 2] != '\r') || (buffer.data[size - 1] != '\n'))
				{
					status = BadRequest;
					goto finally;
				}

				stream_read_flush(stream, size + pattern.length + 2); // There are 2 unnecessary characters after the boundary
				progress += size + pattern.length + 2;
				status_set(status_key, progress, STATE_PENDING);
				break;
			}
			else
			{
				stream_read_flush(stream, buffer.length - pattern.length - 1);
				progress += buffer.length - pattern.length - 1;
				status_set(status_key, progress, STATE_PENDING);
			}
		}
	}

	// Flush the terminating \r\n
	stream_read_flush(stream, 2);
	progress += 2;
	status_set(status_key, progress, STATE_FINISHED);

	if (false)
	{
finally:
		status_set(status_key, progress, STATE_ERROR);
	}

	free(table);
	free(pattern.data);

	return status;
}

int http_upload(const struct string *root, const struct string *filename, const struct http_request *request, struct stream *restrict stream, bool append, bool keep, const struct string *status_key)
{
#if !defined(OS_WINDOWS)
	off_t length = content_length(&request->headers);
#else
	int64_t length = content_length(&request->headers);
#endif
	if (length < 0) return LengthRequired;

	// TODO: send this when necessary (likely unsupported by most browsers)
	//struct string buffer = string("HTTP/1.1 100 Continue\r\n");
	//stream_write(stream, &buffer) || stream_write_flush(stream);

	// If filename is specified, use HTML 5 upload.
	if (filename)
	{
		// Generate path
		struct string *path;
		{
			size_t length = root->length + 1 + filename->length;
			path = malloc(sizeof(struct string) + sizeof(char) * (length + 1));
			if (!path) return InternalServerError; // TODO: memory error
			path->data = (char *)(path + 1);
			path->length = length;

			format_bytes(path->data, root->data, root->length);
			path->data[root->length] = '/';
			format_bytes(path->data + root->length + 1, filename->data, filename->length);
			path->data[length] = 0;
		}

		// TODO better handling for file overwriting (e.g. write to a different file and then rename)

		int file;
		int flags = O_WRONLY | (append ? O_APPEND : (O_CREAT | O_TRUNC));
#if defined(OS_WINDOWS)
		flags |= O_BINARY;
#endif
		file = open(path->data, flags, 0644);
		if (file < 0)
		{
			free(path);
			return Forbidden; // TODO check errno
		}

		int status;
		if (status = transfer(stream, file, length))
		{
			file_finish(path, file, keep);
			return status; // TODO sometimes the connection should be closed in this case
		}

		free(path);
		close(file);

		return OK;
	}
	else
	{
		size_t index = 0;
		struct string *content_type;
		struct string key;

		key = string("content-type");
		content_type = dict_get(&request->headers, &key);
		if (!content_type) return UnsupportedMediaType;

		#define CONTENT_TYPE_LENGTH_MAX 64

		char type[CONTENT_TYPE_LENGTH_MAX];

		// Parse content type
		// Main part's end is marked by end of header field or semicolon
		while ((index < content_type->length) && (content_type->data[index] != ';'))
		{
			// Remember lowercased content type
			type[index] = tolower(content_type->data[index]);
			++index;

			if (index == CONTENT_TYPE_LENGTH_MAX)
				return UnsupportedMediaType;
		}

		#undef CONTENT_TYPE_LENGTH_MAX

		#define CONTENT_TYPE_IS(t) ((index == (sizeof(t) - 1)) && !memcmp(type, (t), (sizeof(t) - 1)))
		if (CONTENT_TYPE_IS("application/x-www-form-urlencoded"))
		{
			return UnsupportedMediaType; // TODO: support this type
		}
		else if (CONTENT_TYPE_IS("multipart/form-data"))
		{
			// This content type requires options
			if (index == content_type->length) return UnsupportedMediaType; // TODO: check whether this is the right response

			// There must be space character before the options
			if ((++index == content_type->length) || (content_type->data[index] != ' ')) return BadRequest;

			struct dict options;

			// Parse options
			if (!dict_init(&options, DICT_SIZE_BASE)) return InternalServerError;
			struct string buffer = string(content_type->data + index + 1, content_type->length - index - 1);
			int status = http_parse_options(&options, &buffer);
			if (status)
			{
				dict_term(&options);
				return status;
			}

			// TODO: content-length is not used here

			status = upload_multipart(root, &options, stream, status_key);

			dict_term(&options);
			return status;
		}
		else return UnsupportedMediaType;
		#undef CONTENT_TYPE_IS
	}
}

int paste_init(struct paste *restrict paste, const struct vector *restrict sources, struct string *restrict destination, const struct string *restrict uuid, unsigned block_id, const struct string *restrict path)
{
	// Decide what paste mode to use.
	// 0				Destination does not exist. The source must be a single entry (not a list of entries). The name of the copy will be destination.
	// EVFS_REGULAR		Destination is a regular file. The source must be a single regular file. The file destination will be overwritten with a copy of source.
	// EVFS_DIRECTORY	Destination is a directory. The files will be copied in destination.
#if !defined(OS_WINDOWS)
	struct stat info;
#else
	struct _stati64 info;
#endif
	if (stat(destination->data, &info) < 0)
	{
		if (sources->length > 1) return ERROR_MISSING; // no sensible way to name destination files

		// The target directory does not exist. Check whether its parent exists.
		if (errno == ENOENT)
		{
			// Find where the last path component starts.
			size_t start = destination->length - 1;
			if (!start) return ERROR_MISSING;
#if !defined(OS_WINDOWS)
			while (destination->data[start] != '/') start -= 1;
#else
			while ((destination->data[start] != '/') && (destination->data[start] != '\\')) start -= 1;
#endif

			destination->data[start] = 0;
			if (stat(destination->data, &info) < 0) return ERROR_MISSING;
			destination->data[start] = '/';
		}
		else return NotFound; // TODO
		// TODO change this when writing to archives is supported (ENODIR should be handled)

		paste->mode = 0;
	}
	else switch (info.st_mode & S_IFMT)
	{
	case S_IFREG:
		// Source must be a single entry. Otherwise there is no sensible place to copy the files.
		// TODO change this when writing to archives is supported (ENODIR should be handled)
		if (sources->length > 1) return ERROR_UNSUPPORTED; // no sensible way to name destination files

		paste->mode = EVFS_REGULAR;
		break;
	case S_IFDIR:
		paste->mode = EVFS_DIRECTORY;
		break;
	default:
		return ERROR_UNSUPPORTED;
	}

	paste->dev = info.st_dev;
	paste->destination = destination;

	paste->flags = 0;

	// Initialize cache data and create cache.
	union json *progress = json_object();
	while (progress)
	{
		struct string key;
		if (key = string("source"), json_object_insert_old(progress, &key, json_string_old(uuid))) break;
		if (key = string("block_id"), json_object_insert_old(progress, &key, json_integer(block_id))) break;
		if (key = string("path"), json_object_insert_old(progress, &key, json_string_old(path))) break;
		if (key = string("size"), json_object_insert_old(progress, &key, json_integer(0))) break;
		if (key = string("status"), json_object_insert_old(progress, &key, json_integer(STATUS_RUNNING))) break;

		paste->operation_id = operation_start();
		if (paste->operation_id < 0) break;
		if (key = string("_oid"), json_object_insert_old(progress, &key, json_integer(paste->operation_id)))
		{
			operation_end(paste->operation_id);
			break;
		}

		if (!cache_create(paste->progress, CACHE_PROGRESS, progress, 0))
		{
			operation_end(paste->operation_id);
			break;
		}
		return 0;
	}

	json_free(progress);
	return ERROR_MEMORY;
}

int paste_write(void *argument, unsigned char *buffer, unsigned total)
{
	struct paste *paste = argument;

	struct cache *cache;
	struct string key = string("size");
	union json *progress;

# if defined(DEVICE)
	size_t chunk;
# endif

	size_t index;
	ssize_t size;
	for(index = 0; index < total; index += size)
	{
# if defined(DEVICE)
		chunk = total - index;
		size = write(paste->fd, buffer + index, (chunk > CHUNK_MAX ? CHUNK_MAX : chunk));
		if (size < 0) return ERROR_WRITE;

		// Update progress for current file.
		if (cache = cache_load(paste->progress))
		{
			progress = dict_get(cache->value->object, &key);
			progress->integer += size;
			cache_save(paste->progress, cache);

			if (!operation_progress(paste->operation_id)) return ERROR_CANCEL;
		}
# else
		size = write(paste->fd, buffer + index, total - index);
		if (size < 0) return ERROR_WRITE;
# endif
	}

	return 0;
}
#else /* FAILSAFE */
# if !defined(OS_WINDOWS)
off_t content_length(const struct dict *restrict headers)
{
	struct string key = string("content-length");
	struct string *content_length = dict_get(headers, &key);
	if (!content_length) return ERROR_MISSING;

	char *end;
	off_t length = strtoll(content_length->data, &end, 10);
	if (end != (content_length->data + content_length->length)) return ERROR_INPUT;

	return length;
}
# else
int64_t content_length(const struct dict *restrict headers)
{
	struct string key = string("content-length");
	struct string *content_length = dict_get(headers, &key);
	if (!content_length) return ERROR_MISSING;

	char *end;
	int64_t length = strtoll(content_length->data, &end, 10);
	if (end != (content_length->data + content_length->length)) return ERROR_INPUT;

	return length;
}
# endif

int http_errno_status(int error)
{
	switch (error)
	{
	case 0:
		return OK;
	case EACCES:
	case EROFS:
	case EEXIST: // TODO: is this right ?
		return Forbidden;
	case ELOOP: // TODO: is this right ?
	case ENAMETOOLONG:
	case ENOENT:
	case ENOTDIR:
		return NotFound;
	case ETIMEDOUT:
		return RequestTimeout;
	default:
		return InternalServerError;
	}
}
#endif

// WARNING: paste->destination must be NUL-terminated
int paste_create(const struct string *restrict name, unsigned char type, struct paste *restrict paste)
{
	// assert(name->length);
	// assert(*name->data != '/');

	char *start;
	struct string path;
	path.data = 0;

	struct string relative = *name;

	// TODO look for force. if not set, ask to overwrite when a file exists

	// Generate appropriate destination path, based on mode.
	switch (paste->mode)
	{
	case EVFS_REGULAR:
		// TODO look for force. if not set, ask to overwrite
		return create_file(paste->destination);

	default:
		// Skip first component of file path.
		do
		{
			relative.data += 1;
			relative.length -= 1;
		} while (relative.length && (relative.data[-1] != '/'));
	case EVFS_DIRECTORY:
		// Generate absolute path for the copy.
		path.length = paste->destination->length;
		if (relative.length) path.length += 1 + relative.length;
		path.data = malloc(path.length + 1);
		if (!path.data) return ERROR_MEMORY;
	}
	start = format_bytes(path.data, paste->destination->data, paste->destination->length);
	if (relative.length)
	{
		*start++ = '/';
		start = format_bytes(start, relative.data, relative.length);
	}
	*start = 0;

	if (type == EVFS_REGULAR) paste->fd = create_file(&path);
	else // EVFS_DIRECTORY
	{
		create_directory(&path); // TODO check return value
		paste->fd = 0;
	}
	free(path.data);
	return paste->fd;
}

// WARNING: paste->destination must be NUL-terminated
int paste_rename(const struct paste *restrict paste)
{
	// assert(paste->source->length);

	char *start;
	struct string path;
	struct string relative;

	// Generate appropriate destination path, based on mode.
	path.length = paste->destination->length;
	switch (paste->mode)
	{
	case EVFS_DIRECTORY:
		// Retrieve the last component of source path.
		{
			size_t index;
			for(index = paste->source->length; index; --index)
				if (paste->source->data[index - 1] == '/')
					break;
			relative = string(paste->source->data + index, paste->source->length - index);
		}
		path.length += 1 + relative.length;
		break;

	case EVFS_REGULAR:
		// TODO look for force. if not set, ask to overwrite
		unlink(paste->destination->data);
	default:
		// Destination file name is provided. Ignore source file name.
		relative = string("");
	}

	// Generate absolute path for the destination.
	path.data = malloc(path.length + 1);
	if (!path.data) return ERROR_MEMORY;
	start = format_bytes(path.data, paste->destination->data, paste->destination->length);
	if (relative.length)
	{
		*start++ = '/';
		start = format_bytes(start, relative.data, relative.length);
	}
	*start = 0;

	int status;
	if (!rename(paste->source->data, path.data)) status = 0;
	else status = errno_error(errno);
	free(path.data);
	return status;
}

// Splits string into 2 parts and returns pointer to the second one.
static char *rsplit(char *start, size_t length, char separator)
{
	while (length--)
		if (start[length] == separator)
			return start + length + 1;
	return start;
}

static int http_transfer_chunked(struct stream *restrict input, struct paste *restrict copy)
{
#if defined(DEVICE) || defined(PUBCLOUD)
	// TODO we assume certain layout of the chunks which is not good

	static const char terminator[2] = "\r\n";

#if !defined(OS_WINDOWS)
	off_t size_chunk, size_left, size;
#else
	int64_t size_chunk, size_left, size;
#endif
	
	enum {Header, Metadata, Filename, Size, Data} state = Header;

	struct metadata metadata;
	int output;

	uint64_t size_total;

	bool first = true;

	struct string buffer;
	size_t index;

	struct cache *cache;
	union json *field;
	struct string key;

	// TODO check validity of each archive field. for example now an archive can contain file with .. in its path

	while (true)
	{
		// Get chunk size.
		if (stream_read(input, &buffer, sizeof(terminator) + 1)) return -1; // TODO
		if (!isxdigit(buffer.data[0])) return ERROR_INPUT; // TODO invalid chunk format
		index = 2;
		while (true)
		{
			if (buffer.data[index] == terminator[1])
			{
				if (buffer.data[index - 1] == terminator[0]) break; // chunk size terminator found
				else return ERROR_INPUT; // TODO invalid chunk format
			}
			else if (!isxdigit(buffer.data[index - 1])) return ERROR_INPUT; // TODO invalid chunk format

			// Abort if chunk size is too big to be file size.
			if (index >= (8 * 2)) return ERROR_INPUT; // TODO

			// Read more data if necessary.
			if (++index >= buffer.length)
				if (stream_read(input, &buffer, buffer.length + 1))
					return -1; // TODO
		}

		// Previous checks ensure that chunk size is valid.
		size_chunk = strtoll(buffer.data, 0, 16);
		stream_read_flush(input, index + 1);

		if (!size_chunk) // last chunk
		{
			if (state == Metadata) break;
			else return -1; // TODO
		}

		switch (state)
		{
		case Header:
			if (size_chunk != 8) return -1; // TODO
			if (stream_read(input, &buffer, 8)) return -1; // TODO
			stream_read_flush(input, 8);
			state = Metadata;
			break;

		case Metadata:
			if (size_chunk != sizeof(metadata)) return -1; // TODO
			if (stream_read(input, &buffer, sizeof(metadata))) return -1; // TODO
			if (!metadata_parse(&metadata, buffer.data)) return -1; // TODO

			stream_read_flush(input, sizeof(metadata));
			state = Filename;
			break;

		case Filename:
			if (size_chunk != metadata.path_length) return -1; // TODO
			if (stream_read(input, &buffer, metadata.path_length)) return -1; // TODO

			{
				struct file file;
				file.name = string(buffer.data, metadata.path_length);
				switch (metadata.mode & ~((1 << 12) - 1))
				{
				case EARCHIVE_REGULAR:
					file.type = EVFS_REGULAR;
					break;
				case EARCHIVE_DIRECTORY:
					file.type = EVFS_DIRECTORY;
					break;
				default:
					return -1; // TODO
				}
				if (first)
				{
					// No sensible way to copy if the root source entry is a directory and the destination is a regular file.
					if ((file.type == EVFS_DIRECTORY) && (copy->mode == EVFS_REGULAR)) return -1; // TODO
					first = false;
				}
				output = paste_create(&file.name, file.type, copy);
				if (output < 0) return output;
			}

			if (metadata.offset) state = Size;
			else state = Metadata;

			stream_read_flush(input, metadata.path_length);
			break;

		case Size:
			if (size_chunk != sizeof(size_total)) return -1; // TODO
			if (stream_read(input, &buffer, sizeof(size_total))) return -1; // TODO
			size_total = earchive_size_parse(buffer.data);

			stream_read_flush(input, sizeof(size_total));
			state = Data;
			break;

		case Data:
			if (size_chunk > size_total) return -1; // TODO

			// Write chunk body.
			size_left = size_chunk;
			while (size_left)
			{
				size = ((size_left > BUFFER_SIZE_MAX) ? BUFFER_SIZE_MAX : size_left);
				if (stream_read(input, &buffer, size)) return -1; // TODO
				if (!writeall(output, buffer.data, size)) return -1; // TODO
				stream_read_flush(input, size);
				size_left -= size;

				if (cache = cache_load(copy->progress))
				{
					key = string("size");
					field = dict_get(((union json *)cache->value)->object, &key);
					field->integer += size;
					cache_save(copy->progress, cache);

					if (!operation_progress(copy->operation_id)) return ERROR_CANCEL;
				}
			}
			size_total -= size_chunk;

			if (!size_total)
			{
				state = Metadata;
				close(output);
			}
			break;
		}

		// Check whether there is a valid chunk terminator.
		if (stream_read(input, &buffer, sizeof(terminator))) return -1; // TODO
		if ((buffer.data[0] != terminator[0]) || (buffer.data[1] != terminator[1])) return ERROR_INPUT; // TODO invalid chunk format
		stream_read_flush(input, sizeof(terminator));
	}
#endif

	return 0;
}

static inline size_t http_request_length(const struct string *restrict method, const struct string *restrict host, const struct string *restrict uri)
{
	return method->length + 1 + uri->length + request_middle.length + host->length + request_end.length;
}
static char *http_request(char *restrict request, const struct string *restrict method, const struct string *restrict host, const struct string *restrict uri)
{
	request = format_bytes(request, method->data, method->length);
	*request++ = ' ';
	request = format_bytes(request, uri->data, uri->length);
	request = format_bytes(request, request_middle.data, request_middle.length);
	request = format_bytes(request, host->data, host->length);
	return format_bytes(request, request_end.data, request_end.length);
}

static int http_response(struct stream *restrict input, const struct string *restrict request, short version[restrict 2], struct dict *restrict headers)
{
	struct string buffer;
	unsigned code;
	int status;

	status = stream_write(input, request);
	if (!status) status = stream_write_flush(input);
	if (status) return status;

	if (http_parse_version(version, input)) return ERROR_INPUT;

	// Read at least as much characters as the shortest valid status string
	if (status = stream_read(input, &buffer, sizeof(" ??? \r\n") - 1)) return status;

	if ((buffer.data[0] != ' ') || !isdigit(buffer.data[1]) || !isdigit(buffer.data[2]) || !isdigit(buffer.data[3]) || (buffer.data[4] != ' '))
		return ERROR_INPUT;

	code = strtol(buffer.data + 1, 0, 10); // previous checks assure that this operation is always successful 
	if (code >= 400) return ERROR_INPUT;

	stream_read_flush(input, 5);

	#define RESPONSE_LENGTH_MAX 256

	// Find the end of the response line
	size_t index = 1;
	while (true)
	{
		if (status = stream_read(input, &buffer, index + 1)) return status;

next:
		if (buffer.data[index] == '\n')
		{
			if (buffer.data[index - 1] == '\r') break;
			else return ERROR_INPUT;
		}

		if (index >= RESPONSE_LENGTH_MAX) return ERROR_INPUT;
		if (++index < buffer.length) goto next;
	}
	stream_read_flush(input, index + 1);

	#undef RESPONSE_LENGTH_MAX

	if (status = http_parse_header(headers, input)) return status;

	return 0;
}

// WARNING: copy->destination must be NUL-terminated
static int http_transfer_internal(struct stream *restrict input, const struct dict *restrict headers, struct paste *restrict copy, struct transfer_context *restrict context)
{
	int status;

	struct string buffer;
	struct cache *cache;
	union json *field;

	struct string key, *value;

#if !defined(OS_WINDOWS)
	off_t length = content_length(headers);
#else
	int64_t length = content_length(headers);
#endif
	if (length == ERROR_INPUT) return ERROR_INPUT;

	key = string("content-type");
	value = dict_get(headers, &key);
	if (!value) return ERROR_INPUT;

	// Just transfer the source file if it doesn't require any additional manipulation.
	key = string("application/earchive");
	if (!string_equal(value, &key))
	{
		if (length < 0) return length;

		if (context->output < 0)
		{
			char *start = rsplit(copy->source->data, copy->source->length, '/');
#if defined(OS_WINDOWS)
			char *start1 = rsplit(copy->source->data, copy->source->length, '\\');
			if (start1 > start) start = start1;
#endif
			struct string basename = string(start, copy->source->data + copy->source->length - start);

			if (paste_create(&basename, EVFS_REGULAR, copy) < 0) return context->output;
			context->output = copy->fd;
		}

#if !defined(FAILSAFE)
		key = string("size");
#endif

		while (length = context->total - context->progress)
		{
			//debug(logs("before "), logi(length), logs(" | done "), logi(context->progress));
			if (status = stream_read(input, &buffer, (length > BUFFER_SIZE_MAX) ? BUFFER_SIZE_MAX : length))
			{
				debug(logs("now"));
				return status;
			}
			if (!writeall(context->output, buffer.data, buffer.length)) return ERROR_WRITE;
			stream_read_flush(input, buffer.length);
			context->progress += buffer.length;

#if !defined(FAILSAFE)
			if (cache = cache_load(copy->progress))
			{
				field = dict_get(((union json *)cache->value)->object, &key);
				field->integer += buffer.length;
				cache_save(copy->progress, cache);

				if (!operation_progress(copy->operation_id)) return ERROR_CANCEL;
			}
#endif
		}
		return 0;
	}

	// TODO remove this
	// Assume that response uses chunked transfer-encoding if there is no content-length header. This is for compatibility with versions < 0.18.
	if (length == ERROR_MISSING) return http_transfer_chunked(input, copy);

#if defined(DEVICE) || defined(PUBCLOUD)

	// TODO this is broken for archives containing a single file
	// No sensible way to copy if the root source entry is a directory and the destination is a regular file.
	if (copy->mode == EVFS_REGULAR) return ERROR_UNSUPPORTED;

	struct file file;
	#if !defined(OS_WINDOWS)
	off_t size;
	#else
	int64_t size;
	#endif

	// TODO check validity of each archive field. for example now an archive can contain file with .. in its path

	// TODO check whether error codes always behave as expected

	while (true)
	{
		// TODO merge this if block into the while loop
		if (context->progress == context->total) // end of archive
		{
			if (context->state == Metadata) break;
			else return ERROR_INPUT;
		}

		switch (context->state)
		{
		case Header:
			if (status = stream_read(input, &buffer, 8)) return status;
			stream_read_flush(input, 8);
			context->progress += 8;
			context->state = Metadata;
			break;

		case Metadata:
			if (status = stream_read(input, &buffer, sizeof(context->metadata))) return status;
			if (!metadata_parse(&context->metadata, buffer.data)) return ERROR_INPUT;

			stream_read_flush(input, sizeof(context->metadata));
			context->progress += sizeof(context->metadata);
			context->state = Filename;
			break;

		case Filename:
			// TODO this should be executed only if there is a valid cache (otherwise device upgrade whould crash here)
			if (!operation_progress(copy->operation_id)) return ERROR_CANCEL;

			if (status = stream_read(input, &buffer, context->metadata.path_length)) return status;

			switch (context->metadata.mode & ~((1 << 12) - 1))
			{
			case EARCHIVE_REGULAR:
				file.type = EVFS_REGULAR;
				break;
			case EARCHIVE_DIRECTORY:
				file.type = EVFS_DIRECTORY;
				break;
			default:
				return ERROR_UNSUPPORTED;
			}
			file.name = string(buffer.data, context->metadata.path_length);

			if (paste_create(&file.name, file.type, copy) < 0) return context->output;
			context->output = copy->fd;

			stream_read_flush(input, context->metadata.path_length);
			context->progress += context->metadata.path_length;
			context->state = (context->metadata.offset ? Size : Metadata);
			break;

		case Size:
			if (status = stream_read(input, &buffer, sizeof(context->size_left))) return status;
			context->size_left = earchive_size_parse(buffer.data);

			stream_read_flush(input, sizeof(context->size_left));
			context->progress += sizeof(context->size_left);
			context->state = Data;
			break;

		case Data:
			// Write file data.
			do
			{
				size = ((context->size_left > BUFFER_SIZE_MAX) ? BUFFER_SIZE_MAX : context->size_left);
				if (status = stream_read(input, &buffer, size)) return status;
				if (!writeall(context->output, buffer.data, size)) return ERROR_WRITE;
				stream_read_flush(input, size);

				context->progress += size;
				context->size_left -= size;
				if (cache = cache_load(copy->progress))
				{
					key = string("size");
					field = dict_get(((union json *)cache->value)->object, &key);
					field->integer += size;
					cache_save(copy->progress, cache);

					if (!operation_progress(copy->operation_id)) return ERROR_CANCEL;
				}
			} while (context->size_left);

			context->state = Metadata;
			close(context->output);
			context->output = -1;
			break;
		}
	}
#endif

	return 0;
}

int http_transfer(struct stream *restrict input, const struct string *restrict host, const struct string *restrict uri, struct paste *restrict copy)
{
	struct string request, method = string("GET");
	short version[2];
	struct dict headers;
	int status;

	request.data = malloc(http_request_length(&method, host, uri));
	if (!request.data) return ERROR_MEMORY;
	request.length = http_request(request.data, &method, host, uri) - request.data;
	status = http_response(input, &request, version, &headers);
	free(request.data);

	if (status < 0)
		return status;

	struct transfer_context context;
	context.output = -1;
	context.progress = 0;
	context.total = content_length(&headers);
	if (context.total == ERROR_INPUT)
	{
		dict_term(&headers);
		return ERROR_INPUT;
	}

	if (!status) status = http_transfer_internal(input, &headers, copy, &context);
	dict_term(&headers);

	return status;
}

// Perform HTTP transfer even if the network connection fails.
int http_transfer_persist(const struct string *restrict host, unsigned port, const struct string *restrict uri, struct paste *restrict copy)
{
	struct string key = string("last-modified"), *value;
	struct string request, range = string("Range: bytes=");
	char *start, *temp;

	int fd;
	struct stream input;

	int status;

	// Generate request string. When allocating memory make sure it is enough to hold an additional range header.
	struct string head = string("HEAD"), get = string("GET");
	char *buffer = malloc(http_request_length(&head, host, uri) + range.length + 8 * 3 + 1 + sizeof("\r\n") - 1);
	if (!buffer) return ERROR_MEMORY;
	request = string(buffer, http_request(buffer, &head, host, uri) - buffer);
	start = request.data + request.length - 2;

	fd = socket_connect(host->data, port);
	if (fd < 0)
	{
		free(buffer);
		return fd;
	}
	if (status = stream_init(&input, fd))
	{
#if !defined(OS_WINDOWS)
		close(fd);
#else
		CLOSE(fd);
#endif
		free(buffer);
		return status;
	}

	// Make sure transfer is possible and ranges are supported.
	short version[2];
	struct dict headers;
	status = http_response(&input, &request, version, &headers);
	if (status) goto finally;
	#if !defined(OS_WINDOWS)
	off_t length = content_length(&headers);
	#else
	int64_t length = content_length(&headers);
	#endif
	if (length == ERROR_INPUT)
	{
		dict_term(&headers);
		status = ERROR_INPUT;
		goto finally;
	}

	struct string *last_modified = dict_remove(&headers, &key);
	dict_term(&headers);

	// Change request method to GET.
	request.data += head.length - get.length;
	request.length -= head.length - get.length;
	format_bytes(request.data, get.data, get.length);

	struct transfer_context context;
	context.state = Header;
	context.output = -1;
	context.progress = 0;
	context.total = length;

	// TODO remove this when support for < 0.18 is dropped
	// Just try to download the file if ranges are not supported. Versions < 0.18 don't send content-length in HEAD requests.
	if (length == ERROR_MISSING)
	{
		free(last_modified);

		stream_term(&input);
#if !defined(OS_WINDOWS)
		close(fd);
#else
		CLOSE(fd);
#endif

		fd = socket_connect(host->data, port);
		if (fd < 0)
		{
			free(buffer);
			return fd;
		}
		if (status = stream_init(&input, fd))
		{
#if !defined(OS_WINDOWS)
			close(fd);
#else
			CLOSE(fd);
#endif
			free(buffer);
			return status;
		}

		status = http_response(&input, &request, version, &headers);
		if (status) goto finally;
		context.total = content_length(&headers);
		status = http_transfer_internal(&input, &headers, copy, &context);
		close(context.output);
		goto finally;
	}

	start = format_bytes(start, range.data, range.length);
	range = string("content-range");

	while (true)
	{
		// Set request range.
		temp = format_uint(start, context.progress);
		*temp++ = '-';
		request.length = format_bytes(temp, request_end.data, request_end.length) - request.data;

		status = http_response(&input, &request, version, &headers);
		if (!status)
		{
			// TODO check content-range and content-length value
			value = dict_get(&headers, &key);
			if (value && string_equal(last_modified, value) && dict_get(&headers, &range))
				status = http_transfer_internal(&input, &headers, copy, &context);
			// TODO else
			dict_term(&headers);

			switch (status)
			{
			case ERROR_AGAIN:
			case ERROR_RESOLVE:
			case ERROR_NETWORK:
				break;
			default:
				close(context.output);
				free(last_modified);
				goto finally;
			}
		}

		stream_term(&input);
#if !defined(OS_WINDOWS)
		close(fd);
#else
		CLOSE(fd);
#endif

retry:
		sleep(10); // TODO fix sleep amount

		fd = socket_connect(host->data, port);
		if (fd < 0) goto retry;
		if (status = stream_init(&input, fd))
		{
#if !defined(OS_WINDOWS)
			close(fd);
#else
			CLOSE(fd);
#endif
			goto retry;
		}
	}

finally:
	stream_term(&input);
#if !defined(OS_WINDOWS)
	close(fd);
#else
	CLOSE(fd);
#endif
	free(buffer);
	return status;
}
