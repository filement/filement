#if !defined(OS_WINDOWS)

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "types.h"
#include "format.h"
#include "log.h"
#include "io.h"
#include "startup.h"

#define PATH_LENGTH_MAX 256

#define RUN "/.filement.run"

#define STARTUP_CSH "/.login"
#define STARTUP_BSH "/.profile"

#define STRING(s) {(s), sizeof(s)}
struct string startup_filement = STRING("\nfilement\n");
#undef STRING

static int startup_file = -1;

static char path[PATH_LENGTH_MAX];
static size_t home_size;

// WARNING: Too long home directory paths will cause this function to return error.
bool startup_init(void)
{
#if defined(OS_IOS)
	return true;
#endif

	char *home = getenv("HOME");
	home_size = strlen(home);
	if (!home || (home_size > (PATH_LENGTH_MAX - sizeof(RUN))))
	{
		error(logs("Home path too long."));
		return false;
	}

	format_bytes(format_bytes(path, home, home_size), RUN, sizeof(RUN));

	startup_file = open(path, O_CREAT | O_RDONLY, 0400);
	if (startup_file < 0)
	{
		error(logs("Unable to open run file."));
		return false;
	}

	// Keep an exclusive lock on the run file so that no two instances for the same user can run simultaneously.
	if (flock(startup_file, LOCK_EX | LOCK_NB) == 0)
		return true;

	startup_term();
	return false;
}

void startup_term(void)
{
	close(startup_file);
}

// WARNING: This function assumes that size_t can store any non-negative value of off_t.
static bool added(const char *restrict path, const struct string *restrict command)
{
	int file;
	struct stat info;
	void *buffer = 0;

	size_t *table;
	struct string content;
	bool found = false;
	bool resized = false;

	file = open(path, O_CREAT | O_RDWR, 0644);
	if (file < 0)
		return false;
	if (fstat(file, &info) < 0)
		return false;

	size_t size_max = info.st_size + command->length;
	if (ftruncate(file, size_max) < 0)
		goto finally;
	buffer = mmap(0, size_max, PROT_READ | PROT_WRITE, MAP_SHARED, file, 0);
	if (buffer == MAP_FAILED)
		goto finally;

	// Search for entry starting the application.
	table = kmp_table(command);
	if (!table)
		goto finally;
	content.length = (size_t)info.st_size;
	content.data = buffer;
	found = (kmp_search(command, table, &content) >= 0);
	free(table);

	// Add the application if it's not added yet.
	if (!found)
	{
		memcpy(buffer + info.st_size, command->data + 1, command->length - 1);
		resized = true;
	}

finally:
	if (buffer)
		munmap(buffer, size_max);
	if (!resized)
		ftruncate(file, info.st_size);

	return found;
}

// WARNING: This function assumes that size_t can store any non-negative value of off_t.
static bool removed(const char *restrict path, const struct string *restrict command)
{
	int file;
	struct stat info;
	void *buffer = 0;

	size_t *table;
	struct string content;
	ssize_t index = ERROR_MISSING;

	file = open(path, O_CREAT | O_RDWR, 0644);
	if (file < 0)
		return (errno == ENOENT);
	if (fstat(file, &info) < 0)
		return false;

	buffer = mmap(0, (size_t)info.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, file, 0);
	if (buffer == MAP_FAILED)
		return false;

	// Search for entry starting the application.
	table = kmp_table(command);
	if (!table)
	{
		munmap(buffer, (size_t)info.st_size);
		return false;
	}
	content.length = (size_t)info.st_size;
	content.data = buffer;
	index = kmp_search(command, table, &content);
	free(table);

	if (index >= 0)
	{
		size_t skip = index + command->length;
		buffer += index;
		if (info.st_size > skip)
			memmove(buffer, buffer + command->length, info.st_size - skip);
		ftruncate(file, info.st_size - command->length);
	}

	munmap(buffer, (size_t)info.st_size);
	return true;
}

bool startup_add(const struct string *command)
{
	format_bytes(path + home_size, STARTUP_CSH, sizeof(STARTUP_CSH));
	if (!added(path, command))
		return false;

	format_bytes(path + home_size, STARTUP_BSH, sizeof(STARTUP_BSH));
	if (!added(path, command))
	{
		format_bytes(path + home_size, STARTUP_CSH, sizeof(STARTUP_CSH));
		removed(path, command);
		return false;
	}

	return true;
}

bool startup_remove(const struct string *command)
{
	bool success;

	format_bytes(path + home_size, STARTUP_CSH, sizeof(STARTUP_CSH));
	success = removed(path, command);

	format_bytes(path + home_size, STARTUP_BSH, sizeof(STARTUP_BSH));
	success = removed(path, command) && success;

	return success;
}

#else /* OS_WINDOWS */

#include <sys/stat.h>
#define WINVER 0x0501
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "mingw.h"

#include "types.h"

bool startup_init(void) {return true;}
bool startup_add(const struct string *path) {return true;}
bool startup_remove(const struct string *path) {return true;}
void startup_term(void) {}

#endif
