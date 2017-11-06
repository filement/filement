int evfs_directory(const struct string *restrict location, unsigned depth, struct buffer *restrict cwd, evfs_callback_t callback, void *argument, unsigned flags);

#undef EVFS_STRICT
#define EVFS_STRICT 0

#undef EVFS_EXTRACT
#define EVFS_EXTRACT 0

#define KiB * 1024
#define MiB * 1024 KiB
#define GiB * 1024 MiB

#define BUFFER_COMPAT (512 MiB)

struct zip_inf_args
{
	int fd;
	struct string *buffer;
};

static unsigned zip_inflate_input_windows(void *arg, unsigned char **result)
{
	struct zip_inf_args *args = arg;
	ssize_t size=0;
	size = read(args->fd, args->buffer->data, WINDOWS_BLOCK_SIZE);
	if (size <= 0) return 0;
	args->buffer->length=size;
			
	*result = args->buffer->data;
	return args->buffer->length;
}

bool evfs_inflate_windows(int fd, int64_t offset, size_t size,int (*callback)(void *, unsigned char *, unsigned), void *arg)
{
struct zip_inf_args args;
args.fd=fd;
args.buffer=string_alloc(0,WINDOWS_BLOCK_SIZE);
lseek64(fd, offset, SEEK_SET);

char window[1 << ZIP_WINDOW_BITS];

	z_stream strm = {
		.zalloc = Z_NULL,
		.zfree = Z_NULL,
		.opaque = Z_NULL
	};
	int status;

	status = inflateBackInit(&strm, ZIP_WINDOW_BITS, window);
	if (status != Z_OK) return false;
	status = inflateBack(&strm, zip_inflate_input_windows, &args, callback, arg);
	inflateBackEnd(&strm);
	free(args.buffer);
	return ((status == Z_STREAM_END) && !strm.avail_in);
}

ssize_t zip_file_sig_pos(const struct string *restrict string0, const struct string *restrict string1)
{
	uint32_t signature = betoh32(ZIP_CENTRALFILE_SIGNATURE);
	struct string pattern = string((char *)&signature, sizeof(uint32_t));
	size_t *table = kmp_table(&pattern);
	if (!table) return -1;

	struct string string0end = string(string0->data + string0->length - 3, 3);
	struct string string1start = string(string1->data, 3);
	struct string *string01 = string_concat(&string0end, &string1start);
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

int evfs_file(const struct file *restrict file, int callback(void *, unsigned char *, unsigned), void *argument)
{
	// TODO: do something for directories
	// if (file->type == EVFS_DIRECTORY) return false; // TODO mark this as an error

	switch (file->_encoding)
	{
	case 0:
		if(!file->content.length)return 0;
		struct string *buffer=string_alloc(0, WINDOWS_BLOCK_SIZE);
		if (!buffer) return -1;

		int64_t index;
		int64_t size;
		 lseek64(file->content.fd, file->content.offset, SEEK_SET);
		for(index = 0; index < file->content.length; index += size)
		{
			size = read(file->content.fd, buffer->data, WINDOWS_BLOCK_SIZE);
			if (size <= 0) return -1;
			buffer->length=size;
			if (callback(argument, buffer->data, buffer->length)) {free(buffer);return -1;}
		}
		free(buffer);
		return 0;

	case EVFS_ENCODING_DEFLATED:
		{
			return (evfs_inflate_windows(file->content.fd,file->content.offset, file->content.length, callback, argument) - 1);
		}

	case EVFS_ENCODING_FD:
        {
            char *buffer;
            int64_t offset = 0, size;
            int status;

            buffer = malloc(BUFFER_COMPAT);
            while (size = file->size - offset)
            {
                readall(file->_fd, buffer, size);
                status = callback(argument, buffer, size);
                if (status) return status;
            }
            free(buffer);
        }
        return -1;

	default:
		return -1;
	}
}

evfs_protocol_t evfs_recognize(struct file *restrict file)
{
	struct string *content = &file->_content;

	// Perform recursive browsing if such is possible and necessary.
	if (content->length < MAGIC_SIZE) return 0;

	// Detect archive format.
	// Compare magic values using the host's endian.
	switch (htobe32(*(uint32_t *)content->data))
	{
	case ZIP_LOCALFILE_SIGNATURE:
	case ZIP_CENTRALDIR_END_SIGNATURE: // empty ZIP archive
		return evfs_zip;

	case 0x08074b50:
		// TODO: support spanned zip archives
		break;
	}

	return 0;
}

// For each file in location, invoke callback(file, argument). Don't follow symbolic links.
int evfs_browse(struct string *restrict location, unsigned depth, evfs_callback_t callback, void *argument, unsigned flags)
{
	char *nul, *end = location->data + location->length;
	int status = 0;

	// Find and stat the longest prefix of location that is accessible through the filesystem.
#if !defined(OS_WINDOWS)
	struct stat info;
#else
		struct string buffer;
		int archive;
		char *locdup;
	struct _stati64 info;
	int i;
	if(location->data[0]=='/')
	{
		location->data[0]=location->data[1];
		location->data[1]=':';
		location->data[2]='\\';

		for(i=3; i<location->length;i++)
			if(location->data[i]=='/')location->data[i]='\\';
	}
	i=0;
	
#endif
	nul = end;
	while (lstat(location->data, &info) < 0)
	{
#if !defined(OS_WINDOWS)
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
#else
		if ((nul - location->data) < location->length) *nul = *SEPARATOR; //TODO check if the archive exists...
		while (*--nul != *SEPARATOR)
			if (nul == location->data)
				return ERROR_MISSING;
		*nul = 0;
#endif
	}
	// Now nul points one character before the relative path to the desired location starting from the stat()ed directory.

	// assert(nul <= end);
	bool browse = (nul == end); // whether current entry should be browsed

	// TODO check if there is cache that could help

	struct file file;

	if ((info.st_mode & S_IFMT) == S_IFDIR) // the requested node is a directory in the filesystem
	{
	#ifdef OS_WINDOWS
	if(!browse) return ERROR_MISSING;
	#endif
		file.name = string("");
		file.size = 0;
		file.mtime = info.st_mtime;
		file.type = EVFS_DIRECTORY;
		file.access = EVFS_READ | EVFS_APPEND; // TODO: modify access?

		// Invoke callback.
		
		if (callback(&file, argument)) return 1;

		if (depth)
		{
			struct buffer cwd = {.data = 0};
			if (buffer_adjust(&cwd, location->length + 1))
			{
				*format_string(cwd.data, location->data, location->length) = *SEPARATOR;
				cwd.length = location->length + 1;
				status = evfs_directory(location, depth - 1, &cwd, callback, argument, flags);
				free(cwd.data);
			}
			else status = ERROR_MEMORY; // either we or the OS doesn't want to spend more memory
		}
	}
	/*else if ((info.st_mode & S_IFMT) == S_IFLNK)
	{
	}*/
	else if ((info.st_mode & S_IFMT) == S_IFREG)
	{
		struct string mapping = {.data = 0};

		evfs_protocol_t protocol;

		file.size = info.st_size;

		if (file.size)
		{
#if !defined(OS_WINDOWS)
			// TODO think about symbolic links
			//int archive = open(location->data, O_RDONLY | O_SYMLINK | O_NOFOLLOW);
			int archive = open(location->data, O_RDONLY);
#else
			archive = open(location->data, O_RDONLY | O_BINARY);
			locdup=strdup(location->data);
			
				location->data[1]=location->data[0];
				location->data[0]='/';
				location->data[2]='/';
			for(;i<location->length;i++)if(location->data[i]=='\\')location->data[i]='/';
#endif
			if (archive < 0) return errno_error(errno);

#if !defined(OS_WINDOWS)
			mapping.length = info.st_size;
			mapping.data = mmap(0, mapping.length, PROT_READ, MAP_SHARED, archive, 0);
			close(archive);
			if (mapping.data == MAP_FAILED) return errno_error(errno);
			file._content = mapping;
#else
		file.content.fd = archive;
		file.content.offset = 0;
		file.content.length = info.st_size;
		buffer.data=0;
		
		buffer.length = 0;
		if(info.st_size <= 196654*2)
		{
		buffer.length = info.st_size;
		buffer.data=malloc(info.st_size+1);
		readall(archive,buffer.data,info.st_size);
		}
		else
		{
		buffer.data=malloc(101);
		buffer.length=100;
		readall(archive,buffer.data,100);
		}
#endif
		}
		else
			{
			file._content = string("");
			locdup=0;
			}
		file._encoding = EVFS_ENCODING_IDENTITY;

		// Recognize archive format if necessary.
#if !defined(OS_WINDOWS)
		if (!browse || (flags & EVFS_NESTED)) protocol = evfs_recognize(&file);
#else
		if (!browse || (flags & EVFS_NESTED))
		{
			file._content = buffer;
			protocol = evfs_recognize(&file);
		}
#endif
		else protocol = 0;

		// Invoke callback for current file if it is in the browsed path.
		if (browse)
		{
			file.name = string("");
			file.mtime = info.st_mtime;
			file.access = EVFS_READ; // TODO: write access?

			if (protocol) file.type = EVFS_DIRECTORY;
			else file.type = EVFS_REGULAR;

			if (status = callback(&file, argument)) goto finally;
		}

		// Browse archive if recursive browsing is possible and necessary.
		if (protocol && (!browse || depth))
		{
			struct buffer cwd = {.data = 0};
			if (buffer_adjust(&cwd, location->length + 1))
			{
#if !defined(OS_WINDOWS)
				struct string content;
#else
				int content;
#endif
				// Extract content if the file is compressed.
				if (file._encoding && (!browse || (flags & EVFS_EXTRACT)))
				{
					struct string entry = string(location->data, nul - location->data);
					if (evfs_extract(&entry, &file, &content))
					{
						if (flags & EVFS_STRICT) status = 1;
						free(cwd.data);
						goto finally;
					}
				}
#if !defined(OS_WINDOWS)
				else content = file._content;
#else
				else content = file.content.fd;
#endif

				// Set cwd.
				if (!browse) *nul = '/';
				*format_string(cwd.data, location->data, location->length) = '/';
				cwd.length = nul + 1 - location->data;

#if !defined(OS_WINDOWS)
				status = protocol(location, depth, &content, &cwd, callback, argument, flags);
				if (content.data != file._content.data) munmap(content.data, content.length);
#else
				if(info.st_size > 196654*2) {
				lseek64(content, 0, SEEK_SET);
				status = protocol(location, &buffer, &cwd, callback, argument, depth,content);
				}
				else
				{
				protocol(location, &buffer, &cwd, callback, argument, depth,0); //TODO pri windows trqbva da se opravi tova za vlojeni archives
				}
#endif
				free(cwd.data);
			}
			else status = ERROR_MEMORY; // either we or the OS doesn't want to spend more memory
		}

finally:
#if !defined(OS_WINDOWS)
		if (mapping.data)
		{
			munmap(mapping.data, mapping.length);
			mapping.data = 0;
		}
#else
		free(buffer.data);
		close(archive);
		free(locdup);locdup=0;
#endif
	}
	else return -32767; // TODO: browse not supported on this file type

#if defined(OS_WINDOWS)
if(location->data[0]!='/')
{
	location->data[1] = location->data[0];
	location->data[0] = '/';
	for(i=2; i<location->length;i++)
		if(location->data[i]=='\\')location->data[i]='/';
}
#endif

	return status;
}
