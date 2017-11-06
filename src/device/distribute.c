#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef OS_BSD
# include <arpa/inet.h>
#else
# include <sys/stat.h>
# define WINVER 0x0501
# include <windows.h>
# include <winsock2.h>
# include <ws2tcpip.h>
# include "mingw.h"
# undef close
# define close CLOSE
#endif

#include <pthread.h>			// libpthread

#include "types.h"
#include "log.h"
#include "format.h"
#include "stream.h"

#if !defined(OS_WINDOWS)
# include "io.h"
# include "cache.h"
# include "server.h"
#else
extern int PLATFORM_ID;
# include "../io.h"
# include "../cache.h"
# include "../server.h"
#endif

#include "distribute.h"

// Global variable known only by the device and the distribute server.
#ifndef PUBCLOUD
struct string SECRET;
#else
struct string SECRET = {.data = "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0", .length = 16};
#endif

static struct stream distribute = {.fd = -1};

#if !defined(FAILSAFE)
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static char *version(char *restrict request)
{
	size_t pos;
	uint16_t value;

	extern const struct string app_name;
	extern const struct string app_version;

	// Major version - 2B
	value = 0;
	for(pos = 0; app_version.data[pos] != '.'; ++pos)
		value = value * 10 + (app_version.data[pos] - '0');
	endian_big16(request, &value);
	request += sizeof(uint16_t);

	// Minor version - 2B
	value = 0;
	for(pos += 1; app_version.data[pos] != '.'; ++pos)
		value = value * 10 + (app_version.data[pos] - '0');
	endian_big16(request, &value);
	request += sizeof(uint16_t);

	// Revision - 2B
	value = 0;
	for(pos += 1; pos < app_version.length; ++pos)
		value = value * 10 + (app_version.data[pos] - '0');
	endian_big16(request, &value);
	request += sizeof(uint16_t);

	return request;
}

#if defined(ROUTER)
# include <stdio.h>

static char *serial(char *restrict request)
{
    FILE *fp;
    char buf[128];
    buf[0]=0;

    // WARNING: This is currently not used for TPLINK.

    #if defined(BELKIN)
    fp = popen("mng_cli get VAR_SYSTEM_SN", "r");
    #elif defined(TPLINK)
    size_t length = format_uint(buf,time(0),16) - buf;
    #elif defined(EMULATOR)
    fp = popen("/root/scripts/mng_cli_get_syssn VAR_SYSTEM_SN", "r");
    #endif

    #if !defined(TPLINK)
    if (fp == NULL){printf("mng_cli problem with the execution."); return 0;}
    fgets(buf, sizeof(buf), fp);
    pclose(fp);

    size_t length = strlen(buf);
    if (!length || (length > SERIAL_LENGTH_MAX)) return 0;
    #endif

	uint32_t size = length;
    endian_big32(request, &size);
    request += sizeof(uint32_t);
    format_string(request, buf, length);
    return request + length;
}
#endif

void distribute_term(void)
{
	if (distribute.fd >= 0)
	{
		stream_term(&distribute);
		close(distribute.fd);
	}

#if !defined(FAILSAFE)
	pthread_mutex_destroy(&mutex);
#endif
}

static bool distribute_connect(void)
{
	// Connect to distribute server.
#if defined(TLS)
	int sock = socket_connect(HOST_DISTRIBUTE, PORT_DISTRIBUTE_DEVICE_TLS);
#else
	int sock = socket_connect(HOST_DISTRIBUTE, PORT_DISTRIBUTE_DEVICE);
#endif
	if (sock >= 0)
	{
#if defined(TLS)
		//if (!stream_init_tls_connect(&distribute, sock, HOST_DISTRIBUTE)) return true; // success
		if (!stream_init_tls_connect(&distribute, sock, 0)) return true; // success
#else
		if (!stream_init(&distribute, sock)) return true; // success
#endif
		else
		{
			close(sock);
			distribute.fd = -1;
		}
	}
	return false;
}

struct stream *distribute_request_start(const char uuid[restrict UUID_SIZE], uint16_t cmd, const struct string *restrict request)
{
#if !defined(FAILSAFE)
	pthread_mutex_lock(&mutex);
#endif

	char buffer[REQUEST_SIZE];
	struct string header = string(buffer, REQUEST_SIZE);

	// Generate request header.
	// UUID - 16B
	// Version - 6B
	// Command - 2B
	if (uuid) format_string(buffer, uuid, UUID_SIZE);
	else
	{
#if !defined(OS_WINDOWS)
		extern struct string UUID;
		hex2bin(buffer, UUID.data, UUID_LENGTH);
#else
		extern struct string UUID_WINDOWS;
		hex2bin(buffer, UUID_WINDOWS.data, UUID_LENGTH);
#endif
	}
	endian_big16(version(buffer + UUID_SIZE), &cmd);

	// If there is no distribute connection, create a new one.
	if (distribute.fd < 0)
		if (!distribute_connect())
		{
#if !defined(FAILSAFE)
			pthread_mutex_unlock(&mutex);
#endif
			return 0;
		}

	// Try to send request. Start new connection if the current one is broken.
	// TODO the protocol is to read 1 byte to make sure that the request will be satisfied; fix the protocol
	struct string response;
	int status = stream_write(&distribute, &header);
	if (!status && request) status = stream_write(&distribute, request);
	if (!status) status = stream_write_flush(&distribute);
	if (!status) status = stream_read(&distribute, &response, 1);
	if (status)
	{
		stream_term(&distribute);
		close(distribute.fd);

		switch (status)
		{
		case ERROR_NETWORK:
		case ERROR_AGAIN:
		case ERROR_WRITE:
			if (distribute_connect())
			{
				if (!stream_write(&distribute, &header) && (!request || !stream_write(&distribute, request)) && !stream_write_flush(&distribute) && !stream_read(&distribute, &response, 1)) break;
				else
				{
					distribute_request_finish(false);
					return 0;
				}
			}
		default:
			distribute.fd = -1;
#if !defined(FAILSAFE)
			pthread_mutex_unlock(&mutex);
#endif
			return 0;
		}
	}

	stream_read_flush(&distribute, 1);
	return &distribute;
}

void distribute_request_finish(bool success)
{
	if (!success)
	{
		stream_term(&distribute);
#if defined(OS_WINDOWS)
		if (distribute.fd >= 0)
#endif
		close(distribute.fd);
		distribute.fd = -1;
	}

#if !defined(FAILSAFE)
	pthread_mutex_unlock(&mutex);
#endif
}

#if defined(DEVICE)
char *distribute_register(uint16_t cmd, const struct string *restrict id, const struct string *restrict devname, uint32_t *restrict client_id)
{
	if ((id->length < ID_LENGTH_MIN) || (ID_LENGTH_MAX < id->length)) return 0;
	if ((devname->length < DEVNAME_LENGTH_MIN) || (DEVNAME_LENGTH_MAX < devname->length)) return 0;

	char anonymous_uuid[UUID_SIZE];
# if !defined(BELKIN)
	char body[sizeof(uint32_t) + DEVNAME_LENGTH_MAX + sizeof(uint32_t) + DEVNAME_LENGTH_MAX], *start;
# else
	char body[sizeof(uint32_t) + DEVNAME_LENGTH_MAX + sizeof(uint32_t) + DEVNAME_LENGTH_MAX + sizeof(uint32_t) + SERIAL_LENGTH_MAX], *start;
# endif

	uint16_t platform_id = PLATFORM_ID;
	uint32_t size;

	// Generate anonymous UUID for registration.
	// ************xx**
	// * - gap
	// xx			16b platform_id
	// Fill the gaps with 0 to avoid sending random stack data (which can contain sensitive information).
	format_byte(anonymous_uuid, 0, 16);
	endian_big16(anonymous_uuid + sizeof(uint8_t) * 12, &platform_id);

	// Add id to request body.
	// ID is either email or auth.
	size = id->length;
	endian_big32(body, &size);
	start = format_bytes(body + sizeof(uint32_t), id->data, id->length);

	// Add device name to request body.
	size = devname->length;
	endian_big32(start, &size);
	start = format_bytes(start + sizeof(uint32_t), devname->data, devname->length);

# if defined(ROUTER)
	// Add device serial number.
	start = serial(start);
# endif

	char *uuid = 0;

	struct string buffer = string(body, start - body);
	struct stream *stream = distribute_request_start(anonymous_uuid, cmd, &buffer);
	if (!stream) return 0;

	uint16_t code;

	#define RESPONSE_SIZE_MIN (sizeof(code) + UUID_SIZE + sizeof(uint32_t) + sizeof(uint32_t))

	// Read response.
	if (stream_read(stream, &buffer, RESPONSE_SIZE_MIN)) goto finally;
	endian_big16(&code, buffer.data);
	if (code) goto finally;
	endian_big32(&size, buffer.data + sizeof(code) + UUID_SIZE + sizeof(uint32_t));
	SECRET.length = size;
	if (SECRET.length > SECRET_SIZE_MAX) goto finally;
	if (stream_read(stream, &buffer, RESPONSE_SIZE_MIN + SECRET.length)) goto finally;

	// Get UUID.
	uuid = malloc(sizeof(char) * (UUID_LENGTH + 1));
	if (!uuid) goto finally; // memory error
	*format_hex(uuid, buffer.data + sizeof(code), UUID_SIZE) = 0;

	// Get client_id.
	endian_big32(client_id, buffer.data + sizeof(code) + UUID_SIZE);

	// Get secret.
	SECRET.data = malloc(sizeof(char) * (SECRET.length + 1));
	if (!SECRET.data)
	{
		free(uuid);
		uuid = 0;
		goto finally; // memory error
	}
	*format_bytes(SECRET.data, buffer.data + RESPONSE_SIZE_MIN, SECRET.length) = 0;

	stream_read_flush(stream, RESPONSE_SIZE_MIN + SECRET.length);

finally:
	distribute_request_finish(uuid);
	return uuid;
}
#endif /* DEVICE */

#if defined(BELKIN)

int32_t distribute_user_add(const struct string *restrict auth_id, const struct string *restrict devname)
{
	int32_t client_id = -1;
	char request[sizeof(uint32_t) + ID_LENGTH_MAX + sizeof(uint32_t) + SECRET_SIZE_MAX + sizeof(uint32_t) + DEVNAME_LENGTH_MAX], *start = request;
	struct string buffer;

	// Add auth_id to the request.
	write32(start, auth_id->length);
	start = format_string(start + sizeof(uint32_t), auth_id->data, auth_id->length);

	// Add SECRET to the request.
	write32(start, SECRET.length);
	start = format_string(start + sizeof(uint32_t), SECRET.data, SECRET.length);

	// Add device name to the request.
	write32(start, devname->length);
	start = format_string(start + sizeof(uint32_t), devname->data, devname->length);

	// Send request.
	buffer = string(request, start - request);
	struct stream *stream = distribute_request_start(0, CMD_ADDUSER_OLD, &buffer);
	if (!stream) return -1;

	// Get client_id.
	if (stream_read(stream, &buffer, sizeof(uint32_t))) client_id = -1; // error
	else
	{
		endian_big32(&client_id, buffer.data);
		stream_read_flush(stream, sizeof(uint32_t));
	}

finally:
	distribute_request_finish(client_id >= 0);
	return client_id;
}

bool distribute_user_rm(uint32_t client_id)
{
	char request[sizeof(uint32_t) + sizeof(uint32_t) + SECRET_SIZE_MAX], *start = request;
	struct string buffer;

	write32(start, client_id);
	start += sizeof(uint32_t);

	// Add SECRET to the request.
	write32(start, SECRET.length);
	start = format_string(start + sizeof(uint32_t), SECRET.data, SECRET.length);

	// Send request.
	buffer = string(request, start - request);
	struct stream *stream = distribute_request_start(0, CMD_RMUSER_OLD, &buffer);
	distribute_request_finish(stream);

	return stream;
}

#endif /* BELKIN */
