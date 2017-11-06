#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef OS_BSD
# include <sys/mman.h>
# include <dirent.h>
# include <arpa/inet.h>
#endif

#if defined(OS_LINUX)
# include <sys/sendfile.h>
#elif defined(OS_MAC)
# include <copyfile.h>
#elif defined(OS_BSD)
# include <sys/mman.h>
#endif

#ifdef OS_WINDOWS
# define WINVER 0x0501
# include <windows.h>
# include <sys/stat.h>
# include <ctype.h>
# include "mingw.h"
# define unlink(f) remove(f)
#endif

#define _ARGS2(func, a0, a1, ...)			   (func)(a0, a1)

#include "types.h"
#include "format.h"
#include "log.h"
#include "stream.h"

#include "sha2.h"
#include "distribute.h"
#include "filement.h"

#ifdef OS_BSD
# include "io.h"
# include "http.h"
# include "http_parse.h"
# include "http_response.h"
# include "cache.h"
# include "evfs.h"
# include "upload.h"
# include "startup.h"
# include "security.h"
#else
# include "../http.h"
# include "../http_parse.h"
# include "../http_response.h"
# include "../io.h"
# include "../cache.h"
# include "../evfs.h"
# include "../upload.h"
# include "../security.h"
# include "../storage.h"
#endif

#ifdef OS_WINDOWS
extern struct string app_location;
extern struct string app_location_name;
#endif

#define TEMP_DIR "/tmp/filement/"

#define RECORD_LENGTH_MAX 1024

#if defined(OS_WINDOWS)
# undef TEMP_DIR
//TODO Change the Temp path in Windows !
char *TEMP_DIR =0;
extern int do_upgrade;
void windows_upgrade();
#endif

#define FILE_X 0x1		/* executable regular file */
#define FILE_D 0x2		/* directory */

struct entry
{
	struct string line[2];
	char checksum[HASH_SIZE];
	unsigned char flags;
};

// TODO: the data received from the network should be checked

// TODO: support renaming the executable file. the problem now is that the new name is not available so it can't be execve() or startup_add()

static struct string *basename_(const struct string *dest)
{
	size_t last = dest->length - 1, index = last;
#ifdef OS_BSD
	while (index && (dest->data[index - 1] != '/'))
#else
	while (index && (dest->data[index - 1] != '\\'))
#endif
		index -= 1;
	return string_alloc(dest->data + index, last - index + 1);
}

static struct string *basedir(const struct string *dest)
{
	size_t last = dest->length - 1, index = last;
#ifdef OS_BSD
	while (index && (dest->data[index - 1] != '/'))
#else
	while (index && (dest->data[index - 1] != '\\'))
#endif
		index -= 1;
	return string_alloc(dest->data, index);
}

static bool upgrade_init(struct vector *restrict download, struct vector *restrict remove)
{
	struct stream *input;
	uint32_t records;
	uint32_t length_src, length_dest;

	struct string buffer;
	size_t index;

	struct entry *file = 0;

	input = distribute_request_start(0, CMD_UPGRADE_LIST, 0);
	if (!input) return false;

	// If a newer version is available, get number of records.
	if (stream_read(input, &buffer, sizeof(uint32_t))) goto error;
	endian_big32(&records, buffer.data);
	stream_read_flush(input, sizeof(uint32_t));

	if (!records) goto error;

	// Parse each record
	while (records--)
	{
		// Get source and destination length.
		if (stream_read(input, &buffer, sizeof(uint32_t))) goto error;
		endian_big32(&length_src, buffer.data);
		if (length_src > RECORD_LENGTH_MAX) goto error;
		stream_read_flush(input, sizeof(uint32_t));
		if (stream_read(input, &buffer, length_src + sizeof(uint32_t))) goto error;
		endian_big32(&length_dest, buffer.data + length_src);
		if (length_dest > RECORD_LENGTH_MAX) goto error;

		// Allocate record value.
		file = malloc(sizeof(struct entry) + length_src + 1 + length_dest + 1);
		if (!file) goto error;
		file->line[0].data = (char *)(file + 1);
		file->line[1].data = (char *)(file + 1) + length_src + 1;

		// Set source and destination length.
		file->line[0].length = length_src;
		file->line[1].length = length_dest;

		// Set source and destination data.
		*format_bytes(file->line[0].data, buffer.data, length_src) = 0;
		stream_read_flush(input, length_src + sizeof(uint32_t));
		if (length_dest)
		{
			if (stream_read(input, &buffer, length_dest)) goto error;
			*format_bytes(file->line[1].data, buffer.data, length_dest) = 0;
			stream_read_flush(input, length_dest);
		}
		else *file->line[1].data = 0;

#if defined(OS_WINDOWS)
		int i;
		for(i = 0; i < length_dest; ++i) if (file->line[1].data[i] == '/') file->line[1].data[i] = '\\';
#endif

		// Set checksum and flags.
		if (stream_read(input, &buffer, HASH_SIZE + 1)) goto error;
		format_bytes(file->checksum, buffer.data, HASH_SIZE);
		file->flags = buffer.data[HASH_SIZE];
		stream_read_flush(input, HASH_SIZE + 1);

		// Write data to the appropriate structure
		if (length_dest)
		{
			// This is a file that should be downloaded
			if (!vector_add(download, file)) goto error;
		}
		else
		{
			// This is a file that should be deleted
			if (!vector_add(remove, file)) goto error;
		}

		file = 0; // avoid double freeing on subsequent error
	}

	distribute_request_finish(true);
	return true;

error:
	free(file);
	distribute_request_finish(false);
	return false;
}

static void upgrade_term(struct vector *restrict vector)
{
	size_t index;
	for(index = 0; index < vector->length; ++index)
		free(vector_get(vector, index));
	vector_term(vector);
}

struct string *upgrade_failsafe(void)
{
	char *var = getenv("HOME");
	if (!var)
	{
		error(logs("$HOME not set"));
		return 0;
	}
	struct string home = string(var, strlen(var));

	struct string *path;
	size_t length;

	#define FILENAME ".filement_failsafe"

	length = home.length + 1 + sizeof(FILENAME) - 1;
	path = malloc(sizeof(struct string) + sizeof(char) * (length + 1));
	if (!path) fail(1, "Memory error");
	path->length = length;
	path->data = (char *)(path + 1);

	memcpy(path->data, home.data, home.length);
	path->data[home.length] = '/';
	memcpy(path->data + home.length + 1, FILENAME, sizeof(FILENAME) - 1);
	path->data[length] = 0;

	#undef FILENAME

	return path;
}

static struct string *temp_filename(const struct string *filename)
{
	struct string *temp = basename_(filename);
	if (!temp) fail(1, "Memory error");

#ifdef OS_BSD
	size_t length = sizeof(TEMP_DIR) - 1 + temp->length;
#else
	size_t temp_len = strlen(TEMP_DIR);
	size_t length = temp_len + 1 + temp->length;
#endif

	struct string *result = malloc(sizeof(struct string) + sizeof(char) * (length + 1));
	if (!result) fail(1, "Memory error");
	result->data = (char *)(result + 1);
	result->length = length;

#ifdef OS_BSD
	memcpy(result->data, TEMP_DIR, sizeof(TEMP_DIR) - 1);
	memcpy(result->data + sizeof(TEMP_DIR) - 1, temp->data, temp->length);
#else
	memcpy(result->data, TEMP_DIR, temp_len);
	result->data[temp_len] = '\\';
	memcpy(result->data + temp_len + 1, temp->data, temp->length);
#endif
	result->data[length] = 0;

	free(temp);
	return result;
}

#if !defined(OS_WINDOWS)
static bool remove_directory(const char *dir)
#else
bool remove_directory(const char *dir)
#endif
{
	if (chdir(dir) < 0) return false;

	DIR *temp = opendir(dir);
	if (!temp) goto error;

	struct dirent *entry;

	while (entry = readdir(temp))
	{
		// TODO: check if this is a directory
		// else remove_directory(entry->d_name);
		/*if (dp->d_namlen == len && !strcmp(dp->d_name, name)) {
			   (void)closedir(dirp);
			   return FOUND;
		}*/

		unlink(entry->d_name);
	}

	chdir("..");
	closedir(temp);
	chdir("..");
	if (rmdir(dir) < 0) goto error;

	return true;

error:
	chdir("..");
	return false;
}

static int checksum_match(const struct string *restrict filename, uint8_t original[HASH_SIZE])
{
#if !defined(OS_WINDOWS)
	struct stat info;
	uint8_t checksum[HASH_SIZE];

	int file = open(filename->data, O_RDONLY);
	if (file < 0) return errno_error(errno);
	if (fstat(file, &info) < 0)
	{
		close(file);
		return errno_error(errno);
	}

	char *content = mmap(0, info.st_size, PROT_READ, MAP_SHARED, file, 0);
	close(file);
	if (content == MAP_FAILED) return errno_error(errno);

	SHA2_CTX context;

	SHA256Init(&context);
	SHA256Update(&context, content, info.st_size);
	SHA256Final(checksum, &context);

	munmap(content, info.st_size);

	return memcmp(checksum, original, HASH_SIZE);
#else
	return 0;
#endif
}

static bool download_temp(struct stream *restrict stream, const struct vector *files)
{
	struct string host = string(HOST_DISTRIBUTE_HTTP);
	struct paste copy;

	// Create temporary directory for downloaded files. If such directory already exists, use it.
	if (mkdir(TEMP_DIR, 0700) < 0)
	{
		if (errno == EEXIST)
		{
#if !defined(OS_WINDOWS)
			struct stat info;
#else
			struct _stati64 info;
#endif
			if (stat(TEMP_DIR, &info) < 0) return false;
			if (!(info.st_mode & S_IFDIR)) return false;
		}
		else return false;
	}

	size_t index;
	struct entry *file;
	struct string *filename;
	struct string *temp;
	int status;
	for(index = 0; index < files->length; ++index)
	{
		file = vector_get(files, index);
		filename = file->line;
		temp = temp_filename(filename + 1);
		if (!temp) goto error;

		copy.source = filename;
		copy.destination = temp;
		format_byte(copy.progress, 0, CACHE_KEY_SIZE);
		copy.mode = 0;

		status = http_transfer(stream, &host, filename, &copy);

		// Make sure the transferred file is the real file.
		if (!status) status = checksum_match(temp, file->checksum);

		free(temp);
		if (status)
		{
			error_("Transfer error");
			goto error;
		}
	}

	return true;

error:
#ifdef OS_BSD
	if (remove_directory(TEMP_DIR) < 0) warning_("Unable to delete directory " TEMP_DIR);
#else
	if (remove_directory(TEMP_DIR) < 0) warning_("Unable to delete the temp dir ");
#endif
	return false;
}

static bool replace(const struct vector *download)
{
	size_t index;
	struct entry *file;
	struct string *src = 0, *dest, *dest_dir = 0;
	int src_fd, dest_fd;

#if !defined(OS_WINDOWS)
	struct stat stat_src, stat_dest_dir;
#else
	struct _stati64 stat_src, stat_dest_dir;
#endif

	// TODO: check for errors

	for(index = 0; index < download->length; ++index)
	{
		file = vector_get(download, index);
		dest = file->line + 1;
		src = temp_filename(dest);
		if (!src) fail(1, "Memory error");
#ifdef OS_BSD
		dest_dir = basedir(dest);
		if (!dest_dir) fail(1, "Memory error");

		// If the source and the destination are on the same filesystem, rename the file. Otherwise move it.
		if ((stat(src->data, &stat_src) < 0) || (stat(dest_dir->data, &stat_dest_dir) < 0)) goto error;
		if (stat_src.st_dev == stat_dest_dir.st_dev)
		{
			if (rename(src->data, dest->data) < 0) goto error;
		}
		else
#endif
		{
			unlink(dest->data);
#if defined(OS_LINUX)
			int src_fd = open(src->data, O_RDONLY);
			int dest_fd = creat(dest->data, stat_src.st_mode);
			sendfile(dest_fd, src_fd, 0, (size_t)stat_src.st_size);
			close(dest_fd);
			close(src_fd);
			unlink(src->data);
#elif defined(OS_MAC)
			if (copyfile(src->data, dest->data, 0, COPYFILE_ALL | COPYFILE_MOVE) < 0) goto error;
#elif defined(OS_WINDOWS)
			CopyFile(src->data,dest->data,FALSE);
#else
			int src_fd = open(src->data, O_RDONLY);
			int dest_fd = creat(dest->data, stat_src.st_mode);

			const unsigned char *src_buf = mmap(0, stat_src.st_size, PROT_READ, MAP_SHARED, src_fd, 0);
			if (src_buf == MAP_FAILED) goto error;

			unsigned char *dest_buf = mmap(0, stat_src.st_size, PROT_WRITE, MAP_SHARED, dest_fd, 0);
			if (dest_buf == MAP_FAILED) goto error;

			memcpy(dest_buf, src_buf, stat_src.st_size);

			munmap(src_buf, stat_src.st_size);
			munmap(dest_buf, stat_src.st_size);

			close(dest_fd);
			close(src_fd);
			unlink(src->data);
#endif
		}

#if !defined(OS_WINDOWS)
		// Set permissions for executables.
		if (file->flags & FILE_X) chmod(dest->data, 0755);
#endif

		free(dest_dir);
		free(src);
	}

	return true;

error:

	free(dest_dir);
	free(src);
	return false;
}

static bool clean(const struct vector *remove)
{
	size_t index;
	struct entry *file;

	// TODO: check for errors

	for(index = 1; index < remove->length; ++index)
	{
		file = vector_get(remove, index);
		unlink(file->line->data);
	}

	return true;
}

#if !defined(OS_WINDOWS)
// Returns true if the device does not require upgrading. Otherwise attempts upgrade.
// If the upgrade succeeds, the application is restarted. Otherwise false is returned.
// WARNING: Only registered devices can be upgraded.
bool filement_upgrade(void)
{
	// Connect to the distribute server and check for a new version.

	// TODO: check for permissions. cancel if permissions are not sufficient

	// Upgrade is supported only for MacOS X and Linux.
#if defined(OS_LINUX)
	// Upgrade requires root permissions.
	if (getuid()) return true;
#elif !defined(OS_MAC)
	return true;
#endif

	struct vector download, remove;

	if (!vector_init(&download, VECTOR_SIZE_BASE)) fail(1, "Memory error");
	if (!vector_init(&remove, VECTOR_SIZE_BASE))
	{
		vector_term(&download);
		fail(1, "Memory error");
	}

	bool upgrade = upgrade_init(&download, &remove);
	if (!upgrade)
	{
		upgrade_term(&remove);
		upgrade_term(&download);
		return true;
	}

	// Remove file 0 is a failsafe for emergency.

	struct stream stream;
	struct string host = string(HOST_DISTRIBUTE_HTTP);
	struct paste copy;

	do
	{
		// Install the new version

		// Connect to the server
		int fd = socket_connect(HOST_DISTRIBUTE_HTTP, PORT_DISTRIBUTE_HTTP);
		if (fd < 0)
		{
			error_("Unable to connect to distribute server");
			break;
		}
		if (stream_init(&stream, fd)) fail(1, "Memory error");

		// Download all the necessary files in a temporary directory
		if (!download_temp(&stream, &download))
		{
			error_("Unable to download upgrade");
			break;
		}

#ifdef DEVICE /* failsafe doesn't need this */
		// Create failsafe in case the upgrade is terminated prematurely.

		struct string *failsafe_src = ((struct entry *)vector_get(&remove, 0))->line, *failsafe_dest = upgrade_failsafe();
		if (!failsafe_dest) break;

		copy.source = failsafe_src;
		copy.destination = failsafe_dest;
		format_byte(copy.progress, 0, CACHE_KEY_SIZE);
		copy.mode = 0;

		if (http_transfer(&stream, &host, failsafe_src, &copy))
		{
			error_("Transfer error while downloading failsafe");
			break;
		}
		if (chmod(failsafe_dest->data, 0100) < 0)
		{
			error_("Cannot set failsafe permissions");
			break; // Make the failsafe executable
		}

		bool success = startup_add(failsafe_dest);
		free(failsafe_dest);
		if (!success)
		{
			error_("Cannot add startup item");
			break;
		}
#endif

		stream_term(&stream);
		close(fd);

		// TODO: make things work without chdir?
		chdir(UPGRADE_PREFIX);

		// WARNING: Operations below can leave the device unusable.

		// Replace files. Delete unnecessary files.
		replace(&download);
		clean(&remove); // TODO: delete unnecessary files (skip failsafe from the remove vector)

		if (remove_directory(TEMP_DIR) < 0) warning_("Unable to delete directory " TEMP_DIR);

		// Make device perform setup when started.
		extern const struct string app_version;
		if (setenv("FILEMENT_SETUP", app_version.data, 1) < 0)
		{
			error_("Unable to set $FILEMENT_SETUP");
			break;
		}

		// Free all allocated resources (memory, file descriptors, mutexes, locks, etc.).
		filement_term();
		// TODO: are there some file descriptors or allocated memory chunks to free?

		// Start the new version.
		debug(logs("Restarting filement..."));
		execl(EXECUTABLE, EXECUTABLE, 0); // TODO: this will cancel all requests
		error(logs("Unable to restart filement"));
	} while (false);

	upgrade_term(&download);
	upgrade_term(&remove);
	return false;
}

#else /* OS_WINDOWS */
//TODO to include windows_upgrade in the interface

bool filement_upgrade(void)
{
	if(!do_upgrade)windows_upgrade();
	return true;
}

bool filement_upgrade_windows(void *storage)
{
	struct vector download, remove;

	if (!vector_init(&download, VECTOR_SIZE_BASE)) fail(1, "Memory error");
	if (!vector_init(&remove, VECTOR_SIZE_BASE))
	{
		vector_term(&download);
		fail(1, "Memory error");
	}

	bool upgrade = upgrade_init(&download, &remove);
	if (!upgrade)
	{
		upgrade_term(&remove);
		upgrade_term(&download);
		return true;
	}

	// Remove file 0 is a failsafe for emergency.

	struct stream stream;
	struct string host = string(HOST_DISTRIBUTE_HTTP);
	struct paste copy;

	do
	{
		// Install the new version

		// Connect to the server
		int fd = socket_connect(HOST_DISTRIBUTE_HTTP, PORT_DISTRIBUTE_HTTP);
		if (fd < 0)
		{
			error_("Unable to connect to distribute server");
			break;
		}
		if (stream_init(&stream, fd)) fail(1, "Memory error");

		//set the tempdir
		TEMP_DIR=(char *)malloc(sizeof(char)*(app_location.length+sizeof("temp")));
		sprintf(TEMP_DIR,"%s%s",app_location.data,"temp");

		// Download all the necessary files in a temporary directory
		if (!download_temp(&stream, &download)) break;

		stream_term(&stream);
		close(fd);

		// Replace files. Delete unnecessary files.
		chdir(app_location.data);
		replace(&download);
		// TODO: delete unnecessary files
		chdir(app_location.data);
		
		if (remove_directory(TEMP_DIR) < 0) warning_("Unable to delete the temp dir ");

		storage_term(storage);
		
		char *exec_loc=malloc(sizeof(char)*(app_location.length+app_location_name.length+1));
		sprintf(exec_loc,"%s%s",app_location.data,app_location_name.data);
		_execl(exec_loc, app_location_name.data, "-s", 0);
		exit(0);
	} while (false);

	upgrade_term(&download);
	upgrade_term(&remove);
	return false;
}

static const char *quote_arg(const char *arg)
{
/* count chars to quote */
int len = 0, n = 0;
int force_quotes = 0;
char *q, *d;
const char *p = arg;
if (!*p) force_quotes = 1;
while (*p) {
if (isspace(*p) || *p == '*' || *p == '?' || *p == '{' || *p == '\'')
force_quotes = 1;
else if (*p == '"')
n++;
else if (*p == '\\') {
int count = 0;
while (*p == '\\') {
count++;
p++;
len++;
}
if (*p == '"')
n += count*2 + 1;
continue;
}
len++;
p++;
}
if (!force_quotes && n == 0)
return strdup(arg);

/* insert \ where necessary */
d = q = malloc(len+n+3);
*d++ = '"';
while (*arg) {
if (*arg == '"')
*d++ = '\\';
else if (*arg == '\\') {
int count = 0;
while (*arg == '\\') {
count++;
*d++ = *arg++;
}
if (*arg == '"') {
while (count-- > 0)
*d++ = '\\';
*d++ = '\\';
}
}
*d++ = *arg++;
}
*d++ = '"';
*d++ = 0;
return q;
}

void windows_upgrade()
{

//TODO to rewrite this function
//TODO error checks

struct vector download, remove;

	if (!vector_init(&download, VECTOR_SIZE_BASE)) fail(1, "Memory error");
	if (!vector_init(&remove, VECTOR_SIZE_BASE))
	{
		vector_term(&download);
		fail(1, "Memory error");
	}

	bool upgrade = upgrade_init(&download, &remove);

	if (!upgrade)
	{
		upgrade_term(&remove);
		upgrade_term(&download);
		return ;
	}
     	upgrade_term(&remove);
		upgrade_term(&download);


char *required_files[]={"filement_upgrade_lib.dll"};
int i=0;
int len=0;
char *src=0,*dst=0,*app=0;

char *path=(char *)malloc(sizeof(char)*(app_location.length+sizeof("backup")));

	sprintf(path,"%s%s",app_location.data,"backup");
	mkdir(path,0700);
	free(path);path=0;
	//TODO, to copy the whole program
	for(i=0;i<1;i++)
	{
	//TODO malloc checks
	len=strlen(required_files[i]);
	src=(char *)malloc(sizeof(char)*(app_location.length+len+1));
	sprintf(src,"%s%s",app_location.data,required_files[i]);
	dst=(char *)malloc(sizeof(char)*(app_location.length+sizeof("backup\\")-1+len+1));
	sprintf(dst,"%s%s%s",app_location.data,"backup\\",required_files[i]);

	CopyFile(src,dst,FALSE);
	free(src);free(dst);
	}
	
	src=(char *)malloc(sizeof(char)*(app_location.length+sizeof("filement_upgrade.exe")));
	sprintf(src,"%s%s",app_location.data,"filement_upgrade.exe");
	dst=(char *)malloc(sizeof(char)*(app_location.length+sizeof("backup\\")-1+sizeof("filement_upgrade.exe")));
	sprintf(dst,"%s%s%s",app_location.data,"backup\\","filement_upgrade.exe");
	
	CopyFile(src,dst,FALSE);
	free(src);
	
	app=(char *)malloc(sizeof(char)*(app_location.length+app_location_name.length+1));
	sprintf(app,"%s%s",app_location.data,app_location_name.data);
	
	//The program is now copied
	const char *const executable[] = {"filement_upgrade.exe", quote_arg(app), 0};
	_execv(dst, executable);
}
#endif
