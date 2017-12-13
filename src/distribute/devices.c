#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <mysql/mysql.h>				// libmysql

#include "types.h"
#include "arch.h"
#include "log.h"
#include "stream.h"
#include "format.h"
#include "security.h"
#include "json.h"
#include "io.h"
#include "protocol.h"
#include "filement.h"
#include "devices.h"
#include "proxies.h"
#include "authorize.h"
#include "uuid.h"
#include "db.h"
#include "subscribe.h"
#include "upgrade.h"

// Given a string literal, generates the string followed by its size.
#define SS(s) (s), (sizeof(s) - 1)

#define BEFORE	"try{receive("
#define AFTER	");}catch(e){receive(false);}"

#define close(f) do {warning(logs("close: "), logi(f), logs(" file="), logs(__FILE__), logs(" line="), logi(__LINE__)); (close)(f);} while (0)

// TODO: make this file independent of the database

// TODO: this is not right
uint32_t ip4_address(const struct sockaddr *address)
{
	unsigned char *ip;
	switch (address->sa_family)
	{
	case AF_INET:
		ip = (unsigned char *)&((struct sockaddr_in *)address)->sin_addr;
		break;
	case AF_INET6:
		ip = ((struct sockaddr_in6 *)address)->sin6_addr.s6_addr + 12;
		break;
	default:
		return 0;
	}
	return (ip[0] << 24) | (ip[1] << 16) | (ip[2] << 8) | ip[3];
}

static const char *uuid_validate(const union json *restrict item)
{
	if (json_type(item) != STRING) return 0;
	const struct string *uuid_hex = &item->string_node;
	if (uuid_hex->length != UUID_LENGTH) return 0;
	size_t index;
	for(index = 0; index < UUID_LENGTH; ++index)
		if (!isxdigit(uuid_hex->data[index]))
			return 0;
	return uuid_hex->data;
}

// Return human-readable form of the address of the device with the given UUID.
// If the device is not connected, return empty string.
union json *devices_locations(const struct vector *restrict hosts)
{
	struct string start = string("select uuid,min(host) from devices_locations where uuid in('"), separator = string("','"), end = string("') group by uuid");
	struct string query;
	query.length = start.length + hosts->length * UUID_LENGTH + (hosts->length - 1) * separator.length + end.length;
	query.data = malloc(query.length + 1);
	if (!query.data) return 0; // memory error

	// Generate SQL query string.
	const char *uuid;
	size_t position = 0;
	size_t index;
	memcpy(query.data + position, start.data, start.length); position += start.length;
	for(index = 0; index < hosts->length; ++index)
	{
		if (index) position = format_string(query.data + position, separator.data, separator.length) - query.data;
		uuid = uuid_validate(vector_get(hosts, index));
		if (!uuid)
		{
			free(query.data);
			return 0; // TODO: bad reqest
		}
		memcpy(query.data + position, uuid, UUID_LENGTH); position += UUID_LENGTH;
	}
	memcpy(query.data + position, end.data, end.length); position += end.length;
	query.data[position] = 0;

	void *restrict db = db_init();
	if (!db)
	{
		free(query.data);
		return 0; // TODO: error
	}
	MYSQL_STMT *stmt = mysql_stmt_init(db);
	if (!stmt)
	{
		free(query.data);
		db_term(db);
		return 0; // TODO: error
	}

	char uuid_data[UUID_LENGTH + 1], address[LOCATION_SIZE_MAX + 1], *pos;
	unsigned long uuid_length, address_length;

	union json *root = 0;

	MYSQL_BIND result[] = {
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = uuid_data,
			.buffer_length = (unsigned long)sizeof(uuid_data),
			.length = &uuid_length
		},
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = address,
			.buffer_length = (unsigned long)sizeof(address),
			.length = &address_length
		}
	};
	if (mysql_stmt_prepare(stmt, query.data, query.length)) goto finally; // TODO: error
	if (mysql_stmt_bind_result(stmt, result) != 0) goto finally; // TODO: error
	if (mysql_stmt_execute(stmt) || mysql_stmt_store_result(stmt)) goto finally; // TODO: error

	free(query.data); query.data = 0;

	struct string key, value;

	root = json_object_old(false);
	if (!root) goto finally;

	while (mysql_stmt_fetch(stmt) != MYSQL_NO_DATA)
	{
		// assert(address_length <= 128); // TODO don't hardcode numbers
		pos = address + address_length;

		*pos++ = ':';
		pos = format_int(pos, PORT_PROXY_HTTP, 10);
		*pos++ = ',';
		pos = format_int(pos, PORT_PROXY_HTTPS, 10);

		key = string(uuid_data, UUID_LENGTH);
		value = string(address, pos - address);

		// TODO: this could be memory error
		if (json_object_insert_old(root, &key, json_string_old(&value)))
		{
			json_free(root); root = 0;
			break;
		}
	}

finally:

	mysql_stmt_close(stmt);
	db_term(db);

	free(query.data);

	return root;
}

bool device_event(const char *restrict state, const char *uuid_hex, const struct string *restrict address)
{
	void *restrict db = db_init();
	if (!db) return false; // database error

	struct string *query = db_sql_alloc(state, uuid_hex, address->data);
	if (!query)
	{
		db_term(db);
		return false; // memory error
	}

	bool success = !mysql_real_query(db, query->data, query->length);
	free(query);
	db_term(db);
	return success;
}

static bool notify_register(const struct header *restrict header, const struct string *uuid_hex, unsigned client_id, int os_id, const struct string *devname, unsigned device_id)
{
	struct string key, value;
	union json *response = json_object_old(false), *device, *item;
	if (!response) return false;

	key = string("receiver");
	value = string("devices");
	if (json_object_insert_old(response, &key, json_string_old(&value))) goto error;

	key = string("action");
	value = string("newdevice");
	if (json_object_insert_old(response, &key, json_string_old(&value))) goto error;

	device = json_object_old(false);
	if (!device) goto error;
	key = string("device");
	if (json_object_insert_old(response, &key, device))
	{
		json_free(device);
		goto error;
	}

	key = string("device_id");
	if (json_object_insert_old(device, &key, json_integer(device_id))) goto error;

	key = string("device_code");
	if (json_object_insert_old(device, &key, json_string_old(uuid_hex))) goto error;

	key = string("name");
	if (json_object_insert_old(device, &key, json_string_old(devname))) goto error;

	key = string("type");
	value = string("pc");
	if (json_object_insert_old(device, &key, json_string_old(&value))) goto error;

	key = string("registered");
	if (json_object_insert_old(device, &key, json_integer(0))) goto error;

	key = string("online");
	if (json_object_insert_old(device, &key, json_boolean(false))) goto error;

	key = string("os_id");
	if (json_object_insert_old(device, &key, json_integer(os_id))) goto error;

	// Add device version
	// TODO fix this?
	if ((header->version_major < 1000) || (header->version_minor < 1000) || (header->revision < 1000))
	{
		char version[3 + 1 + 3 + 1 + 3], *start;
		start = format_uint(version, header->version_major);
		*start++ = '.';
		start = format_uint(start, header->version_minor);
		*start++ = '.';
		start = format_uint(start, header->revision);

		key = string("version");
		value = string(version, start - version);
		if (json_object_insert_old(device, &key, json_string_old(&value))) goto error;
	}

	ssize_t size = json_length(response);
	if (size < 0) goto error;
	size_t data_length = sizeof(BEFORE) - 1 + size + sizeof(AFTER) - 1;
	// assert(data_length < 1000);
	char *message = malloc(1 + 1 + UUID_LENGTH + 1 + 1 + 3 + 1 + data_length), *start;
	if (!message) goto error;

	// Generate message.
	start = message;
	//*start++ = EVENT_MESSAGE; // TODO make this work
	*start++ = '.';
	*start++ = '\n';
	start = format_bytes(start, uuid_hex->data, UUID_LENGTH);
	*start++ = '\n';
	*start++ = '\n';
	start = format_uint(start, data_length);
	*start++ = '\n';
	start = format_bytes(start, SS(BEFORE));
	start = json_dump(start, response);
	start = format_bytes(start, SS(AFTER));

	json_free(response);

	// Connect to the distribute server if there is no open stream.
	int sock = socket_connect(HOST_DISTRIBUTE_EVENT, PORT_DISTRIBUTE_EVENT);
	if (sock >= 0)
	{
		size_t size = start - message;
		bool success = (socket_write(sock, message, size) == size);
		close(sock);
		return success;
	}
	else return false;

error:
	json_free(response);
	return false;
}

// Adds client_id as a user for the device uuid_hex. Makes sure this user is not already added due to database design flaw.
static void olddb_insert(const struct header *restrict header, const char *restrict uuid_hex, uint32_t client_id, uint32_t platform_id, const struct string *devname, bool registered)
{
	const unsigned OS[] = {0, 2, 2, 1, 1, 1, 1, 11, 11, 8, 7, 3, 3, 3, 8, 9, 9, 12};
						// 0  1  2  3  4  5  6   7   8  9 10 11 12 13 14 15 16  17

	// TODO segmentation fault when platform_id is out of range?

	struct string *sql;

	MYSQL *mysql = mysql_init(0);
	mysql_real_connect(mysql, OLD_HOSTNAME, OLD_USERNAME, OLD_PASSWORD, OLD_SCHEMA, OLD_PORT, 0, 0);
	mysql_set_character_set(mysql, "utf8");

	// Do nothing if the user has this device.
	sql = db_sql_alloc("select id from td_devices where device_code='%s' and user_id=%d", uuid_hex, client_id);
	mysql_real_query(mysql, sql->data, sql->length);
	free(sql);
	MYSQL_RES *result = mysql_store_result(mysql);
	bool exists = mysql_num_rows(result);
	mysql_free_result(result);
	if (exists) return;

	sql = db_sql_alloc("insert into td_devices(device_code,`type`,user_id,os_id,registered,added,name) values('%s','pc',%u,%u,%c,NOW(),?)", uuid_hex, client_id, OS[platform_id], (registered ? '1' : '0'));
	MYSQL_STMT *stmt = mysql_stmt_init(mysql);
	mysql_stmt_prepare(stmt, sql->data, sql->length);

	unsigned long length = devname->length;
	MYSQL_BIND value = {
		.buffer_type = MYSQL_TYPE_STRING,
		.buffer = devname->data,
		.buffer_length = length + 1,
		.length = &length
	};
	mysql_stmt_bind_param(stmt, &value);
	mysql_stmt_execute(stmt);

	struct string uuid_string = string((char *)uuid_hex, UUID_LENGTH);
	notify_register(header, &uuid_string, client_id, OS[platform_id], devname, mysql_stmt_insert_id(stmt));

	mysql_stmt_close(stmt);
	free(sql);
	mysql_close(mysql);
}

// WARNING: result must point to a buffer that can store at least limit bytes
// WARNING: limit < 2^31
static ssize_t read_variable(struct stream *restrict stream, char *restrict result, size_t limit)
{
	uint32_t size;
	struct string buffer;

	if (stream_read(stream, &buffer, sizeof(size))) return -1;
	endian_big32(&size, buffer.data);
	stream_read_flush(stream, sizeof(size));
	if (size > limit) return -1; // length limit exceeded

	if (size)
	{
		if (stream_read(stream, &buffer, size)) return -1;
		format_string(result, buffer.data, size);
		stream_read_flush(stream, size);
	}

	return size;
}

static inline bool read_fixed(struct stream *restrict stream, char *restrict result, size_t size)
{
	struct string buffer;
	if (stream_read(stream, &buffer, size)) return false;
	format_string(result, buffer.data, size);
	stream_read_flush(stream, size);
	return true;
}

static void device_adduser_old(const struct header *restrict header, struct stream *restrict stream, uint32_t client_id)
{
	ssize_t size;

	char uuid_hex[UUID_LENGTH + 1];
	*format_hex(uuid_hex, header->uuid, UUID_SIZE) = 0;

	// Get registration information.
	uint16_t platform_id;
	uuid_extract(header->uuid, 0, &platform_id);

	// Read secret from stream to determine whether this really is the device with the given UUID.
	char secret[SECRET_SIZE];
	if (read_variable(stream, secret, SECRET_SIZE) != SECRET_SIZE) return;
	if (!authorize_secret(header->uuid, secret)) return;

	// Get device name.
	char devname_data[DEVNAME_LENGTH_MAX + 1];
	size = read_variable(stream, devname_data, DEVNAME_LENGTH_MAX);
	if (size <= 0) return;
	struct string devname = string(devname_data, size);
	devname.data[devname.length] = 0;

	// Insert data into the database.
	olddb_insert(header, uuid_hex, client_id, platform_id, &devname, true);
}

static void device_rmuser_old(const char *restrict uuid, struct stream *restrict stream, uint32_t client_id)
{
	uint32_t size;

	char uuid_hex[UUID_LENGTH + 1];
	*format_hex(uuid_hex, uuid, UUID_SIZE) = 0;

	// Read secret from stream to determine whether this really is the device with the given UUID.
	char secret[SECRET_SIZE];
	if (read_variable(stream, secret, SECRET_SIZE) != SECRET_SIZE) return;
	if (!authorize_secret(uuid, secret)) return;

	// Delete tuple from the database.

	MYSQL *mysql = mysql_init(0);
	mysql_real_connect(mysql, OLD_HOSTNAME, OLD_USERNAME, OLD_PASSWORD, OLD_SCHEMA, OLD_PORT, 0, 0);
	mysql_set_character_set(mysql, "utf8");
	struct string *sql = db_sql_alloc("delete from td_devices where user_id=%u and device_code=?", client_id);

	MYSQL_STMT *stmt = mysql_stmt_init(mysql);
	mysql_stmt_prepare(stmt, sql->data, sql->length);

	unsigned long length = UUID_LENGTH;
	MYSQL_BIND value = {
		.buffer_type = MYSQL_TYPE_STRING,
		.buffer = uuid_hex,
		.buffer_length = length + 1,
		.length = &length
	};
	mysql_stmt_bind_param(stmt, &value);
	mysql_stmt_execute(stmt);

	mysql_stmt_close(stmt);
	free(sql);
	mysql_close(mysql);
}

bool device_real(void *db, uint16_t platform_id, const struct header *restrict header)
{
	#define SQL0 "select platform_id from versions where platform_id="
	#define SQL1 " and version_major="
	#define SQL2 " and version_minor="
	#define SQL3 " and revision="

	// Generate query sting.
	char query[sizeof(SQL0) - 1 + sizeof(platform_id) * 3 + sizeof(SQL1) - 1 + sizeof(header->version_major) * 3 + sizeof(SQL2) - 1 + sizeof(header->version_minor) * 3 + sizeof(SQL3) - 1 + sizeof(header->revision) * 3 + 1], *position;
	position = format_bytes(query, SQL0, sizeof(SQL0) - 1);
	position = format_uint(position, platform_id, 10);
	position = format_bytes(position, SQL1, sizeof(SQL1) - 1);
	position = format_uint(position, header->version_major, 10);
	position = format_bytes(position, SQL2, sizeof(SQL2) - 1);
	position = format_uint(position, header->version_minor, 10);
	position = format_bytes(position, SQL3, sizeof(SQL3) - 1);
	position = format_uint(position, header->revision, 10);

	if (mysql_real_query(db, query, position - query)) return false;
	MYSQL_RES *result = mysql_store_result(db);
	if (!result) return false;
	bool valid = mysql_num_rows(result);
	mysql_free_result(result);
	return valid;
}

static bool answer(struct stream *restrict stream, int status)
{
	uint16_t code = -status;
	struct string answer = string((char *)&code, sizeof(code));
	return !stream_write(stream, &answer) && !stream_write_flush(stream);
}

static bool device_register(const struct header *restrict header, struct stream *restrict stream, uint32_t client_id)
{
	uint32_t size;

	// Get registration information.
	uint16_t platform_id;
	endian_big16(&platform_id, header->uuid + sizeof(uint8_t) * 12);

	uint16_t flags;
	endian_big16(&flags, header->uuid + sizeof(uint8_t) * 14);

	error(logs("registration: stage 0"));

	// Get device name.
	char devname_data[DEVNAME_LENGTH_MAX + 1];
	size = read_variable(stream, devname_data, DEVNAME_LENGTH_MAX);
	if (size <= 0) return (answer(stream, ERROR_INPUT), false);
	struct string devname = string(devname_data, size);
	devname.data[devname.length] = 0;

	bool success;
	struct string *restrict query = 0;

	struct string *uuid = 0;

	MYSQL_STMT *stmt = 0;
	MYSQL_RES *result;

	error(logs("registration: stage 1"));

	void *restrict db = db_init();
	if (!db) return (answer(stream, ERROR_AGAIN), false); // TODO is ERROR_AGAIN appropriate?

	char uuid_hex[UUID_LENGTH + 1];
	uint64_t container[SECRET_SIZE / sizeof(uint64_t)];
	char *secret;

	char response[sizeof(uint16_t) + UUID_SIZE + sizeof(uint32_t) + sizeof(uint32_t) + SECRET_SIZE];
	uint32_t client_id_string, secret_size_string;
	struct string buffer;

	int status;

	// TODO: handle memory errors better

	error(logs("registration: stage 2"));

	// Check whether the device sent authentic data about platform and verson.
#if !defined(TEST)
	if (!device_real(db, platform_id, header))
	{
		status = ERROR_MISSING;
		goto error;
	}
#endif

	// TODO: on any error below, the device request is lost and nothing restores it. think about fixing this. a transaction can help.

	// TODO: race conditions here. if refresh is done during these operations, the registration may not be finished

	error(logs("registration: stage 3"));

	// Generate device UUID.
	uuid = uuid_alloc(client_id, platform_id);
	if (!uuid)
	{
		status = ERROR_MEMORY;
		goto error;
	}
	*format_hex(uuid_hex, uuid->data, uuid->length) = 0;

	error(logs("registration: stage 4"));

	// When registering a router, delete the old router database (if any).
	if (platform_id == 14)
	{
		// Get serial number.
		char serial[SERIAL_LENGTH_MAX + 1];
		ssize_t length = read_variable(stream, serial, SERIAL_LENGTH_MAX);
		if ((length <= 0) || (length > SERIAL_LENGTH_MAX))
		{
			status = ERROR_INPUT;
			goto error;
		}
		serial[length] = 0;

		char hex[SERIAL_LENGTH_MAX * 2 + 1];
		*format_hex(hex, serial, length) = 0;

		// Get device UUID and secret and send them.
		query = db_sql_alloc("select devices.uuid,secret from serials inner join devices on serials.uuid=devices.uuid where serial=0x%s", hex);
		if (!query)
		{
			status = ERROR_MEMORY;
			goto error;
		}
		success = !mysql_real_query(db, query->data, query->length);
		free(query); query = 0;
		if (!success)
		{
			status = ERROR_AGAIN;
			goto error;
		}

		result = mysql_store_result(db);
		if (!result)
		{
			status = ERROR_MEMORY;
			goto error;
		}
		MYSQL_ROW row = mysql_fetch_row(result);
		if (row)
		{
			char bin[UUID_SIZE];
			hex2bin(bin, row[0], UUID_LENGTH);
			free(uuid);
			uuid = string_alloc(bin, UUID_SIZE);
			*format_string(uuid_hex, row[0], UUID_LENGTH) = 0;

			secret = strdup(row[1]);

			mysql_free_result(result);

			// TODO: REMOVE
			olddb_insert(header, uuid_hex, client_id, platform_id, &devname, true);

			goto finally;
		}
		else
		{
			mysql_free_result(result);

			// Add serial number record to the database.
			query = db_sql_alloc("insert into serials(serial,uuid) values(0x%s,'%s')", hex, uuid_hex);
			if (!query)
			{
				status = ERROR_MEMORY;
				goto error;
			}
			success = !mysql_real_query(db, query->data, query->length);
			free(query); query = 0;
			if (!success)
			{
				status = ERROR_AGAIN;
				goto error;
			}
		}
	}

	// Generate secret.
	secret = (char *)container;
	security_random(secret, SECRET_SIZE);

	error(logs("registration: stage 5"));

	// Register device.
	{
		char hex[SECRET_SIZE * 2 + 1];
		*format_hex(hex, secret, SECRET_SIZE) = 0;
		query = db_sql_alloc("insert into devices(uuid,client_id,platform_id,version_major,version_minor,revision,secret,name,released) values('%s',%u,%u,%u,%u,%u,0x%s,?,%u)", uuid_hex, client_id, platform_id, header->version_major, header->version_minor, header->revision, hex, flags & CMD_FLAG_RELEASE);
		if (!query)
		{
			status = ERROR_MEMORY;
			goto error;
		}

		stmt = mysql_stmt_init(db);
		if (!stmt)
		{
			status = ERROR_MEMORY;
			goto error;
		}
		if (mysql_stmt_prepare(stmt, query->data, query->length))
		{
			status = ERROR_AGAIN;
			goto error;
		}

		error(logs("registration: stage 6"));

		unsigned long length = devname.length;
		MYSQL_BIND value = {
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = devname.data,
			.buffer_length = length + 1,
			.length = &length
		};
		if (mysql_stmt_bind_param(stmt, &value) || mysql_stmt_execute(stmt))
		{
			status = ERROR_AGAIN;
			goto error;
		}

		mysql_stmt_close(stmt);
		stmt = 0;

		error(logs("registration: stage 7"));

		// TODO: REMOVE
		olddb_insert(header, uuid_hex, client_id, platform_id, &devname, (version_support(header, 0, 20) ? 1 : 0));
	}

	free(query);

	db_term(db);

finally:

	// Return response to device.
	client_id_string = htonl(client_id);
	secret_size_string = htonl(SECRET_SIZE);
	if (version_support(header, 0, 20)) // Send success response code if the device expects it.
	{
		status = 0; // int 0 can be used to represent uint16_t 0
		format(response, str((char *)&status, sizeof(uint16_t)), str(uuid->data, uuid->length), str((const char *)&client_id_string, sizeof(uint32_t)), str((const char *)&secret_size_string, sizeof(uint32_t)), str(secret, SECRET_SIZE));
	}
	else format(response, str(uuid->data, uuid->length), str((const char *)&client_id_string, sizeof(uint32_t)), str((const char *)&secret_size_string, sizeof(uint32_t)), str(secret, SECRET_SIZE));
	buffer = string(response, sizeof(response));
	success = (!stream_write(stream, &buffer) && !stream_write_flush(stream));
	free(uuid);
	return success;

error:

	if (stmt) mysql_stmt_close(stmt);
	db_term(db);
	free(uuid);
	free(query);

	answer(stream, status);
	return false;
}

static bool device_upgrade_finish(const struct header *restrict header)
{
	char uuid_hex[UUID_LENGTH + 1];
	*format_hex(uuid_hex, header->uuid, UUID_SIZE) = 0;

	// TODO: handle memory errors better

	bool success;
	struct string *restrict query;
	void *restrict db = db_init();
	if (!db) return false;

	// Update version only if the device sent authentic data about verson for its platform.
	query = db_sql_alloc("update devices set version_major=%u,version_minor=%u,revision=%u where uuid='%s' and platform_id in(select platform_id from versions where version_major=%u and version_minor=%u and revision=%u)", header->version_major, header->version_minor, header->revision, uuid_hex, header->version_major, header->version_minor, header->revision);
	success = (query && !mysql_real_query(db, query->data, query->length) && mysql_affected_rows(db));
	free(query);

	db_term(db);
	return success;
}

static bool device_unregister(const struct header *restrict header)
{
	char uuid_hex[UUID_LENGTH + 1];
	*format_hex(uuid_hex, header->uuid, UUID_SIZE) = 0;

	// TODO: handle memory errors better

	bool success;
	struct string *restrict query;
	void *restrict db = db_init();
	if (!db) return false;

	// Update version only if the device sent authentic data about verson for its platform.
	query = db_sql_alloc("delete from devices where uuid='%s'", uuid_hex);
	success = (query && !mysql_real_query(db, query->data, query->length) && mysql_affected_rows(db));
	free(query);

	db_term(db);

	// Delete device from the old database.
	// TODO change this
	uint32_t client_id;
	uuid_extract(header->uuid, &client_id, 0);
	MYSQL *mysql = mysql_init(0);
	mysql_real_connect(mysql, OLD_HOSTNAME, OLD_USERNAME, OLD_PASSWORD, OLD_SCHEMA, OLD_PORT, 0, 0);
	mysql_set_character_set(mysql, "utf8");
	query = db_sql_alloc("delete from td_devices where user_id=%u and device_code='%s'", client_id, uuid_hex);
	mysql_real_query(db, query->data, query->length);
	free(query);
	mysql_close(mysql);

	return success;
}

static ssize_t device_name(const struct header *restrict header, char *restrict name)
{
	MYSQL_RES *result;
	MYSQL_ROW row;
	void *db = db_init();
	if (!db) return -1;

	// Generate SQL query that selects device name.
	#define QUERY "select name from devices where uuid='"
	char query[sizeof(QUERY) - 1 + UUID_SIZE * 2 + 1]; // 1 byte for closing single quote
	*format_hex(format_bytes(query, QUERY, sizeof(QUERY) - 1), header->uuid, UUID_SIZE) = '\'';
	#undef QUERY

	if (mysql_real_query(db, query, sizeof(query))) goto error;
	result = mysql_store_result(db);
	if (!result) goto error;

	ssize_t response;
	if (row = mysql_fetch_row(result))
	{
		response = strlen(row[0]);
		format_bytes(name, row[0], response);
	}
	else response = ERROR_MISSING;

	mysql_free_result(result);
	db_term(db);
	return response;

error:
	db_term(db);
	return -1;
}

static int device_rename(const struct header *restrict header, const char *restrict name, size_t length)
{
	MYSQL_STMT *stmt;
	void *db = db_init();
	if (!db) return -1;

	// Generate SQL query that selects device name.
	#define QUERY "update devices set name='?' where uuid='"
	char query[sizeof(QUERY) - 1 + UUID_SIZE * 2 + 1]; // 1 byte for closing single quote
	*format_hex(format_bytes(query, QUERY, sizeof(QUERY) - 1), header->uuid, UUID_SIZE) = '\'';
	#undef QUERY

	stmt = mysql_stmt_init(db);
	if (!stmt) goto error;
	if (mysql_stmt_prepare(stmt, query, sizeof(query))) goto error;

	unsigned long name_length = length;
	MYSQL_BIND value = {
		.buffer_type = MYSQL_TYPE_STRING,
		.buffer = (char *)name, // TODO fix this cast
		.buffer_length = name_length + 1,
		.length = &name_length
	};
	if (mysql_stmt_bind_param(stmt, &value) || mysql_stmt_execute(stmt)) goto error;
	if (mysql_stmt_affected_rows(stmt) != 1) goto error;

	mysql_stmt_close(stmt);
	db_term(db);
	return 0;

error:
	if (stmt) mysql_stmt_close(stmt);
	db_term(db);
	return -1;
}

static void device_handle(const struct connection *restrict info, struct stream *restrict stream)
{
	struct header header;
	struct string response;
	ssize_t size;
	char answer;

	// Close the connection on syntax error.

	// Read command header.
	while (read_fixed(stream, (char *)&header, sizeof(header)))
	{
		header.version_major = ntohs(header.version_major);
		header.version_minor = ntohs(header.version_minor);
		header.revision = ntohs(header.revision);
		header.cmd = ntohs(header.cmd);

		if (!version_support(&header, 0, 15))
		{
			error(logs("Unsupported device version: "), logi(header.version_major), logs("."), logi(header.version_minor), logs("."), logi(header.revision));
			return;
		}

		//format_hex(uuid_hex, header.uuid, UUID_SIZE);
		warning(logs("Receiving transmission"));

		// Inform the client that the request will be completed.
		// TODO: find a better solution to the problem with persistent connections
		answer = 1;
		response = string(&answer, 1);
		if (stream_write(stream, &response)) return;

#if RUN_MODE <= 1
		char b[UUID_SIZE * 2];
		format_hex(b, header.uuid, UUID_SIZE);
		debug(logs("Command "), logi(header.cmd), logs(" from "), logs(b, sizeof(b)));
#endif

		switch (header.cmd)
		{
			int32_t client_id;
			char id[ID_LENGTH_MAX + 1];
			uint32_t count;

		case CMD_REGISTER_EMAIL:
			{
				size = read_variable(stream, id, ID_LENGTH_MAX);
				warning(logs("register"));
				if (size <= 0) return; // syntax error
				warning(logs("variables read"));
				struct string email = string(id, size);
				email.data[email.length] = 0;

				client_id = authorize_email(&email);
				if (client_id < 0)
				{
					error(logs("Unauthorized email: "), logs(email.data, email.length));
					goto error; // authorization failed
				}
				if (!device_register(&header, stream, (uint32_t)client_id)) goto error;
			}
			break;
		case CMD_REGISTER_AUTH:
			{
				size = read_variable(stream, id, ID_LENGTH_MAX);
				if (size <= 0) return; // syntax error
				struct string auth = string(id, size);
				auth.data[auth.length] = 0;

				client_id = authorize_id(id, 0);
				if (client_id < 0)
				{
					error(logs("Unauthorized auth_id: "), logs(auth.data, auth.length));
					goto error; // authorization failed
				}
				if (!device_register(&header, stream, (uint32_t)client_id)) goto error;
			}
			break;
		case CMD_UNREGISTER:
			{
				// Read secret from stream to determine whether this really is the device with the given UUID.
				char secret[SECRET_SIZE];
				if (read_variable(stream, secret, SECRET_SIZE) != SECRET_SIZE) return;
				if (!authorize_secret(header.uuid, secret))
				{
					error(logs("CMD_UNREGISTER: Unauthorized secret"));
					goto error;
				}

				// Not implemented. Return success.
				answer = device_unregister(&header);
				response = string(&answer, answer);
				if (stream_write(stream, &response) || stream_write_flush(stream)) return;
			}
			// TODO: implement this
			return; // last command

		case 32762: // TODO work-arounds Filement for Windows 0.19 bug; remove when no longer necessary
		case CMD_UPGRADE_LIST:
			if (upgrade_list(stream, &header))
			{
				count = 0;
				response = string((char *)&count, sizeof(uint32_t));
				if (stream_write(stream, &response) || stream_write_flush(stream)) return;
			}
			break;

		case CMD_UPGRADE_FINISH:
			{
				// Read secret from stream to determine whether this really is the device with the given UUID.
				char secret[SECRET_SIZE];
				if (read_variable(stream, secret, SECRET_SIZE) != SECRET_SIZE) return;
				if (!authorize_secret(header.uuid, secret))
				{
					error(logs("Unauthorized secret"));
					goto error;
				}

				answer = device_upgrade_finish(&header);
				response = string(&answer, 1);
				if (stream_write(stream, &response) || stream_write_flush(stream)) return;
			}
			break;
		case CMD_PROXY_LIST:
			{
				uint32_t address = ip4_address((struct sockaddr *)&info->address);
				struct string *output = proxies_get(address, 3); // TODO: 3 should not be hard-coded

				bool success = !stream_write(stream, output) && !stream_write_flush(stream);
				free(output);
				if (!success) return;
			}
			break;
		case CMD_TOKEN_AUTH_OLD:
			{
				char buffer[sizeof(uint32_t) + 16 + 1];

				if (!read_fixed(stream, id, 32)) return; // TODO
				id[32] = 0;
				client_id = authorize_id(id, buffer + sizeof(uint32_t));
				if (client_id < 0)
				{
					error(logs("Unauthorized auth_id: "), logs(id, 32)); // TODO
					return; // TODO
				}
				write32(buffer, client_id);

				response = string(buffer);
				if (stream_write(stream, &response) || stream_write_flush(stream)) return;
			}
			break;
		case CMD_ADDUSER_OLD:
			{
				size = read_variable(stream, id, ID_LENGTH_MAX);
				if (size <= 0) return;
				struct string auth = string(id, size);
				auth.data[auth.length] = 0;

				client_id = authorize_id(id, 0);
				if (client_id < 0) return;
				device_adduser_old(&header, stream, client_id);

				client_id = htonl(client_id);
				response = string((char *)&client_id, sizeof(client_id));
				if (stream_write(stream, &response) || stream_write_flush(stream)) return;
			}
			break;
		case CMD_RMUSER_OLD:
			if (!read_fixed(stream, (char *)&client_id, sizeof(client_id))) return;
			client_id = ntohl(client_id);
			device_rmuser_old(header.uuid, stream, client_id);
			break;

		case CMD_NAME:
			{
				// Read secret from stream to determine whether this really is the device with the given UUID.
				char secret[SECRET_SIZE];
				if (read_variable(stream, secret, SECRET_SIZE) != SECRET_SIZE) return;
				if (!authorize_secret(header.uuid, secret))
				{
					error(logs("CMD_NAME: Unauthorized secret"));
					goto error;
				}

				char devname[sizeof(uint32_t) + DEVNAME_LENGTH_MAX];
				size = device_name(&header, devname + sizeof(uint32_t));
				if (size <= 0) goto error; // syntax error

				endian_big32(devname, &size);
				response = string(devname, sizeof(uint32_t) + size);
				if (stream_write(stream, &response) || stream_write_flush(stream)) return;
			}
			break;

		case CMD_RENAME:
			{
				// Read secret from stream to determine whether this really is the device with the given UUID.
				char secret[SECRET_SIZE];
				if (read_variable(stream, secret, SECRET_SIZE) != SECRET_SIZE) return;
				if (!authorize_secret(header.uuid, secret))
				{
					error(logs("CMD_RENAME: Unauthorized secret"));
					goto error;
				}

				char devname[DEVNAME_LENGTH_MAX];
				size = read_variable(stream, devname, DEVNAME_LENGTH_MAX);
				if (size <= 0) return; // syntax error

				uint16_t code = -device_rename(&header, devname, size), answer;
				endian_big16(&answer, &code);
				response = string((char *)&answer, sizeof(answer));
				if (stream_write(stream, &response) || stream_write_flush(stream)) return;
			}
			break;

		default:
			return; // invalid command
		}
	}

	return;

error:
	stream_write_flush(stream);
}

void *main_device(void *arg)
{
	struct connection *info = arg;
	struct stream stream;

	if (!stream_init(&stream, info->socket))
	{
		device_handle(info, &stream);
		stream_term(&stream);
	}

	// Make the server discard any further TCP data.
	// http://stackoverflow.com/questions/11436013/writing-to-a-closed-local-tcp-socket-not-failing
	/*struct linger linger = {1, 0};
	setsockopt(info->socket, SOL_SOCKET, SO_LINGER, &linger, sizeof(linger));*/

	close(info->socket);
	free(info);
	return 0;
}

void *main_device_tls(void *arg)
{
	struct connection *info = arg;
	struct stream stream;

	if (!stream_init_tls_accept(&stream, info->socket))
	{
		device_handle(info, &stream);
		stream_term(&stream);
	}

	// Make the server discard any further TCP data.
	// http://stackoverflow.com/questions/11436013/writing-to-a-closed-local-tcp-socket-not-failing
	/*struct linger linger = {1, 0};
	setsockopt(info->socket, SOL_SOCKET, SO_LINGER, &linger, sizeof(linger));*/

	close(info->socket);
	free(info);
	return 0;
}
