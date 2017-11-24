#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdio.h>

#if !defined(OS_WINDOWS)
# include <arpa/inet.h>
# include <dirent.h>
# include <sys/mman.h>
# include <sys/socket.h>
#else
# define WINDOWS_BLOCK_SIZE 0x400000
# include <windows.h>
# include <winsock2.h>
# include "../../windows/mingw.h"
#endif

#include "types.h"
#include "format.h"
#include "json.h"
#include "protocol.h"
#include "stream.h"
#include "http.h"
#include "http_parse.h"
#include "http_response.h"
#include "evfs.h"
#include "cache.h"
#include "server.h"
#include "io.h"
#include "access.h"
#include "magic.h"

#define sign4(b3, b2, b1, b0)					(((b3) << 24) | ((b2) << 16) | ((b1) << 8) | (b0))

#if !defined(OS_WINDOWS)
// TODO think whether this could block a thread. maybe check for EINTR
// Make sure allocated resources are always freed.
/*# define close(file) do ; while (close(file) < 0)
# define closedir(file) do ; while (closedir(file) < 0)
# define munmap(buffer, size) do ; while (munmap(buffer, size) < 0)*/
#endif

// TODO reading mmap()ed memory can product SIGBUS on filesystem corruption:
//  https://mail.gnome.org/archives/mc-devel/2001-September/msg00104.html

// TODO mtime must be time_t (so that it can be passed to gmtime_r())

#if defined(OS_WINDOWS)
# define evfs_extract(...) ERROR_UNSUPPORTED
# include "../windows/evfs_windows.c"
#endif

#define KiB * 1024
#define MiB * 1024 KiB
#define GiB * 1024 MiB

#define BUFFER_COMPAT (512 MiB)

// WARNING: This library assumes that there are no 2 files with the same name.

// Extended Virtual FileSystem
// File system abstraction. Hides the differences between filesystems, archives and compressed files.

// TODO think whether to follow symbolic links:
//  - follow
//  - flag that changes behavior
//  - don't follow
//  in the last 2 cases I should support links as a separate file type

// TODO look for optimizations:
//  do mmap only if file content is requested
//  find central directory of zip files faster
//  cache archive lists (with unlimited depth) to avoid browsing the whole archive each time

// TODO ?reverse browsing (to delete directories during move and remove)

/*
TODO: implement these actions with evfs:

media_stream, media_info, stat									evfs_browse
upload, delete, transfer, mkdir									some write function

write support

TODO: what's the purpose of mkfile?
*/

// TODO: support soft links, etc.

// TODO bzip2
/*case betoh32(sign4('B', 'Z', 'h', '1')):
case betoh32(sign4('B', 'Z', 'h', '2')):
case betoh32(sign4('B', 'Z', 'h', '3')):
case betoh32(sign4('B', 'Z', 'h', '4')):
case betoh32(sign4('B', 'Z', 'h', '5')):
case betoh32(sign4('B', 'Z', 'h', '6')):
case betoh32(sign4('B', 'Z', 'h', '7')):
case betoh32(sign4('B', 'Z', 'h', '8')):
case betoh32(sign4('B', 'Z', 'h', '9')):
	// TODO bzip2
	break;*/

// TODO: stat vs lstat ? (O_NOFOLLOW ?)

const struct string separator = {.data = SEPARATOR, .length = 1};

// Checks whether name is the current directory or the parent directory.
static inline bool skip(const struct string *restrict name)
{
	return ((name->data[0] == '.') && ((name->length == 1) || ((name->length == 2) && (name->data[1] == '.'))));
}

#if !defined(OS_WINDOWS)

static int extract_write(void *argument, unsigned char *buffer, unsigned size)
{
	int *file = argument;
	if (!writeall(*file, buffer, size)) return ERROR_WRITE;
	else return 0;
}

// TODO think about this function - it's kind of complicated considering how it's used
// TODO free on longjmp: fd, cache_destroy(filename + CACHE_INDEX)
int evfs_extract(const struct string *restrict path, const struct file *restrict file, struct string *restrict archive)
{
	int fd;

	#define TEMP_PREFIX "/tmp/filement_"
	#define CACHE_INDEX (sizeof(TEMP_PREFIX) - 1)
	char filename[CACHE_INDEX + CACHE_KEY_SIZE + 1];
	format_bytes(filename, TEMP_PREFIX, CACHE_INDEX);

	// Retrieve extracted file from cache. If there is no cache, create one.
	const char *key = cache_key(CACHE_FILE, path);
	if (key)
	{
		*format_bytes(filename + CACHE_INDEX, key, CACHE_KEY_SIZE) = 0;
		fd = open(filename, O_RDONLY);
		if (fd < 0) return errno_error(errno);
	}
	else
	{
		union json *name = json_string(path->data, path->length); // TODO make name not denounce filesystem path
		if (!name) return ERROR_MEMORY;
		if (!cache_create(filename + CACHE_INDEX, CACHE_FILE, name, 0)) // TODO set expire time
		{
			json_free(name);
			return ERROR_MEMORY;
		}
		filename[CACHE_INDEX + CACHE_KEY_SIZE] = 0;

#if !defined(OS_WINDOWS)
		fd = open(filename, O_CREAT | O_TRUNC | O_RDWR, 0600);
#else
		fd = open(filename, O_CREAT | O_TRUNC | O_BINARY | O_RDWR, 0600);
#endif
		if (fd < 0)
		{
			cache_destroy(filename + CACHE_INDEX);
			return errno_error(errno);
		}

		if (evfs_file(file, extract_write, &fd))
		{
			close(fd);
			unlink(filename);
			cache_destroy(filename + CACHE_INDEX);
			return ERROR_UNSUPPORTED; // TODO this is not always the right error
		}

		// TODO maybe check whether extracted size matches

		// TODO the cache may be added by another thread in the meantime
		int status = cache_enable(CACHE_FILE, path, filename + CACHE_INDEX);
		if (status)
		{
			// TODO handle this
			return status;
		}
	}

	archive->data = mmap(0, file->size, PROT_READ, MAP_PRIVATE, fd, 0); // TODO this may fail
	close(fd);
	if (archive->data == MAP_FAILED)
	{
		// TODO cache_disable() ?
		cache_destroy(filename + CACHE_INDEX); // TODO is it right to always destroy cache?
		return errno_error(errno);
	}
	archive->length = file->size;

	#undef TEMP_PREFIX

	return 0;
}
#endif

// Detects malicious paths.
// TODO this may not be necessary since archives examine each path component separately
// TODO check for two consecutive slashes
bool path_malicious(const struct string *restrict filename)
{
	if (!filename->length || (filename->data[0] == '/')) return true;

	size_t index, length;
	for(index = 0; index < filename->length; ++index)
		if ((filename->data[index] == '.') && (!index || (filename->data[index - 1] == '/'))) // dotfile
		{
			length = filename->length - index;
			if ((length == 1) || (filename->data[index + 1] == '/') || ((filename->data[index + 1] == '.') && ((length == 2) || (filename->data[index + 2] == '/'))))
				return true;
		}

	return false;
}

#if !defined(OS_WINDOWS)
// Recognizes archive format and returns pointer to protocol handler function (0 if the format is not recognized). The field _content must be set.
// If the file is compressed, size, _content and _encoding will be set appropriately.
evfs_protocol_t evfs_recognize(struct file *restrict file) // TODO maybe 2 different arguments for source and destination
{
	struct string *content = &file->_content;

	// Perform recursive browsing if such is possible and necessary.
	if (content->length < MAGIC_SIZE) return 0;

	// Detect archive format.
	// Compare magic values using the host's endian.
	uint32_t magic;
	endian_big32(&magic, content->data);
	switch (magic)
	{
	case ZIP_LOCALFILE_SIGNATURE:
	case ZIP_CENTRALDIR_END_SIGNATURE: // empty ZIP archive
		return evfs_zip;

	case 0x504b0708:
		// TODO: support spanned zip archives
		break;

	/*case betoh32(0x00020000):
		return evfs_earchive;*/

	default:
		// Check whether this is a gzip file.
		// Treat all gzip files as compressed tar files.
		if ((magic >> 16) == 0x1f8b)
		{
			size_t index = sizeof(uint16_t);
			size_t length, length_limit;
			ssize_t left;

			// Read the gzip header.
			// assert(sizeof(header) == 8);
			struct
			{
				// char id[2];
				char compression;
				char flags;
				char mtime[4];
				char xfl;
				char os;
			} header;
			format_bytes((char *)&header, content->data + index, sizeof(header));
			index += sizeof(header);

			#define GZIP_FOOTER 8

			if (header.flags & 0xe0) break; // not a valid gzip file
			if (content->length < GZIP_FOOTER) break; // not a valid gzip file
			if (header.compression != Z_DEFLATED) break; // unsupported compression

			// Exclude gzip footer and extract crc and file size from it (in little endian).
			// WARNING: size is calculated right only if < 2^32
			content->length -= GZIP_FOOTER;
			unsigned char *footer = content->data + content->length;

			#undef GZIP_FOOTER

			// TODO check whether this is a good limit
			#define GZIP_FIELD_LENGTH_LIMIT 4095

			// Find where compressed content starts.
			if (header.flags & (1 << 2)) index += 2 + (content->data[index] | (content->data[index + 1] << 8)); // extra field
			if (header.flags & (1 << 3)) // name field
			{
				left = content->length - index;
				if (left <= 0) break; // not a valid gzip file
				length_limit = ((left > GZIP_FIELD_LENGTH_LIMIT) ? GZIP_FIELD_LENGTH_LIMIT : left);
				length = strnlen(content->data + index, length_limit + 1);
				if (length > length_limit) break; // file name too long
				index += length + 1;
			}
			if (header.flags & (1 << 4)) // comment field
			{
				left = content->length - index;
				if (left <= 0) break; // not a valid gzip file
				length_limit = ((left > GZIP_FIELD_LENGTH_LIMIT) ? GZIP_FIELD_LENGTH_LIMIT : left);
				length = strnlen(content->data + index, length_limit + 1);
				if (length > length_limit) break; // comment too long
				index += length + 1;
			}
			if (header.flags & (1 << 1)) index += 2; // crc field
			left = content->length - index;
			if (left < 0) break; // not a valid gzip file

			#undef GZIP_FIELD_LENGTH_LIMIT

			// Update file information.
			// TODO gzip files will return wrong size if size >= 4GiB
			content->data += index;
			content->length = left;
			file->_encoding = EVFS_ENCODING_DEFLATED;
			file->_crc = footer[0] | (footer[1] << 8) | (footer[2] << 16) | (footer[3] << 24);
			file->size = footer[4] | (footer[5] << 8) | (footer[6] << 16) | (footer[7] << 24);

			return evfs_tar;
		}

		// Check whether the file is likely to be tar archive. If so, handle it as such.
		// Don't recognize empty tar archives as this can cause more problems than it is useful.

		if (file->_content.length % TAR_BLOCK_SIZE) break; // size must be a multiple of TAR_BLOCK_SIZE
		if (file->_content.length < TAR_BLOCK_SIZE * 3) break; // size must be at least 3 blocks
		if (tar_field(content->data, 100) <= 0) break; // check whether the first name field is valid

		return evfs_tar;
	}

	return 0;
}
#endif

// TODO free on longjmp: entry, closedir(dir)
#if !defined(OS_WINDOWS)
static int evfs_directory(const struct string *restrict location, unsigned depth, struct buffer *restrict cwd, evfs_callback_t callback, void *argument, unsigned flags)
#else
int evfs_directory(const struct string *restrict location, unsigned depth, struct buffer *restrict cwd, evfs_callback_t callback, void *argument, unsigned flags)
#endif
{
#if !defined(OS_WINDOWS)
	struct dirent *entry;
	struct stat info;
# if !defined(READDIR)
	struct dirent *more;
# endif
#else
	struct dirent entry, *more;
	struct _stati64 info;
#endif
	struct string filename;

	// TODO find a good way to work-around this nul termination fix
	cwd->data[cwd->length - 1] = 0;
	DIR *dir = opendir(cwd->data);
	if (!dir)
	{
		cwd->data[cwd->length - 1] = *SEPARATOR;
		return errno_error(errno);
	}
#if !defined(READDIR) && !defined(OS_WINDOWS)
	entry = malloc(offsetof(struct dirent, d_name) + pathconf(cwd->data, _PC_NAME_MAX) + 1);
#endif
	cwd->data[cwd->length - 1] = *SEPARATOR;
#if !defined(READDIR) && !defined(OS_WINDOWS)
	if (!entry)
	{
		closedir(dir);
		return ERROR_MEMORY;
	}
#endif

	evfs_protocol_t protocol;
	struct string buffer = {.data = 0};
	int status = 0;

	struct file file;

	file.access = EVFS_READ | EVFS_APPEND | EVFS_MODIFY;

	// TODO breaks and continues should check for strict mode
	while (true)
	{
#if defined(READDIR)
		errno = 0;
		if (!(entry = readdir(dir)))
		{
			if (errno)
				status = errno_error(status);
			break;
		}
#elif !defined(OS_WINDOWS)
		if (status = readdir_r(dir, entry, &more))
		{
			status = errno_error(status);
			break;
		}
		if (!more) break; // no more entries
#else
		if (!(more = readdir(dir))) break;
		entry = *more;
		file.content.fd = -1;
#endif

#if defined(_DIRENT_HAVE_D_NAMLEN)
		filename = string(entry->d_name, entry->d_namlen);
#elif !defined(OS_WINDOWS)
		filename = string(entry->d_name, strlen(entry->d_name));
#else
		filename = string(entry.d_name, strlen(entry.d_name));
#endif
		if (skip(&filename)) continue; // skip . and ..

		if (!buffer_adjust(cwd, cwd->length + filename.length + 1))
		{
			status = ERROR_MEMORY; // either we or the OS doesn't want to spend more memory
			break;
		}
		*format_string(cwd->data + cwd->length, filename.data, filename.length) = 0;

		if (lstat(cwd->data, &info) < 0)
		{
			if (flags & EVFS_STRICT)
			{
				status = ERROR_CANCEL;
				break;
			}
			else continue;
		}

		file._encoding = EVFS_ENCODING_IDENTITY;

		protocol = 0;

		if (info.st_mode & S_IFDIR)
		{
			file.size = 0;
			file.type = EVFS_DIRECTORY;
		}
		else if (info.st_mode & S_IFREG)
		{
#if !defined(OS_WINDOWS)
			file.size = info.st_size;
			file.type = EVFS_REGULAR;

			if (info.st_size) // open non-empty files
			{
				int archive = open(cwd->data, O_RDONLY); // TODO: O_NOFOLLOW ?
				if (archive < 0)
				{
					if (flags & EVFS_STRICT)
					{
						status = ERROR_CANCEL;
						break;
					}
					else continue;
				}

				if (flags & EVFS_NESTED)
				{
					buffer.length = info.st_size;
					buffer.data = mmap(0, buffer.length, PROT_READ, MAP_PRIVATE, archive, 0);
					if (buffer.data == MAP_FAILED)
					{
						buffer.data = 0;
						if (errno == ENOMEM) // file is too big to be mapped
						{
							file._encoding = EVFS_ENCODING_FD;
							file._fd = archive;
						}
						else
						{
							close(archive);
							if (flags & EVFS_STRICT)
							{
								status = ERROR_CANCEL;
								break;
							}
							else continue;
						}
					}
					else
					{
						close(archive);

						// Recognize archive format.
						file._content = buffer;
						if (protocol = evfs_recognize(&file))
							file.type = EVFS_DIRECTORY;
					}
				}
				else
				{
					file._encoding = EVFS_ENCODING_FD;
					file._fd = archive;
				}
			}
		}
		else if (flags & EVFS_STRICT)
		{
			status = ERROR_CANCEL;
			break;
		}
#else
			int archive = open(cwd->data, O_RDONLY | O_BINARY); // TODO: O_NOFOLLOW ?
			if (archive < 0) continue;

			file.content.fd = archive;
			file.content.offset = 0;
			file.content.length = info.st_size;
			buffer.data=0;
			buffer.length = 0;

			file.size = info.st_size;
			file._content = buffer;
			file.type = EVFS_REGULAR;
		}
#endif
		else continue;

		// Invoke callback.
		cwd->length += filename.length;
		file.name = string(cwd->data + location->length + 1, cwd->length - location->length - 1);
		file.mtime = info.st_mtime;
		if (status = callback(&file, argument))
		{
#if !defined(OS_WINDOWS)
			if (buffer.data)
			{
				munmap(buffer.data, buffer.length);
				buffer.data = 0;
			}
			else if (file._encoding == EVFS_ENCODING_FD) close(file._fd);
#else
	
		if (file.content.fd > -1)
		{
			close(file.content.fd);
			file.content.fd = -1;
		}
#endif
			break;
		}

		// Browse nested non-compressed entries if necessary.
		if ((file.type == EVFS_DIRECTORY) && depth && !file._encoding)
		{
			cwd->data[cwd->length++] = *SEPARATOR;
#if !defined(OS_WINDOWS)
			if (protocol) status = protocol(location, depth - 1, &file._content, cwd, callback, argument, flags);
#else
			int content = file.content.fd;
			struct string buffer;
			if (protocol) status = protocol(location,  &buffer, cwd, callback, argument, depth - 1,content);
#endif
			else status = evfs_directory(location, depth - 1, cwd, callback, argument, flags);
			cwd->length -= 1;
		}

finally:

#if !defined(OS_WINDOWS)
		if (buffer.data)
		{
			munmap(buffer.data, buffer.length);
			buffer.data = 0;
		}
		else if (file._encoding == EVFS_ENCODING_FD) close(file._fd);
#else
		if (file.content.fd > -1)
		{
			close(file.content.fd);
			file.content.fd = -1;
		}
#endif

		if (status) break;

		cwd->length -= filename.length;
	}

#if !defined(READDIR) && !defined(OS_WINDOWS)
	free(entry);
#endif
	closedir(dir);
	return status;
}

#if !defined(OS_WINDOWS) && !defined(OS_IOS)
// For each file in location, invoke callback(file, argument). Don't follow symbolic links.
// TODO free on longjmp: cwd, munmap(mapping.data, mapping.length)
int evfs_browse(struct string *restrict location, unsigned depth, evfs_callback_t callback, void *argument, unsigned flags)
{
	char *nul, *end = location->data + location->length;
	int status = 0;

	// Find and stat the longest prefix of location that is accessible through the filesystem.
	struct stat info;
	nul = end;
	while (lstat(location->data, &info) < 0)
	{
		if (errno == ENOTDIR) // location is most likely in an archive
		{
			// Restore original value of location->data if it was modified.
			if (nul < end) *nul = *SEPARATOR;

			// Put NUL terminator in location->data before the last path component.
			while (*--nul != *SEPARATOR)
				if (nul == location->data)
					return ERROR_MISSING;
			*nul = 0;
		}
		else return errno_error(errno);
	}
	// Now nul points one character before the relative path to the desired location starting from the stat()ed directory.

	// assert(nul <= end);
	bool browse = (nul == end); // whether current entry should be browsed

	// TODO check if there is cache that could help

	struct file file;

	if ((info.st_mode & S_IFMT) == S_IFDIR) // the requested node is a directory in the filesystem
	{
		file.name = string("");
		file.size = 0;
		file.mtime = info.st_mtime;
		file.type = EVFS_DIRECTORY;
		file.access = EVFS_READ | EVFS_APPEND; // TODO: modify access?

		// Invoke callback.
		if (status = callback(&file, argument)) return status;

		if (depth)
		{
			struct buffer cwd = {.data = 0};
			if (buffer_adjust(&cwd, location->length + 1))
			{
				*format_string(cwd.data, location->data, location->length) = '/';
				cwd.length = location->length + 1;
				status = evfs_directory(location, depth - 1, &cwd, callback, argument, flags);
				free(cwd.data);
			}
			else status = ERROR_MEMORY; // either we or the OS doesn't want to spend more memory
		}
	}
	else if ((info.st_mode & S_IFMT) == S_IFREG)
	{
		struct string mapping = {.data = 0};

		evfs_protocol_t protocol;

		file.size = info.st_size;
		if (file.size)
		{
			// TODO think about symbolic links
			//int archive = open(location->data, O_RDONLY | O_SYMLINK | O_NOFOLLOW);
			int archive = open(location->data, O_RDONLY);
			if (archive < 0) return errno_error(errno);

			mapping.length = info.st_size;
			mapping.data = mmap(0, mapping.length, PROT_READ, MAP_PRIVATE, archive, 0);
			close(archive);
			if (mapping.data == MAP_FAILED) return errno_error(errno);
			file._content = mapping;
		}
		else file._content = string("");
		file._encoding = EVFS_ENCODING_IDENTITY;

		// Recognize archive format if necessary.
		if (!browse || (flags & EVFS_NESTED)) protocol = evfs_recognize(&file);
		else protocol = 0;

		// Invoke callback for current file if it is in the browsed path.
		if (browse)
		{
			file.name = string("");
			file.mtime = info.st_mtime;
			file.access = EVFS_READ;

			if (protocol) file.type = EVFS_DIRECTORY;
			else file.type = EVFS_REGULAR;

			if (status = callback(&file, argument)) goto finally;
		}
		else if (!protocol)
		{
			status = ERROR_MISSING;
			goto finally;
		}

		// Browse archives if necessary.
		if (protocol && (!browse || (depth && (!file._encoding || (flags & EVFS_EXTRACT)))))
		{
			struct string content;
			struct buffer cwd = {.data = 0};

			if (file._encoding) // extract compressed content
			{
				struct string entry = string(location->data, nul - location->data);
				if (evfs_extract(&entry, &file, &content))
				{
					if (flags & EVFS_STRICT) status = ERROR_CANCEL;
					goto finally;
				}
			}
			else content = file._content;

			if (buffer_adjust(&cwd, location->length + 1))
			{
				// Set cwd.
				if (!browse) *nul = '/';
				*format_string(cwd.data, location->data, location->length) = '/';
				cwd.length = nul + 1 - location->data;

				status = protocol(location, depth, &content, &cwd, callback, argument, flags);
				if (content.data != file._content.data) munmap(content.data, content.length);
				free(cwd.data);
			}
			else status = ERROR_MEMORY;
		}

finally:
		if (mapping.data)
		{
			munmap(mapping.data, mapping.length);
			mapping.data = 0;
		}
	}
	/*else if ((info.st_mode & S_IFMT) == S_IFLNK)
	{
	}*/
	else return ERROR; // TODO: browse not supported on this file type

	return status;
}
#endif

#if !defined(OS_WINDOWS)
static unsigned evfs_input(void *arg, unsigned char **result)
{
	struct string *buffer = arg;
	*result = buffer->data;
	return buffer->length;
}

// WARNING: Works only for regular files.
// TODO free on longjmp: inflateBackEnd(&strm), munmap(buffer, size)
int evfs_file(const struct file *restrict file, int callback(void *, unsigned char *, unsigned), void *argument)
{
	if (!file->size) return 0;

	// TODO support other storage methods - struct stream to read from, etc.

	switch (file->_encoding)
	{
	case 0:
		{
			// Send file content in pieces if it's not possible to send it at once (unsigned has limited range).
			int status;
			size_t index = 0;
			while ((file->_content.length - index) > UINT_MAX)
			{
				status = callback(argument, file->_content.data + index, INT_MAX + 1u);
				if (status) return status;
				index += INT_MAX + 1u;
			}
			return callback(argument, file->_content.data + index, file->_content.length - index);
		}

	case EVFS_ENCODING_DEFLATED:
		{
			char window[1 << ZIP_WINDOW_BITS];
			z_stream strm = {
				.zalloc = Z_NULL,
				.zfree = Z_NULL,
				.opaque = Z_NULL
			};

			// TODO: calculate crc32 somehow

			switch (inflateBackInit(&strm, ZIP_WINDOW_BITS, window))
			{
			case Z_MEM_ERROR: // unable to allocate memory
				return ERROR_MEMORY;
			case Z_VERSION_ERROR: // compression version unsupported
				return ERROR_UNSUPPORTED;
			}
			int status = inflateBack(&strm, evfs_input, (void *)&file->_content, callback, argument);
			inflateBackEnd(&strm);

			switch (status)
			{
			case Z_BUF_ERROR: // callback returned error
				return ERROR_WRITE;
			case Z_DATA_ERROR: // deflate error
				return ERROR_EVFS;
			case Z_STREAM_END:
				// Inflating is successful if the deflated data was properly terminated and there is no more data available.
				// TODO file->_content.length may not be the right value (because of gzip archives). the first return is commented to handle this case
				//return !strm.avail_in;
				return 0;
			}
		}

	case EVFS_ENCODING_FD:
		{
			char *buffer;
			off_t offset = 0, size;
			int status;

			while (size = file->size - offset)
			{
				if (size > BUFFER_COMPAT) size = BUFFER_COMPAT;
				buffer = mmap(0, size, PROT_READ, MAP_PRIVATE, file->_fd, offset);
				if (buffer == MAP_FAILED) return errno_error(errno);
				offset += size;

				status = callback(argument, buffer, size);
				munmap(buffer, size);
				if (status) return status;
			}
		}
		return 0;

	default:
		return ERROR_UNSUPPORTED;
	}
}
#endif
