#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "types.h"
#include "arch.h"
#include "indexing.h"

#define STRING(s) (s), sizeof(s) - 1

#define PATH_SIZE_MAX 4096

#define DB_NAME "/Users/martin/.cache/filement"

// TODO support -name and -path wildcards
// TODO support -exec
// TODO support -mtime
// TODO support mime type

// TODO option to check if a file is modified or missing

// TODO all the return -1 don't do close() or munmap() even if necessary

static int match(const struct string *restrict pattern, const size_t *restrict table, const char *restrict data, size_t length)
{
	struct string name = string((char *)data, length); // TODO fix this cast
	return (kmp_search(pattern, table, &name) >= 0);
}

static int usage(int code)
{
	write(1, STRING("Usage: find <path> [filters]\n\t-type\tFilter by file type\n\t-name\tFilter by filename\n\t-path\tFilter by path\n\t-size\tFilter by size\n"));
	return code;
}

// Generates a normalized absolute path corresponding to the relative path.
static char *normalize(const char *relative, size_t relative_length, size_t *path_length)
{
	char *path;
	size_t length;

	size_t start = 0, index;

	if (relative[0] != '/') // relative path
	{
		path = getcwd(0, 0);
		length = strlen(path);

		// At the position of each slash, store the distance from the previous slash.
		for(index = 1; index < length; ++index)
		{
			if (path[index] == '/')
			{
				// TODO this check can be skipped if the OS guarantees that each component's length is limited
				if ((index - start) > 255) return 0; // TODO

				path[index] = index - start;
				start = index;
			}
		}

		// TODO enlarge the buffer in order to hold the whole path
		path = realloc(path, PATH_SIZE_MAX); // TODO change this; now there is a memory leak on error
		if (!path) return 0; // TODO

		// Put terminating slash if cwd is not the root directory
		if (length > 1)
		{
			path[length] = length - start;
			start = length;
			index = start + 1;
		}
	}
	else // absolute path
	{
		// TODO do this right
		path = malloc(PATH_SIZE_MAX);
		if (!path) return 0; // TODO

		//path[0] = '/';
		index = 0; // TODO not tested
	}

	size_t offset = 0;
	for(; 1; ++offset)
	{
		if ((offset == relative_length) || (relative[offset] == '/')) // end of path component
		{
			switch (index - start)
			{
			case 1:
				if (offset == relative_length) goto finally; // TODO remove code duplication - this line is repeated 4 times
				continue; // skip repeated /

			case 2: // check for .
				if (relative[offset - 1] != '.') break;
				index -= 1;
				if (offset == relative_length) goto finally;
				continue;

			case 3: // check for ..
				if ((relative[offset - 2] == '.') && (relative[offset - 1] == '.'))
				{
					// .. in the root directory points to the same directory
					if (start) start -= path[start];
					index = start + 1;
					if (offset == relative_length) goto finally;
					continue;
				}
				break;
			}

			if (offset == relative_length) break;

			path[index] = index - start;
			start = index;
		}
		else path[index] = relative[offset];

		index += 1;
		if (index == PATH_SIZE_MAX)
		{
			free(path);
			return 0;
		}
	}

finally:

	length = index;

	// Restore the path component separators to slashes.
	do
	{
		index = start;
		start -= path[index];
		path[index] = '/';
	} while (index);

	if ((length > 1) && (path[length - 1] == '/')) length -= 1; // remove terminating slash for all but the root directory

	//path[length] = 0; // put NUL terminator
	path[length++] = '/'; // put terminating /

	*path_length = length;
	return path;
}

int main(int argc, char *argv[])
{
	char *location = 0; // TODO support multiple search locations
	size_t location_length;

	uint64_t size_min = 0, size_max = -1;
	uint16_t type = 0;
	size_t *table_path = 0, *table_name;
	struct string pattern_path, pattern_name;

	size_t index;
	for(index = 1; index < argc; ++index)
	{
		// Parse filters.
		if (*argv[index] == '-')
		{
			if (!memcmp(argv[index] + 1, STRING("type")))
			{
				if (++index == argc) return usage(1);

				if (argv[index][1]) return usage(1);

				// TODO support OS-specific types (door, whiteout, etc.)
				switch (argv[index][0])
				{
				case 'f':
					type = S_IFREG;
					break;
				case 'd':
					type = S_IFDIR;
					break;
				case 'l':
					type = S_IFLNK;
					break;
				case 'b':
					type = S_IFBLK;
					break;
				case 'c':
					type = S_IFCHR;
					break;
				case 'p':
					type = S_IFIFO;
					break;
				case 's':
					type = S_IFSOCK;
					break;
				default:
					return usage(1);
				}
			}
			else if (!memcmp(argv[index] + 1, STRING("name")))
			{
				if (++index == argc) return usage(1);

				pattern_name = string(argv[index], strlen(argv[index]));
				table_name = kmp_table(&pattern_name);
			}
			else if (!memcmp(argv[index] + 1, STRING("path")))
			{
				if (++index == argc) return usage(1);

				pattern_path = string(argv[index], strlen(argv[index]));
				table_path = kmp_table(&pattern_path);
			}
			else if (!memcmp(argv[index] + 1, STRING("size")))
			{
				if (++index == argc) return usage(1);

				char sign = 0;
				char *end;
				size_max = strtol(argv[index], &end, 10);
				if (end == argv[index]) return usage(1);

				if ((*end == '+') || (*end == '-'))
				{
					sign = *end;
					end += 1;
				}

				// TODO K M G T and P are not implemented the same way as in find
				// TODO + and - may not be implemented the same way as in find

				switch (*end)
				{
				case 'c':
					size_min = size_max;
					break;

				case 'P':
					size_max *= 1024;
				case 'T':
					size_max *= 1024;
				case 'G':
					size_max *= 1024;
				case 'M':
					size_max *= 1024;
				case 'k': // compatibility with find(1)
				case 'K':
					size_max *= 2;
				case '\0':
					size_max *= 512;
					if (size_max) size_min = size_max - 511;
					break;

				default:
					return usage(1);
				}

				if (end[0] && end[1]) return usage(1); // there must be no characters after the size

				if (sign == '+') size_max = -1;
				else if (sign == '-') size_min = 0;
			}
			else return usage(1);
		}
		else
		{
			location = argv[index];
			location_length = strlen(location);
		}
	}

	if (!location) return usage(1);

	location = normalize(location, location_length, &location_length);
	if (!location) return -1; // TODO

	struct stat info;
	int db = open(DB_NAME, O_RDONLY);
	if (db < 0) return -1;
	if (fstat(db, &info) < 0) return -1;
	char *buffer = mmap(0, info.st_size, PROT_READ, MAP_PRIVATE, db, 0);
	close(db);
	if (buffer == MAP_FAILED) return -1;

	if (info.st_size < (sizeof(HEADER) - 1)) return -1;
	if (memcmp(buffer, HEADER, sizeof(HEADER) - 1)) return -1;

	off_t offset = sizeof(HEADER) - 1, left;

	struct file file;
	uint16_t raw;
	uint64_t raw64;
	char *path;

next:
	while (left = info.st_size - offset)
	{
		if (left < sizeof(struct file)) return -1; // TODO memory leak
		memcpy((void *)&file, buffer + offset, sizeof(file));
		offset += sizeof(file);

		raw = file.path_length;
		endian_big16(&file.path_length, &raw);
		if (!file.path_length) ; // TODO

		left = info.st_size - offset;
		if (left < file.path_length) return -1; // TODO memory leak
		path = buffer + offset;
		if (path[0] != '/') ; // TODO
		offset += file.path_length;

		raw = file.mode;
		endian_big16(&file.mode, &raw);

		raw64 = file.mtime;
		endian_big64(&file.mtime, &raw64);

		raw64 = file.size;
		endian_big64(&file.size, &raw64);

		// Apply the filters.
		if ((file.path_length < location_length) || memcmp(path, location, location_length)) continue;
		if (table_path && !match(&pattern_path, table_path, path, file.path_length)) continue;
		if ((file.size < size_min) || (size_max < file.size)) continue;
		if (type && ((file.mode & S_IFMT) != type)) continue;
		if (table_name)
		{
			for(index = file.path_length - 1; path[index] != '/'; --index)
				;
			if (!match(&pattern_name, table_name, path + index + 1, file.path_length - index - 1))
				continue;
		}

		write(1, path, file.path_length);
		write(1, "\n", 1);
	}

	munmap(buffer, info.st_size);

	free(table_path);
	free(table_name);

	return 0;
}
