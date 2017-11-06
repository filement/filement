#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "types.h"
#include "arch.h"
#include "magic.h"
#include "indexing.h"

#define PATH_SIZE_MAX 4096

#define DB_PATH "/.cache/filement" /* database path relative to the home directory */

#define STRING(s) (s), sizeof(s) - 1

struct pattern
{
	const char *data;
	size_t length;
	size_t chars, asterisks;
};

static char *location = 0; // TODO support multiple search locations
static size_t location_length;
static uint64_t size_min = 0, size_max = -1;
static uint16_t type = 0;
static struct pattern pattern_path = {0}, pattern_name = {0};
static size_t exec_index = 0;
static uint32_t filecontent;

// TODO support -mtime
// TODO support mime type

// TODO option to check if a file is modified or missing

static int usage(int code)
{
	write(1, STRING(
"Usage: find <path> [filters] [-exec command ;]\n"
"\t-type\t Filter by file type\n"
"\t-name\t Filter by filename\n"
"\t-path\t Filter by path\n"
"\t-size\t Filter by size\n"
"\t-content Filter by content\n"
	));
	return code;
}

static bool match_internal(const char *restrict pattern, size_t pattern_length, size_t chars, size_t asterisks, const char *restrict string, size_t string_length)
{
	size_t pattern_index = 0, string_index = 0;

	// Discard strings that won't match because of their length.
	// This also ensures that (string_length - string_index) >= chars.
	if (string_length < chars) return false;

	for(pattern_index = 0; pattern_index < pattern_length; ++pattern_index)
	{
		switch (pattern[pattern_index])
		{
		case '?':
			if (string_index == string_length) return false; // no more string chars to consume
			string_index += 1;
			chars -= 1;
			break;

		case '*':
			{
				size_t asterisk_chars = (string_length - string_index) - chars;
				if (asterisks > 1)
				{
					// The first asterisk can match [0, asterisk_chars] characters.
					// Try each of these values in descending order until a match succeeds.
					// Use recursive call to match_internal() to match the rest of the string with the rest of the pattern.
					pattern += pattern_index + 1;
					pattern_length -= pattern_index + 1;
					string += string_index;
					string_length -= string_index;
					do
					{
						if (match_internal(pattern, pattern_length, chars, asterisks - 1, string + asterisk_chars, string_length - asterisk_chars))
							return true;
					} while (asterisk_chars--);
					return false;
				}
				else
				{
					// The only possibility for the asterisk is to match asterisk_chars characters.
					string_index += asterisk_chars;
					// asterisks -= 1; // asterisks will never be used again
				}
			}
			break;

		// TODO support this
		/*case '[':
			pattern_index += 1;
			break;*/

		case '\\':
			pattern_index += 1;
			// pattern_init() ensures pattern_index is in the bounds of pattern
		default:
			if (string_index == string_length) return false; // no more string chars to consume
			if (pattern[pattern_index] != string[string_index]) return false; // mismatch
			string_index += 1;
			chars -= 1;
			break;
		}
	}

	return (string_index == string_length);
}
static inline int match(const struct pattern *restrict pattern, const char *restrict data, size_t length)
{
	return match_internal(pattern->data, pattern->length, pattern->chars, pattern->asterisks, data, length);
}

// Initializes struct pattern with data with the specified length.
// Counts the * and the non-* characters in the pattern.
// Returns whether the pattern is valid.
static bool pattern_init(struct pattern *restrict pattern, const char *restrict data, size_t length)
{
	size_t index;

	pattern->data = data;
	pattern->length = length;
	pattern->chars = 0;
	pattern->asterisks = 0;

	for(index = 0; index < pattern->length; ++index)
	{
		switch (pattern->data[index])
		{
		case '*':
			pattern->asterisks += 1;
			break;

		// TODO support this
		/*case '[':
			do if (++index == pattern->length) return false;
			while ((pattern->data[index] != ']') || (pattern->data[index - 1] == '\\'));
			pattern->chars += 1;
			break;*/

		case '\\':
			if (++index == pattern->length) return false;
		default:
			pattern->chars += 1;
			break;
		}
	}

	return true;
}

// Generates a normalized absolute path corresponding to the relative path.
static char *normalize(const char *relative, size_t relative_length, size_t *path_length)
{
	char *path;
	size_t length;

	size_t start = 0, index;
	size_t offset;

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

	for(offset = 0; 1; ++offset)
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

	// make sure the path ends with a /
	if (path[length - 1] != '/') path[length++] = '/';

	/*
	if ((length > 1) && (path[length - 1] == '/')) length -= 1; // remove terminating slash for all but the root directory

	//path[length] = 0; // put NUL terminator
	path[length++] = '/'; // put terminating /
	*/

	*path_length = length;
	return path;
}

static int print(const char *path, size_t length, char *argv[])
{
	write(1, path, length);
	write(1, "\n", 1);
	return 0;
}

static int execute(const char *path, size_t length, char *argv[])
{
	if (fork())
	{
		// TODO do something with status
		int status;
		waitpid(0, &status, 0);
		return 0;
	}
	else
	{
		size_t index = exec_index;

		unsigned count;
		char *buffer, *end;

		// The code below relies on the fact that each argument is NUL-terminated.
		do
		{
			// find the number of occurrences of the sequence {} in the argument
			count = 0;
			end = argv[index];
			while (*end)
			{
				if ((end[0] == '{') && (end[1] == '}'))
				{
					count += 1;
					end += 2;
					continue;
				}

				end += 1;
			}

			// replace each occurrence of {} (if any)
			if (count)
			{
				// WARNING: This memory is never freed because it is passed to execvp().
				buffer = malloc((end - argv[index]) + count * (length - 2) + 1);
				if (!buffer) _exit(1); // TODO

				end = argv[index];
				argv[index] = buffer;

				while (*end)
				{
					if ((end[0] == '{') && (end[1] == '}'))
					{
						memcpy(buffer, path, length);
						buffer += length;
						end += 2;
						continue;
					}

					*buffer++ = *end++;
				}
				*buffer = 0;

			}
		} while (argv[++index]);

		if (execvp(argv[exec_index], argv + exec_index))
			_exit(1); // TODO
	}
}

static int find(const unsigned char *db, size_t dbsize, int (*callback)(const char *, size_t, char *[]), char *argv[])
{
	struct file file;
	uint16_t raw;
	uint32_t raw32;
	uint64_t raw64;
	const unsigned char *path;

	size_t offset, left;

	// Check file header.
	if (dbsize < (sizeof(HEADER) - 1)) return ERROR_INPUT; // unexpected EOF
	if (memcmp(db, HEADER, sizeof(HEADER) - 1)) return ERROR_INPUT;
	offset = sizeof(HEADER) - 1;

	while (left = dbsize - offset)
	{
		if (left < sizeof(struct file)) return ERROR_INPUT; // unexpected EOF
		memcpy((void *)&file, db + offset, sizeof(file));
		offset += sizeof(file);

		raw = file.path_length;
		endian_big16(&file.path_length, &raw);
		// if (!file.path_length) ;

		left = dbsize - offset;
		if (left < file.path_length) return ERROR_INPUT; // unexpected EOF
		path = db + offset;
		// if (path[0] != '/') ;
		offset += file.path_length;

		raw = file.mode;
		endian_big16(&file.mode, &raw);

		raw32 = file.content;
		endian_big32(&file.content, &raw32);

		raw64 = file.mtime;
		endian_big64(&file.mtime, &raw64);

		raw64 = file.size;
		endian_big64(&file.size, &raw64);

		// Apply the filters.
		if ((file.path_length < location_length) || memcmp(path, location, location_length)) continue;
		if ((file.size < size_min) || (size_max < file.size)) continue;
		if (type && ((file.mode & S_IFMT) != type)) continue;
		if (filecontent && ((file.content >> 16) != (filecontent >> 16))) continue;
		if (pattern_path.data && !match(&pattern_path, path, file.path_length)) continue;
		if (pattern_name.data)
		{
			// Find the basename of the file and match it against the pattern.
			size_t index;
			for(index = file.path_length - 1; path[index] != '/'; --index)
				;
			if (!match(&pattern_name, path + index + 1, file.path_length - index - 1))
				continue;
		}

		callback(path, file.path_length, argv); // TODO use return code
	}

	return 0;
}

static int db_open(void)
{
	char path[PATH_SIZE_MAX];
	const char *home;
	size_t home_length;

	home = getenv("HOME");
	if (!home) return -1;

	home_length = strlen(home);
	if ((home_length + sizeof(DB_PATH) - 1) >= PATH_SIZE_MAX) return -1;

	memcpy(path, home, home_length);
	memcpy(path + home_length, DB_PATH, sizeof(DB_PATH) - 1);
	path[home_length + sizeof(DB_PATH) - 1] = 0;

	return open(path, O_RDONLY);
}

int main(int argc, char *argv[])
{
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
				if (!pattern_init(&pattern_name, argv[index], strlen(argv[index]))) return usage(1);
			}
			else if (!memcmp(argv[index] + 1, STRING("path")))
			{
				if (++index == argc) return usage(1);
				if (!pattern_init(&pattern_path, argv[index], strlen(argv[index]))) return usage(1);
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

				// TODO K M G T and P are not implemented the same way as in find(1)
				// TODO + and - may not be implemented the same way as in find(1)

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
			else if (!memcmp(argv[index] + 1, STRING("content")))
			{
				if (++index == argc) return usage(1);

				if (!memcmp(argv[index], STRING("text")))
					filecontent = content_text;
				else if (!memcmp(argv[index], STRING("archive")))
					filecontent = content_archive;
				else if (!memcmp(argv[index], STRING("document")))
					filecontent = content_document;
				else if (!memcmp(argv[index], STRING("image")))
					filecontent = content_image;
				else if (!memcmp(argv[index], STRING("audio")))
					filecontent = content_audio;
				else if (!memcmp(argv[index], STRING("video")))
					filecontent = content_video;
				else if (!memcmp(argv[index], STRING("multimedia")))
					filecontent = content_multimedia;
				else
					return usage(1);
			}
			else if (!memcmp(argv[index] + 1, STRING("exec")))
			{
				exec_index = ++index;

				// Find command terminator (semicolon).
				// Replace it with NULL so that (argv + exec_index) can be passed directly to an exec function.
				while (1)
				{
					if (index == argc) return usage(1);
					if ((argv[index][0] == ';') && !argv[index][1])
					{
						if (index == exec_index) return usage(1);
						argv[index] = 0;
						break;
					}
					index += 1;
				}
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
	int db = db_open();
	if (db < 0)
	{
		free(location);
		return -1;
	}
	if (fstat(db, &info) < 0)
	{
		free(location);
		return -1;
	}
	char *buffer = mmap(0, info.st_size, PROT_READ, MAP_PRIVATE, db, 0);
	close(db);
	if (buffer == MAP_FAILED)
	{
		free(location);
		return -1;
	}

	int status = find(buffer, info.st_size, (exec_index ? execute : print), argv);

	munmap(buffer, info.st_size);

	free(location);

	if (status)
	{
		// TODO
	}

	return 0;
}
