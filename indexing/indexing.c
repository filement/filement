#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "types.h"
#include "arch.h"
#include "buffer.h"
#include "magic.h"
#include "indexing.h"

#define PATH_SIZE_MAX 4096

#define DB_PATH "/.cache/filement" /* database path relative to the user's home directory */

#define STRING(s) (s), sizeof(s) - 1

// TODO i don't follow symbolic links; is this okay?

static int db_add(int db, struct buffer *restrict filename, const struct stat *restrict info)
{
	struct file file;
	uint16_t length = filename->length;

	endian_big16(&file.mode, &info->st_mode);	// TODO is this right?
	endian_big16(&file.path_length, &length);
	endian_big64(&file.mtime, &info->st_mtime);
	endian_big64(&file.size, &info->st_size);

	{
		ssize_t size;
		char buffer[MAGIC_SIZE];
		int entry;

		entry = open(filename->data, O_RDONLY);
		if (entry >= 0)
		{
			size = read(entry, &buffer, MAGIC_SIZE);
			if (size >= 0)
			{
				uint32_t file_content = content(buffer, size);
				endian_big32(&file.content, &file_content);
			}
			else file.content = 0;
			close(entry);
		}
		else file.content = 0;
	}

	// TODO error check
	write(db, &file, sizeof(file));
	write(db, filename->data, filename->length);

	// TODO this tests that the inode number from stat and directory listing are the same; why do I need this?
	//printf("%s\t%u\t%u\n", name, (unsigned)entry->d_ino, (unsigned)info.st_ino);

	return 0;
}

// Writes indexing data in a database.
static int db_index(int db, struct buffer *restrict filename)
{
	int status;

	struct dirent *entry, *more;
	struct stat info;

	const char *name;
	size_t length;

	entry = malloc(offsetof(struct dirent, d_name) + pathconf(filename->data, _PC_NAME_MAX) + 1);
	if (!entry) return ERROR_MEMORY;

	DIR *dir = opendir(filename->data);
	if (!dir) return -1; // TODO error

	filename->data[filename->length++] = '/';

	while (1)
	{
		if (readdir_r(dir, entry, &more)) return -1; // TODO error
		if (!more) break; // no more entries

		// skip . and ..
		if ((entry->d_name[0] == '.') && (!entry->d_name[1] || ((entry->d_name[1] == '.') && !entry->d_name[2])))
			continue;

		name = entry->d_name;
#if defined(_DIRENT_HAVE_D_NAMLEN)
		length = entry->d_namlen;
#elif !defined(OS_WINDOWS)
		length = strlen(name);
#endif

		if (!buffer_adjust(filename, filename->length + length + 1))
		{
			status = ERROR_MEMORY;
			goto finally;
		}
		memcpy(filename->data + filename->length, name, length);
		filename->length += length;

		filename->data[filename->length] = 0;
		if (lstat(filename->data, &info) < 0) return -1; // TODO error

		if (db_add(db, filename, &info)) return -1; // TODO error

		// Recursively index each directory.
		if ((info.st_mode & S_IFMT) == S_IFDIR)
			if (status = db_index(db, filename))
				goto finally;

		filename->length -= length;
	}

	filename->length -= 1;
	status = 0;

finally:
	closedir(dir);
	free(entry);
	return status;
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

	return creat(path, 0600);
}

// WARNING: target must start with /
static int db_create(const char *restrict target)
{
	int status;

	int db = db_open();
	if (!db) ; // TODO
	write(db, HEADER, 8);

	size_t target_length = strlen(target);

	struct buffer filename = {0};
	if (!buffer_adjust(&filename, target_length + 1))  // make sure there is enough space for terminating / or NUL
	{
		close(db);
		return ERROR_MEMORY;
	}

	filename.data[filename.length++] = '/'; // target path is absolute

	// Check target validity and normalize it.
	size_t start = 1, index = start, length;
	do
	{
		if ((index == target_length) || (target[index] == '/'))
		{
			length = index - start;
			if (length)
			{
				// ignore . and return error for ..
				if (filename.data[start] == '.')
				{
					if (length == 1) filename.length -= 1;
					else if ((length == 2) && (filename.data[start + 1] == '.'))
					{
						status = ERROR_INPUT;
						goto finally;
					}
				}

				if (index == target_length) break;
				else start = index + 1;
			}
			else
			{
				if (index == target_length) break;

				// ignore repeated / characters
				start += 1;
				continue;
			}
		}

		filename.data[filename.length++] = target[index];
	} while (index += 1);

	// put terminating NUL while removing terminating /
	filename.data[filename.length -= (filename.data[filename.length - 1] == '/')] = 0;
	status = db_index(db, &filename);

finally:
	free(filename.data);
	close(db);
	return status;
}

int main(int argc, char *argv[])
{
	struct dirent *entry, *more;
	struct stat info;

	if ((argc < 2) || (argv[1][0] != '/'))
	{
		write(2, STRING("Usage: ./indexing <target_absolute_path>\n"));
		return 0;
	}

	int status = db_create(argv[1]);

	return 0;
}
