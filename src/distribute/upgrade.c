#include <arpa/inet.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <mysql/mysql.h>				// libmysql

#include "types.h"
#include "log.h"
#include "buffer.h"
#include "format.h"
#include "stream.h"
#include "security.h"
#include "protocol.h"
#include "db.h"
#include "uuid.h"
#include "devices.h"

#define REPOSITORY "/var/www/repository/" 
#define LATEST "latest"
#define FILES "files/"
#define CHECKSUM "/checksums"
#define FAILSAFE "/failsafe"

#define FLAG_EXECUTABLE 0x1

#define PLATFORM_LENGTH_MAX 64
#define VERSION_LENGTH_MAX 64
#define BUFFER_SIZE (sizeof(REPOSITORY) - 1 + PLATFORM_LENGTH_MAX + 1 + VERSION_LENGTH_MAX + 1 + sizeof(CHECKSUM))

struct change
{
	struct string name;
	uint8_t checksum[HASH_SIZE];
	unsigned char flags;
	bool exists; // whether the file exists in the new version
};

static void upgrade_diff(struct vector *restrict changes, int current_checksum, int latest_checksum)
{
	struct change *entry, *entry_old;

	struct stat info;
	struct string current, latest;

	// Get the sizes of the checksum files.
	if (fstat(current_checksum, &info) < 0) return; // TODO
	current.length = info.st_size;
	if (fstat(latest_checksum, &info) < 0) return; // TODO
	latest.length = info.st_size;

	// Map the checksum files to memory.
	current.data = mmap(0, current.length, PROT_READ, MAP_SHARED, current_checksum, 0);
	if (current.data == MAP_FAILED) return; // TODO
	latest.data = mmap(0, latest.length, PROT_READ, MAP_SHARED, latest_checksum, 0);
	if (latest.data == MAP_FAILED)
	{
		munmap(current.data, current.length);
		return;
	}

	struct dict current_dict, latest_dict;

	dict_init(&current_dict, DICT_SIZE_BASE);
	dict_init(&latest_dict, DICT_SIZE_BASE);

	size_t index;
	size_t checksum, flags, filename;

	struct dict_iterator it;
	const struct dict_item *item;
	struct dict_item *item_old;

	// Store each entry from the current version checksum file into a dictionary.
	index = 0;
	while (true)
	{
		checksum = index;
		flags = checksum + HASH_SIZE * 2 + 1;
		index = filename = flags + 2;

		do
		{
			if (index >= current.length)
			{
				if (index == filename) goto current_ready; // EOF
				else goto finally; // invalid checksum file
			}
		} while (current.data[index++] != '\n');

		entry = malloc(sizeof(struct change) + index - 1 - filename);
		if (!entry) goto finally; // memory error
		entry->name.data = (char *)(entry + 1);
		entry->name.length = index - 1 - filename;

		hex2bin(entry->checksum, current.data + checksum, HASH_SIZE * 2);
		entry->flags = ((latest.data[flags] == 'x') ? FLAG_EXECUTABLE : 0);
		entry->exists = false;

		if (index == (filename + 1)) // failsafe
		{
			entry->flags = FLAG_EXECUTABLE;
			if (!vector_add(changes, entry)) goto finally; // memory error
			continue;
		}

		format_bytes(entry->name.data, current.data + filename, entry->name.length);

		if (dict_add(&current_dict, &entry->name, entry)) goto finally; // memory error or file entry duplication
	}

current_ready:

	// Store each entry from the latest version checksum file into a dictionary.
	index = 0;
	while (true)
	{
		checksum = index;
		flags = checksum + HASH_SIZE * 2 + 1;
		index = filename = flags + 2;

		do
		{
			if (index >= latest.length)
			{
				if (index == filename) goto latest_ready; // EOF
				else goto finally; // invalid checksum file
			}
		} while (latest.data[index++] != '\n');

		if (index == (filename + 1)) continue; // skip failsafe

		entry = malloc(sizeof(struct change) + index - 1 - filename);
		if (!entry) goto finally; // memory error

		entry->name.data = (char *)(entry + 1);
		entry->name.length = index - 1 - filename;
		format_bytes(entry->name.data, latest.data + filename, entry->name.length);
		hex2bin(entry->checksum, latest.data + checksum, HASH_SIZE * 2);
		entry->exists = true;
		entry->flags = ((latest.data[flags] == 'x') ? FLAG_EXECUTABLE : 0);

		if (dict_add(&latest_dict, &entry->name, entry)) goto finally; // memory error or file entry duplication
	}

latest_ready:

	// Firstly determine which files require upgrade.
	// Then determine which files no longer exist.

	// Iterate over each file of the latest version.
	for(item = dict_first(&it, &latest_dict); item; item = dict_next(&it, &latest_dict))
	{
		entry = item->value;

		// Store each new or changed file entry in the vector.
		// Delete current version entries that exist in the latest version.
		item_old = dict_remove(&current_dict, &entry->name);
		if (!item_old || memcmp(((struct change *)item_old)->checksum, entry->checksum, HASH_SIZE))
		{
			if (!vector_add(changes, entry)) ; // TODO
			((struct dict_item *)item)->value = 0; // prevent dict_term() from freeing the entry
		}
		free(item_old);
	}

	// Iterate over each file of the current version.
	for(item = dict_first(&it, &current_dict); item; item = dict_next(&it, &current_dict))
	{
		entry = item->value;

		// Store each deleted file entry in the vector.
		if (!dict_get(&latest_dict, &entry->name))
		{
			if (!vector_add(changes, entry)) ; // TODO
			((struct dict_item *)item)->value = 0; // prevent dict_term() from freeing the entry
		}
	}

finally:
	dict_term(&current_dict);
	dict_term(&latest_dict);

	munmap(current.data, current.length);
	munmap(latest.data, latest.length);
}

static struct string *restrict upgrade_response(const struct vector *restrict files, const struct header *restrict header, uint16_t platform_id, const struct string *restrict platform, const struct string *restrict current)
{
	struct change *entry;
	size_t index;
	int flags;

	struct string *response;

	// Devices older than 0.18 treat paths incorrectly. This code fixes their behavior.
	// Initialize destination directory prefix for the current platform.
	// TODO remove this when support for devices < 0.18 is dropped
	struct string prefix = string("");
	if (!version_support(header, 0, 18))
	{
		switch (platform_id)
		{
		case 2:
			prefix = string("/Applications/Filement.app/");
			break;
		case 3:
		case 4:
			prefix = string("/");
			break;
		case 11:
		case 12:
		case 13:
			prefix = string("./");
			break;
		default:
			return 0; // unsupported
		}
	}

	// TODO linux path won't behave as expected - the device has /usr/local/ as prefix while this function will generate usr/local/... (there will be duplication)
	// TODO icons should be in /usr/share/icons/ which is not in the prefix; think of a way to fix this

	size_t size = sizeof(uint32_t);
	for(index = 0; index < files->length; ++index)
	{
		entry = vector_get(files, index);

		if (entry->name.length)
		{
			if (entry->exists) size += sizeof(uint32_t) + 1 + platform->length + 1 + sizeof(LATEST) - 1 + 1 + sizeof(FILES) - 1 + entry->name.length;
			else size += sizeof(uint32_t) + entry->name.length;

			// Devices older than 0.18 treat paths incorrectly. This code fixes their behavior.
			// TODO remove this when support for devices < 0.18 is dropped
			if (version_support(header, 0, 18)) size += sizeof(uint32_t) + (entry->exists ? entry->name.length : 0);
			else size += sizeof(uint32_t) + (entry->exists ? prefix.length + entry->name.length : 0);
		}
		else // failsafe
		{
			size += sizeof(uint32_t) + 1 + platform->length + 1 + current->length + sizeof(FAILSAFE) - 1 + sizeof(uint32_t);
		}

		size += HASH_SIZE + 1;
	}

	response = malloc(sizeof(struct string) + size);
	if (!response) return 0; // memory error
	char *start = response->data = (char *)(response + 1);
	response->length = size;

	*(uint32_t *)start = htobe32(files->length);
	start += sizeof(uint32_t);
	for(index = 0; index < files->length; ++index)
	{
		entry = vector_get(files, index);

		if (entry->name.length)
		{
			if (entry->exists)
			{
				*(uint32_t *)start = htobe32(1 + platform->length + 1 + sizeof(LATEST) - 1 + 1 + sizeof(FILES) - 1 + entry->name.length);
				start += sizeof(uint32_t);

				*start++ = '/';
				start = format_bytes(start, platform->data, platform->length);
				*start++ = '/';
				start = format_bytes(start, LATEST, sizeof(LATEST) - 1);
				*start++ = '/';
				start = format_bytes(start, FILES, sizeof(FILES) - 1);
				start = format_bytes(start, entry->name.data, entry->name.length);

				// Devices older than 0.18 treat paths incorrectly. This code fixes their behavior.
				// TODO remove this when support for devices < 0.18 is dropped
				if (version_support(header, 0, 18))
				{
					*(uint32_t *)start = htobe32(entry->name.length);
					start += sizeof(uint32_t);
					start = format_bytes(start, entry->name.data, entry->name.length);
				}
				else
				{
					*(uint32_t *)start = htobe32(prefix.length + entry->name.length);
					start += sizeof(uint32_t);
					start = format_bytes(start, prefix.data, prefix.length);
					start = format_bytes(start, entry->name.data, entry->name.length);
				}
			}
			else
			{
				*(uint32_t *)start = htobe32(entry->name.length);
				start += sizeof(uint32_t);

				start = format_bytes(start, entry->name.data, entry->name.length);
				*(uint32_t *)start = 0;
				start += sizeof(uint32_t);
			}
		}
		else // failsafe
		{
			*(uint32_t *)start = htobe32(1 + platform->length + 1 + current->length + sizeof(FAILSAFE) - 1);
			start += sizeof(uint32_t);

			*start++ = '/';
			start = format_bytes(start, platform->data, platform->length);
			*start++ = '/';
			start = format_bytes(start, current->data, current->length);
			start = format_bytes(start, FAILSAFE, sizeof(FAILSAFE) - 1);

			*(uint32_t *)start = 0;
			start += sizeof(uint32_t);
		}

		start = format_bytes(start, entry->checksum, HASH_SIZE);
		*start++ = entry->flags;
	}

	return response;
}

static int32_t upgrade_platform(const struct header *restrict header, char *restrict name, size_t *restrict length)
{
	int32_t response = -1;

	uint16_t platform_id;
	uuid_extract(header->uuid, 0, &platform_id);

	void *db = db_init();
	if (!db) return -1;
	MYSQL_STMT *stmt = mysql_stmt_init(db);
	if (!stmt)
	{
		db_term(db);
		return -1;
	}

	// Generate query to get directory name for current platform.
	#define QUERY "select concat(os,'.',device,'.',arch,'.',format) from platforms where platform_id="
	char query[sizeof(QUERY) - 1 + 5 + 1]; // decimal of uint16_t always fits in 5B
	size_t size = format_uint(format_bytes(query, QUERY, sizeof(QUERY) - 1), platform_id) - query;
	query[size] = 0;
	if (mysql_stmt_prepare(stmt, query, size)) goto finally;
	#undef QUERY

	unsigned long platform_length;
	MYSQL_BIND result = {
		.buffer_type = MYSQL_TYPE_STRING,
		.buffer = name,
		.buffer_length = PLATFORM_LENGTH_MAX,
		.length = &platform_length
	};
	if (mysql_stmt_bind_result(stmt, &result) != 0) goto finally;

	if (mysql_stmt_execute(stmt) || mysql_stmt_store_result(stmt) || (mysql_stmt_fetch(stmt) == MYSQL_NO_DATA)) goto finally;

	*length = platform_length;
	response = platform_id;

finally:
	mysql_stmt_close(stmt);
	db_term(db);
	return response;
}

int upgrade_list(struct stream *restrict stream, const struct header *restrict header)
{
#if TEST
	return ERROR_CANCEL;
#endif

#if RUN_MODE <= 1
	char b[UUID_SIZE * 2];
	bin2hex(b, header->uuid, UUID_SIZE);
#endif

	// Devices before 0.17 do not support upgrading.
	if (!version_support(header, 0, 17)) return ERROR_UNSUPPORTED;

#if defined(UPGRADE_TEST)
	// Permit upgrades only for the devices with the specified UUIDs.
	char permitted[][32] = {
		"73d040bba726f4b17ac3a886f7897783", // martomac
//		"d949488b0b9c9a2de1b798cd94c48fb4", // nikov
//		"afc46f322d859909d60e5061fe9ec66f", // plamen
	};
	char bin[UUID_SIZE];
	size_t n;
	for(n = 0; n < sizeof(permitted) / sizeof(*permitted); ++n)
	{
		hex2bin(bin, permitted[n], UUID_SIZE * 2);
		if (!memcmp(header->uuid, bin, UUID_SIZE))
			goto test;
	}
	debug(logs("Upgrade skipped for "), logs(b, UUID_SIZE * 2), logs(" version "), logi(header->version_major), logs("."), logi(header->version_minor), logs("."), logi(header->revision));
	return ERROR_CANCEL;
test: ;
#endif

	debug(logs("Looking for upgrade for "), logs(b, UUID_SIZE * 2), logs(" version "), logi(header->version_major), logs("."), logi(header->version_minor), logs("."), logi(header->revision));

	char buffer[BUFFER_SIZE], *start = format_bytes(buffer, REPOSITORY, sizeof(REPOSITORY) - 1);
	struct string platform;
	int32_t platform_id = upgrade_platform(header, start, &platform.length);
	if (platform_id < 0) return ERROR_INPUT;
	platform.data = start;

	start += platform.length;
	*start++ = '/';

	// Generate current version string.
	// e.g. 0.16.2
	char current[VERSION_LENGTH_MAX], *position;
	size_t current_length;
	position = format_uint(current, header->version_major);
	*position++ = '.';
	position = format_uint(position, header->version_minor);
	*position++ = '.';
	position = format_uint(position, header->revision);
	current_length = position - current;

	// Generate path to the latest version.
	// e.g. /var/www/repository/Linux.PC.x86_64.ELF/latest
	size_t length = format_bytes(start, LATEST, sizeof(LATEST) - 1) - buffer;

	// Check whether the current version is the latest. If so, there is nothing to upgrade.
	char latest[VERSION_LENGTH_MAX];
	buffer[length] = 0;
	ssize_t latest_length = readlink(buffer, latest, VERSION_LENGTH_MAX);
	if (latest_length < 0) return -1; // TODO
	if ((current_length == latest_length) && !memcmp(current, latest, current_length))
		return ERROR_MISSING; // no upgrade available

	// Open the checksum file for the latest version.
	*format_bytes(buffer + length, CHECKSUM, sizeof(CHECKSUM) - 1) = 0;
	int latest_checksum = open(buffer, O_RDONLY);
	if (!latest_checksum) return -1; // TODO

	// Open the checksum file for the current version.
	start = format_bytes(start, current, current_length);
	*format_bytes(start, CHECKSUM, sizeof(CHECKSUM) - 1) = 0;
	int current_checksum = open(buffer, O_RDONLY);
	if (!current_checksum)
	{
		close(latest_checksum);
		return -1; // TODO
	}

	// Store data about each changed file in a vector.
	struct vector changes;
	size_t index;
	if (!vector_init(&changes, VECTOR_SIZE_BASE))
	{
		close(latest_checksum);
		close(current_checksum);
		return ERROR_MEMORY;
	}
	upgrade_diff(&changes, current_checksum, latest_checksum);
	close(latest_checksum);
	close(current_checksum);

	debug(logs("There are "), logi(changes.length - 1), logs(" changes for "), logs(b, UUID_SIZE * 2), logs(" version "), logi(header->version_major), logs("."), logi(header->version_minor), logs("."), logi(header->revision));

	if (changes.length > 1) // failsafe is always added to changes
	{
		struct string version = string(current, current_length);
		struct string *response = upgrade_response(&changes, header, platform_id, &platform, &version);
		if (response)
		{
			if (stream_write(stream, response) || stream_write_flush(stream))
			{
				for(index = 0; index < changes.length; ++index) free(vector_get(&changes, index));
				vector_term(&changes);
				return -1; // TODO error
			}
			free(response);
		}
		else ERROR_MEMORY; // TODO this can be ERROR_UNSUPPORTED
	}

	for(index = 0; index < changes.length; ++index) free(vector_get(&changes, index));
	vector_term(&changes);

	return 0;
}
