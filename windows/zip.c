#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>

# include <sys/stat.h>
# include <windows.h>
# include "../../windows/mingw.h"
/*
ssize_t signature(const struct string *restrict string0, const struct string *restrict string1)
{
	uint32_t sig = ZIP_CENTRALFILE_SIGNATURE;
	struct string pattern = string((char *)&sig, sizeof(uint32_t));
	size_t *table = kmp_table(&pattern);
	if (!table) return -1;

	struct string string0end = string(string0->data + string0->length - 3, 3);
	struct string string1start = string(string1->data, 3);
	struct string *string01 = string_alloc(&string0end->data, &string1start->data);
	ssize_t position;

	position = kmp_search(&pattern, table, string0);
	if (position >= 0) goto finally;

	position = kmp_search(&pattern, table, string01);
	if (position >= 0)
	{
		position += string0->length - 3;
		goto finally;
	}

	position = kmp_search(&pattern, table, string1);
	if (position >= 0)
	{
		position += string0->length;
		goto finally;
	}

finally:
	free(string01);
	free(table);
	return position;
}
*/

struct zip_central
{
	uint16_t version_created, version_extract;
	// TODO finish this
};

char *zip_date(char *restrict buffer, time_t timestamp)
{
	struct tm mtime;
	gmtime_r(&timestamp, &mtime);
	if (mtime.tm_sec == 60) --mtime.tm_sec; // handle leap seconds

	uint16_t year = (uint16_t)(mtime.tm_year - 80) & 0x7f;
	uint16_t month = (uint16_t)(mtime.tm_mon + 1) & 0xf;
	uint16_t day = (uint16_t)mtime.tm_mday & 0x1f;
	uint16_t hour = (uint16_t)mtime.tm_hour & 0x1f;
	uint16_t minute = (uint16_t)mtime.tm_min & 0x3f;
	uint16_t second = (uint16_t)mtime.tm_sec & 0x1f;

	zip_write(buffer, (hour << 11) | (minute << 5) | (second >> 1), 16);
	zip_write(buffer, (year << 9) | (month << 5) | day, 16);
	return buffer;
}
time_t unzip_date(const char *restrict buffer)
{
	struct tm mtime;
	uint16_t date, time;

	zip_read(time, buffer, 16);
	zip_read(date, buffer, 16);

	mtime.tm_year = ((date >> 9) & 0x7f) + 80;
	mtime.tm_mon = ((date >> 5) & 0xf) - 1;
	mtime.tm_mday = date & 0x1f;
	mtime.tm_hour = (time >> 11) & 0x1f;
	mtime.tm_min = (time >> 5) & 0x3f;
	mtime.tm_sec = (time << 1) & 0x1f;

#if !defined(OS_WINDOWS)
	return timegm(&mtime);
#else
	return mktime(&mtime);
#endif
}

ssize_t zip_file_sig_pos(const struct string *restrict string0, const struct string *restrict string1);

int evfs_zip(const struct string *restrict location,  struct string *restrict buffer, struct buffer *restrict cwd, evfs_callback_t callback, void *argument, unsigned depth, int buffer_fd)
{
	struct string buf1,buf2;
	int pos=-1,last_pos=-1,i=0;
	struct _stati64 archive_stat;

	unsigned strict = 0;

	fstat(buffer_fd,&archive_stat);

	int flags = 0;

	if(buffer_fd)
	{
		buf1.data=malloc(196655);
		buf2.data=malloc(196655);
		do
		{	
		i+=2;
		lseek(buffer_fd, -(i*196654), SEEK_END); //TODO to check for errors

		if(!readall(buffer_fd,buf1.data,196654)){free(buf1.data);free(buf2.data);return ERROR_EVFS; }
		if(!readall(buffer_fd,buf2.data,196654)){free(buf1.data);free(buf2.data);return ERROR_EVFS; }
		buf1.length=buf2.length=196654;
		last_pos=pos;
		pos=zip_file_sig_pos(&buf1,&buf2);

		}while(pos>-1);
		free(buf1.data);
		free(buf2.data);
		if(last_pos<0)return 0;

		i-=2;
		pos=i*196654 - last_pos;
		lseek(buffer_fd,-pos,SEEK_END);
		buffer->data=malloc(pos+1);
		if(!readall(buffer_fd,buffer->data,pos))return ERROR_EVFS;
		buffer->length=pos;
	}

	char tmp_buf[2];

	char *position = buffer->data, *start, *end = buffer->data + buffer->length;

	uint32_t signature;
	uint16_t version_created, version_extract;
	uint16_t file_flags;
	uint16_t compression;
	uint32_t crc, crc_expected;
	uint32_t size, size_expected, size_real;
	uint16_t length_name, length_extra, length_comment;
	uint32_t attributes;
	uint32_t offset;

	struct string search, path;
	if (cwd->length < location->length) search = string(location->data + cwd->length, location->length - cwd->length);
	else search = string(location->data, 0);

	unsigned depth_real; // depth of entry relative to the current archive
	evfs_protocol_t protocol;
	int status;
	struct dict visited;

	if (!dict_init(&visited, DICT_SIZE_BASE)) return ERROR_MEMORY;

	// Only read support for ZIP archives.
	// Use one struct to store information for archive entries and one for virtual entries made up internally.
	struct file file, file_virtual;
	//file.access = EVFS_READ; TODO looks like this is not necessary
	file_virtual.access = EVFS_READ;

	// assert(depth >= 0);

next:
	while ((end - position) >= (ptrdiff_t)sizeof(signature))
	{
		// All the necessary information is located in the Central Directory.
		// Skip local file entries.
		status = ERROR_EVFS;
		endian_big32(&signature, position);
		position += 4;
		switch (signature)
		{
		case ZIP_LOCALFILE_SIGNATURE:
			if ((end - position) < (ptrdiff_t)26) goto finally; // unexpected EOF

			zip_read(version_extract, position, 16);
			zip_read(file_flags, position, 16);
			zip_read(compression, position, 16);
			position += 4;
			zip_read(crc, position, 32);
			zip_read(size, position, 32);
			position += 4;
			zip_read(length_name, position, 16);
			zip_read(length_extra, position, 16);

			position += length_name + length_extra;

			if (compression)
			{
				if (compression == Z_DEFLATED) file._encoding = EVFS_ENCODING_DEFLATED;
				else if (flags & EVFS_STRICT)
				{
					status = ERROR_CANCEL;
					goto finally;
				}
			}
			else file._encoding = EVFS_ENCODING_IDENTITY;

			// Check flags to see whether this entry contains data descriptor. If so, find where it starts and skip all the data up to its end.
			if (file_flags & 0x8)
			{
				// This entry is stored as a stream so we must find its end by looking for a data descriptor.

				start = position;
				while (true)
				{
					if ((end - position) < (ptrdiff_t)sizeof(signature)) goto finally; // unexpected EOF
					endian_big32(&signature, position);
					position += 4;

					if (signature == ZIP_DATADESCRIPTOR_SIGNATURE) // data descriptor probably starts here
					{
						if ((end - position) < (ptrdiff_t)ZIP_DATA_DESCRIPTOR_SIZE) goto finally; // unexpected EOF

						size_expected = position - start - 4;
						zip_read(crc_expected, position, 32);
						zip_read(size, position, 32);
						position -= 8;

						// Assume that data descriptor starts here if size and CRC match.
						if (size == size_expected)
						{
							file._content.data = start;
							file._content.length = size;
							file.size = size;

							crc = ZIP_CRC32_BASE;
							if (status = evfs_file(&file, zip_crc, &crc)) goto finally;
							if (crc_expected == crc) break;
						}
					}
					// WARNING: ZIP archives containing data descriptors with no corresponding signature are not supported

					position -= 3;
				}
				position += ZIP_DATA_DESCRIPTOR_SIZE;
			}
			else position += size;

			break;

		case ZIP_CENTRALFILE_SIGNATURE:
			if ((end - position) < (ptrdiff_t)42) goto finally; // unexpected EOF

			zip_read(version_created, position, 16);
			zip_read(version_extract, position, 16);
			zip_read(file_flags, position, 16);
			zip_read(compression, position, 16);
			file.mtime = unzip_date(position);
			position += 4;
			zip_read(file._crc, position, 32);
			zip_read(size, position, 32);
			zip_read(size_real, position, 32);
			zip_read(length_name, position, 16);
			zip_read(length_extra, position, 16);
			zip_read(length_comment, position, 16);
			position += 4; // skip unused fields
			zip_read(attributes, position, 32);
			zip_read(offset, position, 32);

			path = string(position, length_name);

			position += length_name + length_extra + length_comment;
			if (end <= position) goto finally; // unexpected EOF

			if (compression)
			{
				if (compression == Z_DEFLATED) file._encoding = EVFS_ENCODING_DEFLATED;
				else if (flags & EVFS_STRICT)
				{
					status = ERROR_CANCEL;
					goto finally;
				}
				else break; // skip this entry
			}
			else file._encoding = EVFS_ENCODING_IDENTITY;

			if (file_flags & 0x1) // encrypted files are not supported
			{
				if (flags & EVFS_STRICT)
				{
					status = ERROR_CANCEL;
					goto finally;
				}
				else break;
			}

			if (path_malicious(&path))
			{
				if (flags & EVFS_STRICT)
				{
					status = ERROR_CANCEL;
					goto finally;
				}
				else break;
			}

			// Consider file with name ending in / as a directory and everything else as a regular file.
			if (path.data[path.length - 1] == '/')
			{
				file.type = EVFS_DIRECTORY;
				file.size = 0;
				path.length -= 1; // TODO is this a good idea?
			}
			else
			{
				file.type = EVFS_REGULAR;
				file.size = size_real;
			}

#if !defined(OS_WINDOWS)
			// Read local file's extra field to find where file content starts. Store the content in file._content .
			start = buffer->data + offset + 28;
			zip_read(length_extra, start, 16);
			start += length_name + length_extra;
			if ((end - start) < size) goto finally; // invalid offset
			file._content = string(start, size);
#else
			lseek(buffer_fd,offset + 28,SEEK_SET);
			if(!readall(buffer_fd,tmp_buf,2))goto finally;
			start = tmp_buf;
			zip_read(length_extra, start, 16);
			file.content.offset = offset+28+2+length_extra+length_name;
			file._content.length=size;
			file.content.length=size;
			file.content.fd=buffer_fd;
#endif

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

					//file_virtual._encoding = EVFS_ENCODING_DIRECTORY; TODO
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
					else break;
				}

				// Mark current entry as visited. If it is already visited (status != 0), we will skip it.
				status = dict_add(&visited, &file.name, 0);
				if (status == ERROR_MEMORY) goto finally;

				bool extract = (file._encoding && (flags & EVFS_EXTRACT) && search.length && !depth_real);

#if !defined(OS_WINDOWS)
				// Extract compressed files in order to recognize if they are archives.
				if (extract)
				{
					struct string location = string(cwd->data, length);
					cwd->data[length] = 0;
					if (evfs_extract(&location, &file, &file_virtual._content))
					{
						if (flags & EVFS_STRICT)
						{
							status = ERROR_CANCEL;
							goto finally;
						}
						else break;
					}
					file_virtual._encoding = 0;
				}
				else
				{
					file_virtual._content = file._content;
					file_virtual._encoding = file._encoding;
				}

				// Recognize nested archive format.
				protocol = 0;
				if ((file.type == EVFS_REGULAR) && (flags & EVFS_NESTED) && (protocol = evfs_recognize(&file_virtual))) file_virtual.type = EVFS_DIRECTORY;
				else
#endif
				{
					file_virtual.size = file.size;
					file_virtual.type = file.type;
				}

#if defined(OS_WINDOWS)
				file_virtual._encoding = file._encoding;
				file_virtual.content.fd = file.content.fd;
				file_virtual.content.length = file.content.length;
				file_virtual.content.offset = file.content.offset;
#endif

				// Invoke callback if this entry is not visited yet.
				if (!status)
				{
					file_virtual.name = file.name;
					if (callback(&file_virtual, argument))
					{
#if !defined(OS_WINDOWS)
						if (extract) munmap(file_virtual._content.data, file_virtual._content.length);
#endif
						if (flags & EVFS_STRICT)
						{
							status = ERROR_CANCEL;
							goto finally;
						}
						else break;
					}
				}

#if !defined(OS_WINDOWS)
				// Browse nested non-compressed archives if depth is enough.
				if (protocol && !file_virtual._encoding && (depth_real < depth))
				{
					cwd->data[length] = '/';
					cwd->length += path.length + 1;
					status = protocol(location, depth - depth_real, &file_virtual._content, cwd, callback, argument, flags);
					cwd->length -= path.length + 1;
				}
				else status = 0;
				if (extract) munmap(file_virtual._content.data, file_virtual._content.length);
#else
				status = 0;
#endif

				if (status) goto finally;
				else break;
			}
			else if ((file.type == EVFS_REGULAR) && EVFS_IN(search, path)) // this entry is an archive containing the directory we want to browse
			{
#if !defined(OS_WINDOWS)
				cwd->length += path.length + 1;
				struct string resource = string(cwd->data, cwd->length - 1);

				dict_term(&visited);

				if (file._encoding)
				{
					status = evfs_extract(&resource, &file, &file_virtual._content);
					if (status) return status;
				}
				else file_virtual._content = file._content;

				// Recognize nested archive format and browse it.
				file_virtual._encoding = EVFS_ENCODING_IDENTITY;
				if (protocol = evfs_recognize(&file_virtual))
				{
					struct string content;

					if (file_virtual._encoding)
					{
						// If the file is compressed twice, replace the cache after the first extraction with a cache after the second extraction.
						if (file._encoding)
						{
							char *key = cache_disable(CACHE_FILE, &resource);
							cache_destroy(key);
							free(key);
						}

						status = evfs_extract(&resource, &file_virtual, &content);
						if (file._encoding) munmap(file_virtual._content.data, file_virtual._content.length);
						if (status) return status;
					}
					else content = file_virtual._content;

					status = protocol(location, depth, &content, cwd, callback, argument, flags);

					cwd->length -= path.length + 1;

					if (file._encoding || file_virtual._encoding) munmap(content.data, content.length);
					return status;
				}
				else
				{
					if (file._encoding) munmap(file_virtual._content.data, file_virtual._content.length);
					return ERROR_UNSUPPORTED;
				}
#else
				// TODO not implemented
#endif
			}
			else break; // this entry is not in location

		case ZIP_CENTRALDIR_END_SIGNATURE:
			// Return error if we were searching for specific entries and none were found.
			status = ((search.length && !visited.count) ? ERROR_MISSING : 0);
			goto finally;

		default:
			status = ERROR_UNSUPPORTED; // unsupported file format
			goto finally;
		}
	}
	// unexpected EOF

finally:
	dict_term(&visited);
	return status;
}
