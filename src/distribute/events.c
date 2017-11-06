#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <mysql/mysql.h>				// libmysql

#include "types.h"
#include "format.h"
#include "stream.h"
#include "log.h"
#include "protocol.h"
#include "filement.h"
#include "proxies.h"
#include "devices.h"
#include "db.h"
#include "uuid.h"
#include "subscribe.h"
#include "authorize.h"
#include "events.h"

#define EVENT_SIZE_MIN (sizeof(" \n") - 1)

#define EVENT_OFF		'-'
#define EVENT_ON		'+'
#define EVENT_RESET		'X'
#define EVENT_MESSAGE	'.'

#define RETRY_WAIT 3
#define RETRY_MAX 3

#define LENGTH_SIZE_LIMIT 4

#define close(f) do {warning(logs("close: "), logi(f), logs(" file="), logs(__FILE__), logs(" line="), logi(__LINE__)); (close)(f);} while (0)

// TODO: this and the next function are almost the same. remove code duplication
static bool event_off(const struct sockaddr_storage *source, const struct vector *clients, struct stream *restrict stream)
{
#define QUERY0 "insert into events(uuid,state) values('"
#define QUERY1 "','-')"

	// Check whether the event is sent from a real proxy.
	const struct credential *cred = authorize_rights(source);
	if (!cred || !(cred->rights & RIGHT_PROXY)) return false;

	struct string before = string("try{receive({\"receiver\":\"devices\",\"action\":\"noproxy\",\"device_id\":\""), after = string("\"});}catch(e){receive(false);}");
	struct string *text;

	uint32_t client_id;
	struct string *uuid_hex;
	char buffer[UUID_SIZE + 1];
	struct string uuid = string(buffer, UUID_SIZE);

	void *db = db_init();
	char query[] = QUERY0 "00000000" "00000000" "00000000" "00000000" QUERY1;

	size_t index;
	for(index = 0; index < clients->length; ++index)
	{
		uuid_hex = vector_get(clients, index);
		device_event(DEVICE_OFF, uuid_hex->data, &cred->hostname);

		// Extract client_id.
		hex2bin(uuid.data, uuid_hex->data, UUID_LENGTH);
		uuid_extract(uuid.data, &client_id, 0);
		if (client_id >= CLIENTS_LIMIT)
		{
			if (db) db_term(db);
			return false; // invalid client_id
		}

		text = string_concat(&before, uuid_hex, &after);
		subscribe_message(text, clients); // TODO: error check
		free(text);

		// Log device state change event.
		if (db)
		{
			format_bytes(query + sizeof(QUERY0) - 1, uuid_hex->data, UUID_SIZE * 2);
			mysql_real_query(db, query, sizeof(query) - 1); // non-crucial; ignore failure
		}
	}

	if (db) db_term(db);
	else ; // TODO log?

	return true;

#undef QUERY1
#undef QUERY0
}
static bool event_on(const struct sockaddr_storage *source, const struct vector *clients, struct stream *restrict stream)
{
#define QUERY0 "insert into events(uuid,state) values('"
#define QUERY1 "','+')"

	// Check whether the event is sent from a real proxy.
	const struct credential *cred = authorize_rights(source);
	if (!cred || !(cred->rights & RIGHT_PROXY)) return false;

	struct string before = string("try{receive({\"receiver\":\"devices\",\"action\":\"changeproxy\",\"device_id\":\""), middle = string("\",\"proxy\":\""), after = string("\"});}catch(e){receive(false);}");
	struct string *text;

	uint32_t client_id;
	struct string *uuid_hex;
	char buffer[UUID_SIZE + 1];
	struct string uuid = string(buffer, UUID_SIZE);

	// TODO tls support: two ports separated by ,
	struct string port;
	char port_buffer[1 + sizeof(cred->port) * 3]; // stores string representation of socket port prefixed with :
	port_buffer[0] = ':';
	port.data = port_buffer;

	void *db = db_init();
	char query[] = QUERY0 "00000000" "00000000" "00000000" "00000000" QUERY1;

	size_t index;
	for(index = 0; index < clients->length; ++index)
	{
		uuid_hex = vector_get(clients, index);
		if (!device_event(DEVICE_ON, uuid_hex->data, &cred->hostname)) ;

		// Extract client_id.
		hex2bin(uuid.data, uuid_hex->data, UUID_LENGTH);
		uuid_extract(uuid.data, &client_id, 0);
		if (client_id >= CLIENTS_LIMIT)
		{
			if (db) db_term(db);
			return false; // invalid client_id
		}

		port.length = format_uint(port_buffer + 1, cred->port) - port_buffer;
		text = string_concat(&before, uuid_hex, &middle, &cred->hostname, &port, &after);
		subscribe_message(text, clients); // TODO: error check
		free(text);

		// Log device state change event.
		if (db)
		{
			format_bytes(query + sizeof(QUERY0) - 1, uuid_hex->data, UUID_SIZE * 2);
			mysql_real_query(db, query, sizeof(query) - 1); // non-crucial; ignore failure
		}
	}

	if (db) db_term(db);
	else ; // TODO log?

	return true;

#undef QUERY1
#undef QUERY0
}

static bool event_proxy_reset(const struct sockaddr_storage *source)
{
	// Check whether the event is sent from a real proxy.
	const struct credential *cred = authorize_rights(source);
	if (!cred || !(cred->rights & RIGHT_PROXY)) return false;

	// TODO event notifications to the interface?

	void *db = db_init();
	if (!db) return false; // database error

	struct string *query;

	// Log device state change event.
	query = db_sql_alloc("insert into events(uuid,state) select uuid,'-' from devices_locations where host='%s'", cred->hostname.data);
	mysql_real_query(db, query->data, query->length); // non-crucial; ignore failure
	free(query);

	query = db_sql_alloc("delete from devices_locations where host='%s'", cred->hostname.data);
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

// WARNING: Message length is currently limited to 9999.
static void event_message(struct sockaddr_storage *restrict source, const struct vector *clients, struct stream *restrict stream)
{
	size_t index = 0;
	struct string buffer;
	buffer.length = 0;

	// Get message size.
	size_t size;
	do
	{
		if (index == buffer.length)
		{
			if (buffer.length >= LENGTH_SIZE_LIMIT) return; // TODO: parse error
			if (stream_read(stream, &buffer, buffer.length + 1)) ; // TODO: error
		}
	} while (buffer.data[index++] != '\n');
	size = strtol(buffer.data, 0, 10);
	stream_read_flush(stream, index);

	// Get message and send it.
	if (stream_read(stream, &buffer, size)) return; // TODO: parse error
	buffer.length = size;
	subscribe_message(&buffer, clients); // TODO: error check
}

void *main_event(void *arg)
{
	struct connection *info = arg;

	struct string buffer;
	struct stream stream;

	char cmd;
	struct string *uuid;
	struct vector clients;

	bool finish;

	if (stream_init(&stream, info->socket))
	{
		warning(logs("event init error: "), logi(info->socket));
		goto free; // TODO: error
	}

	// Determine which event handler to use
	if (stream_read(&stream, &buffer, EVENT_SIZE_MIN + 1)) goto free_stream;
	cmd = *buffer.data;
	if (buffer.data[1] != '\n') goto free_stream;
	finish = (buffer.data[EVENT_SIZE_MIN] == '\n');
	stream_read_flush(&stream, EVENT_SIZE_MIN);

	if (!vector_init(&clients, VECTOR_SIZE_BASE)) goto free_stream; // TODO: memory error

	// Initialize list of UUIDs of devices this event is concerning.
	while (!finish)
	{
		if (clients.length >= INBOX_RECIPIENTS_LIMIT) // TODO: support more than 1 client
		{
			// TODO: error. continuing will waste too much memory
			goto finally;
		}

		if (stream_read(&stream, &buffer, UUID_LENGTH + 2)) goto finally; // TODO: error
		if (buffer.data[UUID_LENGTH] != '\n') goto finally;

		uuid = string_alloc(buffer.data, UUID_LENGTH);
		if (!uuid) goto finally; // TODO: memory error
		if (!vector_add(&clients, uuid))
		{
			free(uuid);
			goto finally; // TODO: memory error
		}

		finish = (buffer.data[UUID_LENGTH + 1] == '\n');
		stream_read_flush(&stream, UUID_LENGTH + 1);
	}
	stream_read_flush(&stream, 1);

	switch (cmd)
	{
	case EVENT_OFF:
		event_off(&info->address, &clients, &stream);
		break;
	case EVENT_ON:
		event_on(&info->address, &clients, &stream);
		break;
	case EVENT_RESET:
		event_proxy_reset(&info->address);
		break;
	case EVENT_MESSAGE:
		event_message(&info->address, &clients, &stream);
		break;
	}

finally:
	{
		size_t index;
		for(index = 0; index < clients.length; ++index) free(vector_get(&clients, index));
		vector_term(&clients);
	}
free_stream:
	stream_term(&stream);
free:
	close(info->socket);
	free(info);
	return 0;
}
