#if !defined(OS_WINDOWS)

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "types.h"
#include "format.h"
#include "evfs.h"

#define TAR_MAGIC "ustar"

// tar specification:
// http://www.gnu.org/software/tar/manual/html_node/Standard.html
// http://www.fileformat.info/format/tar/corion.htm

// TODO pax support

struct tar_header
{
	char name[100];				/*   0 */
	char mode[8];				/* 100 */
	char uid[8];				/* 108 */
	char gid[8];				/* 116 */
	char size[12];				/* 124 */
	char mtime[12];				/* 136 */
	char chksum[8];				/* 148 */
	char typeflag;				/* 156 */
	char linkname[100];			/* 157 */
	char magic[6];				/* 257 */
	char version[2];			/* 263 */
	char uname[32];				/* 265 */
	char gname[32];				/* 297 */
	char devmajor[8];			/* 329 */
	char devminor[8];			/* 337 */
	char prefix[155];			/* 345 */
	char fill[12];				/* 500 */
};

// Check whether a tar header field is properly padded with NULs.
// Returns length of the field if it is padded properly and -1 otherwise.
ssize_t tar_field(const char *restrict buffer, size_t size)
{
	size_t length = strnlen(buffer, size);
	if (length == size) return -1;
	for(size -= 1; size > length; --size)
		if (buffer[size])
			return -1;
	return (ssize_t)length;
}

// Invokes callback for each file in the tar buffer that is contained in the directory location.
#if !defined(OS_WINDOWS)
// @location	Absolute path to the root entry for browsing. No / at the end.
// @cwd			Absolute path of current directory. It always ends in '/'.
int evfs_tar(const struct string *restrict location, unsigned depth, const struct string *restrict buffer, struct buffer *restrict cwd, evfs_callback_t callback, void *argument, unsigned flags)
{
#else
int evfs_tar(const struct string *restrict location,  struct string *restrict buffer, struct buffer *restrict cwd, evfs_callback_t callback, void *argument, unsigned depth, int buffer_fd)
{
#endif
	char *position = buffer->data, *end = position + buffer->length, *after;

	struct tar_header header;

	struct string search, path;
	if (cwd->length < location->length) search = string(location->data + cwd->length, location->length - cwd->length);
	else search = string(location->data, 0);

	unsigned depth_real; // depth of entry relative to the current archive
	evfs_protocol_t protocol;
	int status;
	struct dict visited;

	if (!dict_init(&visited, DICT_SIZE_BASE)) return ERROR_MEMORY;

	struct file file, file_virtual;
	file.access = EVFS_READ; // only read support for tar archives
	file_virtual.access = EVFS_READ;

	// assert(depth >= 0);

next:
	while ((end - position) >= TAR_BLOCK_SIZE)
	{
		if (!*position)
		{
			// TODO: check whether this is a block with 0 values
			status = 0;
			goto finally;
		}

		header = *(struct tar_header *)position;
		position += sizeof(header);

		// Initialize file information from tar header.
		// Check whether the header is valid.
		// - all paddings must be NUL-filled
		// - magic must be TAR_MAGIC
		// - version must be "00"
		// - all numeric fields must be octal

		// TODO: some fields are not checked and not used; maybe they should be handled in some way

		status = ERROR_EVFS;

		ssize_t size = tar_field(header.name, sizeof(header.name));
		if (size < 0) goto finally;
		path = string(header.name, size);

		// TODO check path validity with path_malicious()

		if (tar_field(header.linkname, sizeof(header.linkname)) < 0) goto finally;

		// These tests are disabled because some archives fail them.
		//if (memcmp(header.magic, TAR_MAGIC, sizeof(TAR_MAGIC))) goto finally; // include terminating NUL
		//if ((header.version[0] != '0') || (header.version[1] != '0')) goto finally;

		if (memcmp(header.magic, TAR_MAGIC, sizeof(TAR_MAGIC) - 1)) goto finally;

		if (tar_field(header.uname, sizeof(header.uname)) < 0) goto finally;

		if (tar_field(header.gname, sizeof(header.gname)) < 0) goto finally;

		file.size = strtol(header.size, &after, 8);
		// WARNING: file.size set for directories here
		if ((after != (header.size + sizeof(header.size) - 1)) || (*after && (*after != ' '))) goto finally;

		file.mtime = strtol(header.mtime, &after, 8);
		if ((after != (header.mtime + sizeof(header.mtime) - 1)) || (*after && (*after != ' '))) goto finally;

		// TODO: tar specifies unrecognized file types to be treated as regular files; however this may require special handling
		// Determine whether the file is a directory.
		if (header.typeflag == '5')
		{
			file.type = EVFS_DIRECTORY;
			//file.size = 0;

			path.length -= 1; // TODO is this a good idea?
		}
		else file.type = EVFS_REGULAR;

		file._encoding = EVFS_ENCODING_IDENTITY;
		file._content = string(position, file.size);

		position += (file.size + TAR_BLOCK_SIZE - 1) & ~(TAR_BLOCK_SIZE - 1);

		if (EVFS_IN(path, search)) // this entry is in the path specified in location
		{
			// Generate relative path to the current entry.
			size_t skip = search.length + (search.length && (path.length > search.length));
			struct string relative = string(path.data + skip, path.length - skip);
			size_t length = cwd->length + path.length;
			file.name = string(cwd->data + location->length + 1, length - location->length - (length > location->length));

			file_virtual.mtime = file.mtime;

			// Calculate path depth and invoke callback for each unvisited parent directory.
			if (relative.length)
			{
				struct string key = string(file.name.data, 0);

				// Append current entry path to cwd.
				if (!buffer_adjust(cwd, cwd->length + path.length + 1))
				{
					status = ERROR_MEMORY;
					goto finally;
				}
				format_string(cwd->data + cwd->length + skip, relative.data, relative.length);

				file_virtual.type = EVFS_DIRECTORY;
				file_virtual.size = 0;

				// Invoke callback if the entry is not visited yet.
				status = dict_add(&visited, &key, 0);
				if (status == ERROR_MEMORY) goto finally;
				else if (!status)
				{
					file_virtual.name = string("");
					if (callback(&file_virtual, argument))
					{
						// TODO is this right?
						if (flags & EVFS_STRICT)
						{
							status = ERROR_CANCEL;
							goto finally;
						}
						else goto next;
					}
				}

				depth_real = 1;

				do
				{
					if (key.data[key.length] != '/') continue;

					if (depth_real > depth)
					{
						if (flags & EVFS_STRICT)
						{
							status = ERROR_CANCEL;
							goto finally;
						}
						else goto next;
					}
					depth_real += 1;

					// Invoke callback if the entry is not visited yet.
					status = dict_add(&visited, &key, 0);
					if (status == ERROR_MEMORY) goto finally;
					else if (!status)
					{
						file_virtual.name = key;
						file_virtual.name.data[key.length] = 0;
						if (callback(&file_virtual, argument))
						{
							if (flags & EVFS_STRICT)
							{
								status = ERROR_CANCEL;
								goto finally;
							}
							else goto next;
						}
						file_virtual.name.data[key.length] = '/';
					}
				} while (++key.length < file.name.length);
			}
			else depth_real = 0;

			// Check whether the entry exceeds the desired depth.
			if (depth_real > depth)
			{
				if (flags & EVFS_STRICT)
				{
					status = ERROR_CANCEL;
					goto finally;
				}
				else continue;
			}

			// Mark current entry as visited. If it is already visited (status != 0), we will skip it.
			status = dict_add(&visited, &file.name, 0);
			if (status == ERROR_MEMORY) goto finally;

			file_virtual._content = file._content;
			file_virtual._encoding = file._encoding;

			// Recognize nested archive format.
			protocol = 0;
			if ((file.type == EVFS_REGULAR) && (flags & EVFS_NESTED) && (protocol = evfs_recognize(&file_virtual))) file_virtual.type = EVFS_DIRECTORY;
			else
			{
				file_virtual.size = file.size;
				file_virtual.type = file.type;
			}

			// Invoke callback if this entry is not visited yet.
			if (!status)
			{
				file_virtual.name = file.name;
				if (callback(&file_virtual, argument))
				{
					if (flags & EVFS_STRICT)
					{
						status = ERROR_CANCEL;
						goto finally;
					}
					else continue;
				}
			}

			// Browse nested non-compressed archives if depth is enough.
			if (protocol && !file_virtual._encoding && (depth_real < depth))
			{
				cwd->data[length] = '/';
				cwd->length = length + 1;
				status = protocol(location, depth - depth_real, &file_virtual._content, cwd, callback, argument, flags);
				cwd->length -= skip + file.name.length + 1;
			}
			else status = 0;

			if (status) goto finally;
			else continue;
		}
		else if ((file.type == EVFS_REGULAR) && EVFS_IN(search, path)) // this entry is an archive containing the directory we want to browse
		{
			cwd->length += path.length + 1;
			struct string resource = string(cwd->data, cwd->length - 1);

			dict_term(&visited);

			// Recognize nested archive format and browse it.
			if (protocol = evfs_recognize(&file))
			{
				struct string content;
				if (!file._encoding) content = file._content;
				else if (status = evfs_extract(&resource, &file, &content)) return status;

				status = protocol(location, depth, &content, cwd, callback, argument, flags);

				if (file._encoding) munmap(content.data, content.length);
				return status;
			}
			else return ERROR_UNSUPPORTED;
		}
		else continue; // this entry is not in location
	}

	// Return error if we were searching for specific entries and none were found.
	status = ((search.length && !visited.count) ? ERROR_MISSING : 0);

finally:
	dict_term(&visited);
	return status;
}

#endif
