#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if !defined(OS_WINDOWS)
# include <sys/mman.h>
#else
# define WINDOWS_BLOCK_SIZE 0x200000
# include <windows.h>
# include <winsock2.h>
# include "../../windows/mingw.h"
# define ELOOP -1
#endif

#include "types.h"
#include "format.h"
#include "magic.h"
#include "actions.h"
#include "download.h"
#include "evfs.h"
#include "earchive.h"

// TODO: fix this
#if !defined(O_NOFOLLOW)
# define O_NOFOLLOW 0
#endif

// TODO this is not perfect but usually works
#if __x86_64__ || __LP64__
# define E64
#else
# define E32
#endif

// TODO support unix attributes (UNIX Extra Field) - UID, GID, etc.
// TODO optimize by not extracting deflate-compressed entries

struct entry
{
	struct string name;
#if !defined(OS_WINDOWS)
	off_t offset, size;
#else
	int64_t offset, size;
#endif
	uint32_t crc;
	time_t mtime;
};

static struct string content_type = {.data = "Content-Type", .length = sizeof("Content-Type") - 1};

int http_download(const struct string *path, const struct http_request *request, struct http_response *restrict response, struct stream *restrict stream)
{
#if !defined(OS_WINDOWS)
	struct stat info;
#else
	struct _stati64 info;
#endif
	struct string *buffer;
	int file;
	int status;

	struct string key, value;

#if !defined(OS_WINDOWS)
	file = open(path->data, O_RDONLY | O_NOFOLLOW);
#else
	file = open(path->data, O_RDONLY | O_BINARY);
#endif
	if (file < 0) return http_errno_status(errno);

	// Use fstat on the descriptor to ensure that the stated file is the opened one.
	// This prevents some possible TOCTOU attacks.
#if !defined(OS_WINDOWS)
	if (fstat(file, &info) < 0)
#else
	if (_fstati64(file, &info) < 0)
#endif
	{
		close(file);
		return http_errno_status(errno);
	}

	if (!S_ISREG(info.st_mode))
	{
		close(file);
		return NotFound;
	}

	// Content-Type - Detected by magic bytes
	key = string("Content-Type");
	const struct string *type;
	char magic_buffer[MAGIC_SIZE];
	ssize_t size = read(file, magic_buffer, MAGIC_SIZE);
	if (size < 0) type = &type_unknown;
	else type = mime(magic_buffer, size);
	if (!response_header_add(response, &key, type))
	{
		close(file);
		return -1; // memory error
	}

	response->code = OK;
	if (!response_headers_send(stream, request, response, info.st_size))
	{
		close(file);
		return -1;
	}

	if (response->content_encoding) // if response body is required
	{
#if !defined(OS_WINDOWS)
# if defined(E64)
		char *buffer = mmap(0, info.st_size, PROT_READ, MAP_PRIVATE, file, 0);
		close(file);
		if (buffer == MAP_FAILED) return http_errno_status(errno); // TODO what if after the headers are sent, mmap fails and no headers can be sent to indicate the error?

		status = response_content_send(stream, response, buffer, info.st_size);
		munmap(buffer, info.st_size);
		if (!status) return -1;
# else
		#define MMAP_PART_BUFFER 67108864 /* 64MiB */
		// assert(!(MMAP_PART_BUFFER % sysconf(_SC_PAGE_SIZE)));

		char *buffer;
		off_t offset = 0, size;

		// Send file content on pieces with size MMAP_PART_BUFFER each.
		while (size = info.st_size - offset)
		{
			if (size > MMAP_PART_BUFFER) size = MMAP_PART_BUFFER;
			buffer = mmap(0, size, PROT_READ, MAP_PRIVATE, file, offset);
			if (buffer == MAP_FAILED)
			{
				close(file);
				return -1; // TODO return http_errno_status(errno);
			}
			offset += size;

			status = response_entity_send(stream, response, buffer, size);
			munmap(buffer, size);
			if (status) return -1; // TODO
		}

		#undef MMAP_PART_BUFFER
# endif
#else
		struct string *buffer=string_alloc(0, WINDOWS_BLOCK_SIZE);
		if (!buffer)
		{
			close(file);
			return http_errno_status(errno);
		}

		__int64 index;
		__int64 size;
		if (response->ranges)
		{
			lseek64(file, response->ranges[0][0], SEEK_SET);
			info.st_size = response->ranges[0][1] - response->ranges[0][0] + 1;
			response_content_send(stream, response, 0, response->ranges[0][0]);
		}
		else lseek64(file, 0, SEEK_SET);
		for(index = 0; index < info.st_size; index += size)
		{
			size = read(file, buffer->data, WINDOWS_BLOCK_SIZE);
			if (size <= 0) return -1;
			buffer->length=size;

			status = response_content_send(stream, response, buffer->data, buffer->length);
			if (!status)
			{
				close(file);
				return -1;
			}
		}
#endif
	}

	return 0;
}

// TODO fix this to comply this: http://tools.ietf.org/html/rfc2183
static bool content_disposition(struct http_response *restrict response, const struct string *restrict prefix, const struct string *restrict suffix)
{
	#define BEFORE "attachment; filename=\""
	#define AFTER '"'

	size_t index, length = sizeof(BEFORE) - 1 + prefix->length + 1;

	// Make sure to allocate enough space to escape double quotes.
	for(index = 0; index < prefix->length; ++index)
		switch (prefix->data[index])
		{
		case '"':
		case '\\':
			length += 1;
		}

	if (suffix) length += suffix->length;

	struct string key = string("Content-Disposition"), *value = malloc(sizeof(struct string) + length);
	if (!value) return false;
	value->length = length;

	// Generate Content-Disposition header value.
	char *start = value->data = (char *)(value + 1);
	start = format_bytes(start, BEFORE, sizeof(BEFORE) - 1);
	for(index = 0; index < prefix->length; ++index)
	{
		switch (prefix->data[index])
		{
		case '"':
		case '\\':
			*start++ = '\\';
		}
		*start++ = prefix->data[index];
	}
	if (suffix) start = format_bytes(start, suffix->data, suffix->length);
	*start = AFTER;

	bool success = response_header_add(response, &key, value);
	free(value);

	#undef BEFORE
	#undef AFTER

	return success;
}

static int download_file(void *argument, unsigned char *buffer, unsigned size)
{
	struct download *info = argument;
	struct http_response *response = info->response;

	if (response->content_encoding < 0) // headers not sent yet
	{
		size_t available = MAGIC_SIZE - info->magic_size;
		struct string key = string("Content-Type");
		const struct string *value;

		// If we don't have enough data to determine content type, just remember buffer data in info.
		if (available > size)
		{
			format_bytes(info->magic + info->magic_size, buffer, size);
			info->magic_size += size;

			// Handle files with size less than MAGIC_SIZE properly.
			if (info->magic_size == info->offset)
			{
				value = &type_unknown;
				goto send;
			}

			return 0;
		}

		// Content-Type
		format_bytes(info->magic + info->magic_size, buffer, available);
		buffer += available;
		size -= available;
		value = mime(info->magic, MAGIC_SIZE);
send:
		if (!response_header_add(response, &key, value)) return ERROR_WRITE;

		response->code = OK;
		if (!response_headers_send(info->stream, info->request, response, info->offset)) return ERROR_WRITE;

		if (!response_content_send(info->stream, response, info->magic, MAGIC_SIZE)) return ERROR_WRITE;

		if (!size) return 0;
	}

	if (!response_content_send(info->stream, response, buffer, size)) return ERROR_WRITE;

	return 0;
}

static int download_prepare(const struct file *restrict file, void *argument)
{
	struct download *info = argument;

	// last_modified == max(mtime)
	if (info->last_modified < file->mtime) info->last_modified = file->mtime;

	// Decide whether to create archive.
	if (info->archive < 0)
	{
		if (file->type == EVFS_DIRECTORY)
		{
			info->archive = -info->archive;
			if (info->archive == ARCHIVE_ZIP)
			{
				struct string suffix = string(".zip");
				if (!content_disposition(info->response, &info->prefix, &suffix)) return ERROR_MEMORY;
			}
		}
		else
		{
			info->archive = 0;
			if (!content_disposition(info->response, &info->prefix, 0)) return ERROR_MEMORY;
		}
	}

	if (info->archive)
	{
		// Allocate file structure and set file name with directory prefix.
		struct entry *entry = malloc(sizeof(struct entry) + info->prefix.length + 1 + file->name.length + 1);
		if (!entry) return ERROR_MEMORY;
		char *start = entry->name.data = (char *)(entry + 1);
		start = format_bytes(start, info->prefix.data, info->prefix.length);
		if (file->name.length)
		{
			*start++ = '/';
			start = format_bytes(start, file->name.data, file->name.length);
		}
		if ((info->archive == ARCHIVE_ZIP) || (info->archive == ARCHIVE_ZIP64))
			if (file->type == EVFS_DIRECTORY)
				*start++ = '/';
		entry->name.length = start - entry->name.data;

		#if defined(OS_WINDOWS)
		int i=0;
		for(;i<entry->name.length;i++)
			if(entry->name.data[i]=='\\')entry->name.data[i]='/';
		#endif

		entry->size = file->size;

		if (info->archive == ARCHIVE_EARCHIVE)
		{
			info->total += 24 + entry->name.length;
			if (file->size && (file->type == EVFS_REGULAR)) info->total += 8 + file->size;
		}
		else // zip archive
		{
			if (info->files.length)
			{
				struct entry *prev = vector_get(&info->files, info->files.length - 1);
				entry->offset = prev->offset + 30 + prev->name.length + prev->size;
				if (prev->size >= (uint32_t)-1) entry->offset += 4 + 8 + 8; // zip64 extra field
			}
			else entry->offset = info->offset;

			info->total += 30 + 46 + entry->name.length * 2 + file->size;

			// Add zip64 extra fields' length to the total length.
			if (entry->size >= (uint32_t)-1)
			{
				info->archive = ARCHIVE_ZIP64;
				info->total += (4 + 8 + 8) * 2;
				if (entry->offset >= (uint32_t)-1) info->total += 8;
			}
			else if (entry->offset >= (uint32_t)-1)
			{
				info->archive = ARCHIVE_ZIP64;
				info->total += 4 + 8;
			}
		}

		if (!vector_add(&info->files, entry))
		{
			free(entry);
			return ERROR_MEMORY;
		}
	}
	else
	{
		char date[HTTP_DATE_LENGTH + 1];
		http_date(date, file->mtime);
		struct string key = string("Last-Modified"), value = string(date, HTTP_DATE_LENGTH);
		if (!response_header_add(info->response, &key, &value)) return ERROR_MEMORY;

		if (file->size)
		{
			info->magic_size = 0;
			info->offset = file->size; // TODO offset is not the right variable to use here
			if (evfs_file(file, download_file, argument)) return -1; // TODO: error
		}
		else
		{
			// Send headers.
			info->response->code = OK;
			if (!response_header_add(info->response, &content_type, &type_unknown)) return ERROR_MEMORY;
			if (!response_headers_send(info->stream, info->request, info->response, 0)) return -1; // TODO
		}
	}

	return 0;
}

static int archive_file(void *argument, unsigned char *buffer, unsigned size)
{
	struct download *info = argument;
	if (!response_content_send(info->stream, info->response, buffer, size)) return ERROR_WRITE; // TODO error
	info->offset += size;
	return 0;
}

static int download_zip(const struct file *restrict file, void *argument)
{
	struct download *info = argument;

	// Retrieve entry information.
	struct entry *entry;
	if (info->index >= info->files.length) return ERROR_CANCEL; // TODO
	entry = vector_get(&info->files, info->index);
	info->index += 1;

	struct string path;
	path.data = malloc(info->prefix.length + 1 + file->name.length + 1);
	if (!path.data) return ERROR_MEMORY;

	// Generate file name with directory prefix.
	char *start = format_bytes(path.data, info->prefix.data, info->prefix.length);
	if (file->name.length)
	{
		*start++ = '/';
		start = format_bytes(start, file->name.data, file->name.length);
	}
	if (file->type == EVFS_DIRECTORY) *start++ = '/';
	path.length = start - path.data;

	#if defined(OS_WINDOWS)
	
		int i=0;
		for(;i<path.length;i++)
			if(path.data[i]=='\\')path.data[i]='/';
	
	#endif

	// Cancel download if download content changed.
	int status = ((file->size != entry->size) || !string_equal(&path, &entry->name));
	free(path.data);
	if (status) return ERROR_CANCEL; // TODO

	// Calculate file checksum.
	entry->crc = ZIP_CRC32_BASE;
	if (file->size && evfs_file(file, zip_crc, &entry->crc)) return -1; // TODO

	entry->mtime = file->mtime;

	char header[62], *position = header;

	unsigned zip64 = (entry->size >= (uint32_t)-1) * 16;
	uint16_t version;
	if (zip64) version = ZIP_VERSION_64;
	else if (file->type == EVFS_DIRECTORY) version = ZIP_VERSION_DIRECTORY;
	else version = ZIP_VERSION_REGULAR;

	// Generate local file header.
	uint32_t signature = ZIP_LOCALFILE_SIGNATURE;
	endian_big32(position, &signature);
	position += 4;
	zip_write(position, version, 16);
	zip_write(position, 1 << 11, 16);						// bit 11: UTF-8
	zip_write(position, 0, 16);								// no compression
	zip_date(position, entry->mtime);
	position += sizeof(uint32_t);
	zip_write(position, entry->crc, 32);
	zip_write(position, entry->size, 32);					// compressed size
	zip_write(position, entry->size, 32);					// uncompressed size
	zip_write(position, entry->name.length, 16);
	zip_write(position, (zip64 ? (4 + zip64) : 0), 16);		// extra field length

	if (!response_content_send(info->stream, info->response, header, position - header)) return -1; // TODO
	if (!response_content_send(info->stream, info->response, entry->name.data, entry->name.length)) return -1; // TODO
	info->offset += (position - header) + entry->name.length;

	if (zip64) // zip64 extended information
	{
		position = header;
		zip_write(position, 0x1, 16);						// extra field tag
		zip_write(position, zip64, 16);						// size of the extra field
		zip_write(position, entry->size, 64);				// uncompressed size
		zip_write(position, entry->size, 64);				// compressed size

		if (!response_content_send(info->stream, info->response, header, position - header)) return -1; // TODO
		info->offset += position - header;
	}

	if (file->size && evfs_file(file, archive_file, argument)) return -1; // TODO

	return 0; // continue browsing for more files
}

static int download_earchive(const struct file *restrict file, void *argument)
{
	struct download *info = argument;

	// Retrieve entry information.
	struct entry *entry;
	if (info->index >= info->files.length) return ERROR_CANCEL; // TODO
	entry = vector_get(&info->files, info->index);
	info->index += 1;

	struct string filename;
	filename.data = malloc(info->prefix.length + 1 + file->name.length + 1);
	if (!filename.data) return ERROR_MEMORY;

	// Generate file name with directory prefix.
	char *start = format_bytes(filename.data, info->prefix.data, info->prefix.length);
	if (file->name.length)
	{
		*start++ = '/';
		start = format_bytes(start, file->name.data, file->name.length);
	}
	filename.length = start - filename.data;

	// Cancel download if download content changed.
	int status = ((file->size != entry->size) || !string_equal(&filename, &entry->name));
	if (status)
	{
		free(filename.data);
		return ERROR_CANCEL; // TODO
	}

	info->offset += 24 + filename.length;

	char header[sizeof(struct metadata)];
	struct string chunk = string(header, sizeof(header));

	metadata_format(header, file, filename.length, info->offset);

	status = response_content_send(info->stream, info->response, chunk.data, chunk.length);
	status = status && response_content_send(info->stream, info->response, filename.data, filename.length);

	if (file->size && (file->type == EVFS_REGULAR))
	{
		uint64_t size;
		endian_big64(&size, &file->size);
		status = status && response_content_send(info->stream, info->response, (char *)&size, sizeof(size));
		if (evfs_file(file, archive_file, argument))
		{
			free(filename.data);
			return -1; // TODO
		}
	}

	free(filename.data);

	return (status ? 0 : -1);
}

int download(const struct vector *restrict paths, struct download *restrict info, unsigned archive)
{
	int status;
	size_t index;
	int (*download_process)(const struct file *restrict, void *);

	// Create an archive if we are downloading multiple files or a directory.
	// For a directory, archive name is determined on the first call to download_prepare().

	if (paths->length > 1)
	{
		info->archive = archive;
		if (info->archive == ARCHIVE_ZIP)
		{
			struct string prefix = string("filement_archive"), suffix = string(".zip");
			if (!content_disposition(info->response, &prefix, &suffix))
			{
				status = ERROR_MEMORY;
				goto finally;
			}
		}
	}
	else info->archive = -archive;

	if (!vector_init(&info->files, VECTOR_SIZE_BASE)) return ERROR_MEMORY;

	if (archive == ARCHIVE_EARCHIVE)
	{
		download_process = download_earchive;
		info->offset = 8; // header
		info->total = 8; // header
	}
	else
	{
		// assert(archive == ARCHIVE_ZIP);
		download_process = download_zip;
		info->offset = 0;
		info->total = 22; // end of central directory
	}

	struct string *path;
	size_t start;

	// last_modified == max(mtime)
	info->last_modified = 0; // 0 is the least value

	// Collect the necessary information for the download.
	for(index = 0; index < paths->length; ++index)
	{
		path = vector_get(paths, index);

		// Set prefix for the file.
		for(start = path->length; start > 0; --start) if (path->data[start - 1] == '/') break;
		info->prefix = string(path->data + start, path->length - start);

		// Single-file download is handled by download_prepare().
		if (status = evfs_browse(path, 63, download_prepare, info, EVFS_STRICT)) goto finally;
	}

	// Nothing more to do if we are downloading a regular file.
	if (!info->archive) goto finally;

	struct string type;
	if (info->archive == ARCHIVE_EARCHIVE) type = string("application/earchive");
	else
	{
		// zip archive
		if ((info->total >= (uint32_t)-1) || (info->files.length >= (uint16_t)-1)) info->archive = ARCHIVE_ZIP64;
		if (info->archive == ARCHIVE_ZIP64) info->total += 56 + 20; // zip64 footers
		type = string("application/zip");
	}
	if (!response_header_add(info->response, &content_type, &type))
	{
		status = ERROR_MEMORY;
		goto finally;
	}

	char date[HTTP_DATE_LENGTH + 1];
	http_date(date, info->last_modified);
	struct string key = string("Last-Modified"), value = string(date, HTTP_DATE_LENGTH);
	if (!response_header_add(info->response, &key, &value))
	{
		status = ERROR_MEMORY;
		goto finally;
	}

	// Send headers.
	info->response->code = OK;
	if (!response_headers_send(info->stream, info->request, info->response, info->total))
	{
		status = -1; // TODO
		goto finally;
	}

	// Download is finished if body is not required.
	if (!info->response->content_encoding)
	{
		status = 0;
		goto finally;
	}

	if (info->archive == ARCHIVE_EARCHIVE)
	{
		struct string header = string("\x00\x02\x00\x00\x00\x00\x00\x00");
		if (!response_content_send(info->stream, info->response, header.data, header.length))
		{
			status = -1; // TODO
			goto finally;
		}
	}

	// Add each file in the vector to the archive.
	info->index = 0;
	for(index = 0; index < paths->length; ++index)
	{
		path = vector_get(paths, index);

		// Set prefix for the file.
		for(start = path->length; start > 0; --start) if (path->data[start - 1] == '/') break;
		info->prefix = string(path->data + start, path->length - start);

		if (status = evfs_browse(path, 63, download_process, info, EVFS_STRICT)) goto finally;
	}

	if (info->index != info->files.length)
	{
		status = -1; // TODO
		goto finally;
	}

	if ((info->archive == ARCHIVE_ZIP) || (info->archive == ARCHIVE_ZIP64))
	{
	#if !defined(OS_WINDOWS)
		size_t central_dir64_offset, central_dir_offset, central_dir_size;
	#else
		uint64_t central_dir64_offset, central_dir_offset, central_dir_size;
	#endif

		central_dir_offset = info->offset;

		// TODO make sure this is enough; for now the longest possible is 78 (central file signature)
		char header[78], *position;

		struct entry *file;
		mode_t mode;

		uint32_t signature;
		unsigned zip64;
		uint16_t version;

		// Add each file to the central directory.
		for(index = 0; index < info->files.length; ++index)
		{
			file = vector_get(&info->files, index);

			mode = ((file->name.data[file->name.length - 1] == '/') ? (S_IFDIR | 0111) : S_IFREG) | 0644;
			zip64 = (file->size >= (uint32_t)-1) * 16 + (file->offset >= (uint32_t)-1) * 8;

			if (zip64) version = ZIP_VERSION_64;
			else if (file->name.data[file->name.length - 1] == '/') version = ZIP_VERSION_DIRECTORY;
			else version = ZIP_VERSION_REGULAR;

			position = header;

			signature = ZIP_CENTRALFILE_SIGNATURE;
			endian_big32(position, &signature);
			position += 4;

			zip_write(position, ZIP_VERSION_CREATED, 16);
			zip_write(position, version, 16);
			zip_write(position, 1 << 11, 16);						// bit 11 - UTF-8
			zip_write(position, 0, 16);								// no compression
			zip_date(position, file->mtime);
			position += sizeof(uint32_t);
			zip_write(position, file->crc, 32);
			zip_write(position, file->size, 32);					// compressed size
			zip_write(position, file->size, 32);					// uncompressed size
			zip_write(position, file->name.length, 16);				// filename length
			zip_write(position, (zip64 ? (4 + zip64) : 0), 16);		// extra field length
			zip_write(position, 0, 16);								// file comment length
			zip_write(position, 0, 16);								// disk number start
			zip_write(position, 0, 16);								// internal file attributes
			zip_write(position, mode << 16, 32);					// external file attributes
			zip_write(position, file->offset, 32);					// local file offset

			if (!response_content_send(info->stream, info->response, header, position - header))
			{
				status = -1; // TODO
				goto finally;
			}
			if (!response_content_send(info->stream, info->response, file->name.data, file->name.length))
			{
				status = -1; // TODO
				goto finally;
			}
			info->offset += (position - header) + file->name.length;

			if (zip64) // zip64 extended information
			{
				position = header;
				zip_write(position, 0x1, 16);						// extra field tag
				zip_write(position, zip64, 16);						// size of the extra field
				if (file->size >= (uint32_t)-1)
				{
					zip_write(position, file->size, 64);			// uncompressed size
					zip_write(position, file->size, 64);			// compressed size
				}
				if (file->offset >= (uint32_t)-1) zip_write(position, file->offset, 64);

				if (!response_content_send(info->stream, info->response, header, position - header))
				{
					status = -1; // TODO
					goto finally;
				}
				info->offset += position - header;
			}
		}

		central_dir_size = info->offset - central_dir_offset;

		if (info->archive == ARCHIVE_ZIP64)
		{
			central_dir64_offset = info->offset;

			// zip64 end of central directory record

			position = header;

			signature = ZIP64_CENTRALDIR_END_SIGNATURE;
			endian_big32(position, &signature);
			position += 4;

			zip_write(position, 44, 64);							// size to the end of zip64 end of central directory record
			zip_write(position, ZIP_VERSION_CREATED, 16);
			zip_write(position, ZIP_VERSION_64, 16);
			zip_write(position, 0, 32);								// number of this disk
			zip_write(position, 0, 32);								// number of the disk with the start of the central directory
			zip_write(position, info->files.length, 64);			// total number of entries in the central directory on this disk
			zip_write(position, info->files.length, 64);			// total number of entries in the central directory
			zip_write(position, central_dir_size, 64);
			zip_write(position, central_dir_offset, 64);			// offset of the start of the central directory on the disk on which it starts

			if (!response_content_send(info->stream, info->response, header, position - header))
			{
				status = -1; // TODO
				goto finally;
			}
			//info->offset += position - header;

			// zip64 end of central directory locator

			position = header;

			signature = ZIP64_CENTRALDIR_LOCATOR_SIGNATURE;
			endian_big32(position, &signature);
			position += 4;

			zip_write(position, 0, 32);								// number of the disk with the start of the central directory
			zip_write(position, central_dir64_offset, 64);			// relative offset of the zip64 end of central directory record
			zip_write(position, 1, 32);								// total number of disks

			if (!response_content_send(info->stream, info->response, header, position - header))
			{
				status = -1; // TODO
				goto finally;
			}
			//info->offset += position - header;
		}

		position = header;

		signature = ZIP_CENTRALDIR_END_SIGNATURE;
		endian_big32(position, &signature);
		position += 4;

		zip_write(position, 0, 16);								// number of this disk
		zip_write(position, 0, 16);								// number of the disk with the start of the central directory
		zip_write(position, info->files.length, 16);			// total number of entries in the central directory on this disk
		zip_write(position, info->files.length, 16);			// total number of entries in the central directory
		zip_write(position, central_dir_size, 32);
		zip_write(position, central_dir_offset, 32);			// offset of the start of the central directory on the disk on which it starts
		zip_write(position, 0, 16);								// comment length

		if (!response_content_send(info->stream, info->response, header, position - header))
		{
			status = -1; // TODO
			goto finally;
		}
		//info->offset += position - header;
	}

	status = 0;

finally:
	for(index = 0; index < info->files.length; ++index)
		free(vector_get(&info->files, index));
	vector_term(&info->files);
	return status;
}
