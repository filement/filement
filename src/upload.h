/*
TODO make documentation of of this
struct paste
	source - name of the source (possibly with some path)
	destination - full path to the destination
	progress - cache key used to access progress information
	fd - file descriptor of the currently opened file for writing
	flags - reserved for future use
	operation_id - ID of the operation that allows the operation to be paused/resumed or cancelled
	dev - device number of the destination (used for checking whether the source and the destination reside in the same filesystem) TODO must be changed to support archive writing
	mode - paste mode (whether the destination exists and whether it is a regular file); used to determine various aspects of the paste operation

paste_init - initializes some of the fields of struct paste
	sources - vector with the filename of each of the sources
	destination - absolute path to the paste destination
	uuid - UUID of the source
	block_id - block_id of the destination
	path - path to the destination in the given block

	sets: mode, dev, destination, flags, operation_id, progress
	source is left unchanged - it must be initialized witch each of the source values consecutively
	fd is left unchanged - it will be initialized when a file is opened for writing
	on error returns error code

paste_create - creates file for pasting with the right path (uses destination and mode from struct paste)
	name - source name with relative path
	type - type of file to create (EVFS_REGULAR or EVFS_DIRECTORY)
	paste - struct initialized with paste_init()

	if destination directory doesn't exist, this function assumes that the caller supplied a new name for the first component of the destination and removes this component. example:
	destination->data	"/home/martin/newdir"
	name				"olddir/blq/filename"
	"/home/martin/newdir" doesn't exist so the composed path is "/home/martin/newdir/blq/filename" ("olddir" is assumed to be the old name of "newdir")

	sets fd of the passed struct paste
	returns the opened file descriptor or negative error code on error

paste_rename - renames a file (uses source, destination and mode from struct paste)
	paste - struct initialized with paste_init()

	if destination name is not provided, the source name is used

struct paste's source is used in 2 places:
- last component is used to determine destination name
- the whole value is used as location in evfs_browse()
*/

struct paste
{
	struct string *source, *destination;
	char progress[CACHE_KEY_SIZE]; // initialized with NULs if no progress is stored
	int fd;
	uint32_t flags; // TODO force, etc.
	int operation_id; // depends on progress
	dev_t dev; // device number of the destination
	unsigned char mode;
};

int paste_init(struct paste *restrict copy, const struct vector *restrict sources, struct string *restrict destination, const struct string *restrict uuid, unsigned block_id, const struct string *restrict path);
int paste_create(const struct string *restrict name, unsigned char type, struct paste *restrict paste);
int paste_rename(const struct paste *restrict paste);
int paste_write(void *argument, unsigned char *buffer, unsigned total);

int http_upload(const struct string *root, const struct string *filename, const struct http_request *request, struct stream *restrict stream, bool append, bool keep, const struct string *status_key);
int http_transfer(struct stream *restrict input, const struct string *restrict host, const struct string *restrict uri, struct paste *restrict copy);
int http_transfer_persist(const struct string *restrict host, unsigned port, const struct string *restrict uri, struct paste *restrict copy);
