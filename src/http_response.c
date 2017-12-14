#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#if defined(PUBCLOUD)
# include <sys/fsuid.h>
#endif

#ifdef OS_WINDOWS
# define WINDOWS_BLOCK_SIZE 0x200000
# include <windows.h>
# include <winsock2.h>
# include <sys/stat.h>
# include "../../windows/mingw.h"
# define ELOOP -1
# define ETIMEDOUT WSAETIMEDOUT
#endif

#include "types.h"
#include "log.h"
#include "format.h"
#include "aes.h"
#include "protocol.h"
#include "actions.h"
#include "magic.h"
#include "download.h"
#include "dlna/dlnahttp.h"
#include "dlna/ssdp.h"

#if defined(DEVICE)
# include "access.h"
#endif

#define PHRASE_200 "OK"
#define PHRASE_204 "No Content"
#define PHRASE_206 "Partial Content"
#define PHRASE_301 "Moved Permanently"
#define PHRASE_304 "Not Modified"
#define PHRASE_400 "Bad Request"
#define PHRASE_403 "Forbidden"
#define PHRASE_404 "Not Found"
#define PHRASE_405 "Method Not Allowed"
#define PHRASE_408 "Request Timeout"
#define PHRASE_411 "Length Required"
#define PHRASE_413 "Request Entity Too Large"
#define PHRASE_414 "Request-URI Too Long"
#define PHRASE_415 "Unsupported Media Type"
#define PHRASE_416 "Requested Range Not Satisfiable"
#define PHRASE_500 "Internal Server Error"
#define PHRASE_501 "Not Implemented"
#define PHRASE_502 "Bad Gateway"
#define PHRASE_503 "Service Unavailable"

#define WEB_ROOT "/var/www/repository/"
#define WEB_INDEX "index.html"

// Number of bytes required to store string representation of content size. Should work for both base 10 and 16.
#define SIZE_LENGTH_MAX (sizeof(off_t) * 3)

#define RESPONSE_IDENTITY 1
// #defien RESPONSE_DEFLATE 2

#define HTTP_CODE_LENGTH 3

// TODO: string() can be used instead of string_static() with newer versions of gcc
#define string_static(s) {(s), sizeof(s) - 1}
static const struct string methods[] = {
	[METHOD_HEAD] = string_static("HEAD"),
	[METHOD_GET] = string_static("GET"),
	[METHOD_POST] = string_static("POST"),
	[METHOD_OPTIONS] = string_static("OPTIONS"),
	[METHOD_PUT] = string_static("PUT"),
	[METHOD_DELETE] = string_static("DELETE"),
	[METHOD_SUBSCRIBE] = string_static("SUBSCRIBE"),
	[METHOD_NOTIFY] = string_static("NOTIFY")
};
#undef string_static

// TODO maybe hardcode terminator chars in the code
static const struct string version = {"HTTP/1.1", 8}, terminator = {"\r\n", 2};

// TODO: Serve default file based on the returned status (? call http_download())

// TODO prevent crashes when stream_write() is called on a closed stream

// TODO: maybe stream should be closed on stream error (in response_content)

// TODO fix response functions return values

// All actions for current target. Sorted alphabetically at compile time by actions_sort.pl
static const struct
{
	struct string name;
	int (*handler)(const struct http_request *, struct http_response *restrict, struct resources *restrict, const union json *);
} actions[] = {
	ACTIONS
};

bool access_check_general(const struct string *action_name,const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query);

// can be used with: open, fstat, write, mmap
int http_errno_status(int error)
{
	switch (error)
	{
	case 0:
		return OK;
	case EACCES:
	case EROFS:
	case EEXIST: // TODO: is this right ?
		return Forbidden;
	case ELOOP: // TODO: is this right ?
	case ENAMETOOLONG:
	case ENOENT:
	case ENOTDIR:
		return NotFound;
	case ETIMEDOUT:
		return RequestTimeout;
	default:
		return InternalServerError;
	}
}

#if defined(DEVICE)
int handler_static(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources)
{
	if (DLNAisEnabled() && !strncmp(request->URI.data + 33, "/dlna/", 6)) return processDLNArequest(request, response, resources);
	return NotFound; // disable serving static files
}
#else
// TODO: fix path generation, etc.
int handler_static(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources)
{
	// TODO: make this work for file types different from regular file?

	size_t index;
	if (request->path.data[0] == '.') return Forbidden;
	for(index = 1; index < request->path.length; ++index)
		if ((request->path.data[index] == '.') && (request->path.data[index - 1] == '/'))
			return Forbidden;

	struct string web_root = string(WEB_ROOT), web_index = string(WEB_INDEX);
	struct string *path;
	if (request->path.data[request->path.length - 1] == '/') path = string_concat(&web_root, &request->path, &web_index);
	else path = string_concat(&web_root, &request->path);
	if (!path) return -1; // memory error

	int status = http_download(path, request, response, &resources->stream);
	free(path);
	return status;
}
#endif

int handler_dynamic(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources)
{
	// The only accepted query format is an object with keys representing action names
	// The value of each key represents action parameters

	struct string key;

	if (request->path.data[0] != '/') return ERROR_MISSING;

	// Decrypt the query if it is encrypted.
#if defined(DEVICE) || defined(PUBCLOUD)
	if (request->path.length > 1)
	{
		if (json_type(request->query) != STRING) return BadRequest;

		struct string *encoded = &request->query->string_node;

		// Initialize session.
		if (request->URI.length < (1 + CACHE_KEY_SIZE + 1)) return BadRequest;
		if (request->URI.data[1 + UUID_LENGTH + 1 + CACHE_KEY_SIZE] != '?') return ERROR_MISSING;
		memcpy(resources->session_id, request->URI.data + 1 + UUID_LENGTH + 1, CACHE_KEY_SIZE);
		resources->_session.ro = cache_use(resources->session_id);
		if (!resources->_session.ro) return ERROR_SESSION;
		resources->session.ro = resources->_session.ro->value;
		resources->session_access = SESSION_RO;

		size_t length = parse_base64_length(encoded->data, encoded->length);
		if (!length) return BadRequest;

		unsigned char *buffer = malloc(length);
		if (!buffer) return ERROR_MEMORY;

		bool success = parse_base64(encoded->data, buffer, encoded->length);
		if (!success)
		{
			free(buffer);
			return BadRequest;
		}

		struct string json_raw;
		json_raw.data = malloc(length);
		if (!json_raw.data)
		{
			free(buffer);
			return InternalServerError;
		}
		json_raw.length = length;

		// Get encryption key.
		key = string("encrypt_key");
		struct string *value = dict_get(resources->session.ro->object, &key);
		if (!value)
		{
			free(json_raw.data);
			free(buffer);
			return Forbidden;
		}

		struct aes_context context;
		aes_setup(value->data, value->length, &context);
		size_t index;
		for(index = 0; index < length; index += 16)
			aes_decrypt(buffer + index, json_raw.data + index, &context);
		free(buffer);

		// Ignore padding at the end of json_raw.
		json_raw.length = json_raw.length - 16 + strnlen(json_raw.data + index - 16, 16);

		// Parse query
		json_free(request->query);
		request->query = json_parse(&json_raw);
		free(json_raw.data);
		if (!request->query) return BadRequest; // TODO: this could be either BadRequest or InternalServerError because of json_parse
	}
	else
#endif
		resources->session_access = 0;

	if (json_type(request->query) != OBJECT) return BadRequest;

	struct dict *query = request->query->object;
	union json *json;

#if defined(DEBUG) || (!defined(DEVICE) && !defined(PUBCLOUD))
	if (!resources->session_access)
	{
		// Initialize session.
		key = string("session_id");
		json = dict_get(query, &key);
		if (json && (json_type(json) == STRING))
		{
			memcpy(resources->session_id, json->string_node.data, json->string_node.length);
			resources->_session.ro = cache_use(resources->session_id);
			if (!resources->_session.ro) return ERROR_SESSION;
			resources->session.ro = resources->_session.ro->value;
			resources->session_access = SESSION_RO;
		}
	}
#endif

#if defined(PUBCLOUD)
	if (resources->session_access)
	{
		setfsuid(65534);
		setfsgid(65534);
	}
#endif

	// Get auth_id and initialize auth data
	key = string("auth_id");
	json = dict_get(query, &key);
	if (json && (json_type(json) == STRING)) resources->auth_id = &json->string_node;
	else resources->auth_id = 0;
	resources->auth = 0;

	key = string("actions");
	json = dict_get(query, &key);
	if (!json || (json_type(json) != OBJECT)) return BadRequest;

	// Get one action and invoke its handler. Ignore the other actions.
	struct dict_iterator it;
	const struct dict_item *item = dict_first(&it, json->object);

	// Binary search for action named item->key_data
	size_t l = 0, r = (sizeof(actions) / sizeof(*actions));
	size_t index;
	int diff;
	int status;
	while (1)
	{
		index = (r - l) / 2 + l;
		diff = memcmp(item->key_data, actions[index].name.data, item->key_size + 1);
		if (!diff)
		{
			// Since clients should only request one action at a time, break after the first executed action
# ifdef DEVICE
			key = string((char *)item->key_data, item->key_size);
			if (access_check_general(&key, request, response, resources, item->value)) status = (*actions[index].handler)(request, response, resources, item->value);
			else
			{
				//TODO to be fixed
				struct string buffer = string("{\"error\":\"1037\"}");
				remote_json_send(request, response, resources, &buffer);
				status = 0;
			}
# else
			warning(logs("http: "), logs(actions[index].name.data, item->key_size));
			status = (*actions[index].handler)(request, response, resources, item->value);
# endif
			break;
		}
		else
		{
			if (diff > 0) l = index + 1;
			else r = index;

			if (l == r)
			{
				status = NotFound; // action not supported
				break;
			}
		}
	}

	switch (resources->session_access)
	{
	case SESSION_RO:
		cache_finish(resources->_session.ro);
		break;
	case SESSION_RW:
		cache_save(resources->session_id, resources->_session.rw);
		break;
	}

	return status;
}

#if !defined(OS_WINDOWS)
off_t content_length(const struct dict *restrict headers)
{
	struct string key = string("content-length");
	struct string *content_length = dict_get(headers, &key);
	if (!content_length) return ERROR_MISSING;

	char *end;
	off_t length = strtoll(content_length->data, &end, 10);
	if (end != (content_length->data + content_length->length)) return ERROR_INPUT;

	return length;
}
#else
int64_t content_length(const struct dict *restrict headers)
{
	struct string key = string("content-length");
	struct string *content_length = dict_get(headers, &key);
	if (!content_length) return ERROR_MISSING;

	char *end;
	int64_t length = strtoll(content_length->data, &end, 10);
	if (end != (content_length->data + content_length->length)) return ERROR_INPUT;

	return length;
}
#endif

int response_cache(const char key[restrict CACHE_KEY_SIZE], const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources)
{
	struct string *json_serialized;
	union json *node;

	struct string connection = string("Connection"), close = string("close");
	if (!response_header_add(response, &connection, &close)) return ERROR_MEMORY;

	node = json_string(key, CACHE_KEY_SIZE);
	if (!node) return ERROR_MEMORY;
	json_serialized = json_serialize(node);
	json_free(node);
	if (!json_serialized) return ERROR_MEMORY;

	bool success = remote_json_send(request, response, resources, json_serialized);
	free(json_serialized);

	if (success)
	{
		//connection_release(...); TODO use this
		stream_term(&resources->stream);
		return 0;
	}
	else return -1; // TODO better error here
}

// Generate string from the data read from the client so that it can be sent to a server.
static struct string *request_string(const struct http_request *request)
{
	const struct string *URI = &request->URI;

	size_t length;

	struct dict_iterator it;
	const struct dict_item *item;
	struct string *value;

	length = methods[request->method].length + sizeof(" ") - 1 + URI->length + sizeof(" ") - 1 + version.length + sizeof("\r\n") - 1;
	for(item = dict_first(&it, &request->headers); item; item = dict_next(&it, &request->headers))
	{
		value = item->value;
		length += item->key_size + sizeof(": ") - 1 + value->length + sizeof("\r\n") - 1;
	}
	length += sizeof("\r\n") - 1;

	struct string *result = malloc(sizeof(struct string) + sizeof(char) * (length + 1));
	if (!result) return 0;
	result->data = (char *)(result + 1);
	result->length = length;

	size_t index = 0;

	// Set method
	memcpy(result->data + index, methods[request->method].data, methods[request->method].length);
	index += methods[request->method].length;

	// Set URI
	result->data[index++] = ' ';
	memcpy(result->data + index, URI->data, URI->length);
	index += URI->length;

	// Set version
	result->data[index++] = ' ';
	memcpy(result->data + index, version.data, version.length);
	index += version.length;

	// Add request line terminator.
	result->data[index++] = '\r';
	result->data[index++] = '\n';

	// Add headers.
	for(item = dict_first(&it, &request->headers); item; item = dict_next(&it, &request->headers))
	{
		memcpy(result->data + index, item->key_data, item->key_size);
		index += item->key_size;

		result->data[index++] = ':';
		result->data[index++] = ' ';

		value = item->value;
		memcpy(result->data + index, value->data, value->length);
		index += value->length;

		result->data[index++] = '\r';
		result->data[index++] = '\n';
	}

	// Add headers terminator.
	result->data[index++] = '\r';
	result->data[index++] = '\n';

	result->data[index] = 0;
	return result;
}

/*int deflate(FILE *source, FILE *dest)
{
	int ret, flush;
	unsigned have;
	unsigned char in[CHUNK];
	unsigned char out[CHUNK];

	z_stream strm;
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	ret = deflateInit(&strm, Z_DEFAULT_COMPRESSION);
	if (ret != Z_OK) return ret;

	/* compress until end of file *
	do {
		strm.avail_in = fread(in, 1, CHUNK, source);
		strm.next_in = in;

		/* run deflate() on input until output buffer not full, finish
		   compression if all of source has been read in *
		do {
			strm.avail_out = CHUNK;
			strm.next_out = out;
			ret = deflate(&strm, flush);	/* no bad return value *
			assert(ret != Z_STREAM_ERROR);  /* state not clobbered *
			have = CHUNK - strm.avail_out;
			if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
			}
		} while (strm.avail_out == 0);
		assert(strm.avail_in == 0);	 /* all input will be used *

		/* done when last data in file processed *
	} while (flush != Z_FINISH);
	assert(ret == Z_STREAM_END);		/* stream will be complete *

	deflateEnd(&strm);
	return Z_OK;
}*/

bool response_header_add(struct http_response *restrict response, const struct string *key, const struct string *value)
{
	// Make sure there is enough space left in the buffer.
	if (((response->headers + HEADERS_LENGTH_MAX) - response->headers_end) < (key->length + 2 + value->length + 2)) return false;

	// name:value\r\n
	response->headers_end = format_bytes(response->headers_end, key->data, key->length);
	*response->headers_end++ = ':';
	*response->headers_end++ = ' '; // TODO ? remove this to save memory, traffic and CPU
	response->headers_end = format_bytes(response->headers_end, value->data, value->length);
	*response->headers_end++ = '\r';
	*response->headers_end++ = '\n';

	return true;
}

#if !defined(OS_WINDOWS)
bool response_headers_send(struct stream *restrict stream, const struct http_request *request, struct http_response *restrict response, off_t length)
#else
bool response_headers_send(struct stream *restrict stream, const struct http_request *request, struct http_response *restrict response, __int64 length)
#endif
{
	struct string key, value;
	int status;

	// TODO check what specific things should be done for each response code
	//unsigned type = response->code / 100;

	// Determine compression method. Add transfer headers.
	int content;
	if ((request->method == METHOD_HEAD) || (response->code < 200) || (response->code == NoContent) || (response->code == NotModified)) content = 0;
	else
	{
		content = RESPONSE_IDENTITY;

		// TODO: 415 UnsupportedMediaType should be returned in some cases here
		// TODO: support compression

		/*key = string("accept-encoding");
		struct string *header = dict_get(&request->headers, &key);
		if (header)
		{
			struct string *list;
			size_t allow, deny;
			int status = http_parse_accept(header, &list, &allow, &deny);
			// TODO: find the best encoding
			free(list);
		}
		else ; // TODO: identity

		key = string("Content-Encoding");
		value = string("deflate");
		response_header_add(response, &key, &value);*/

		response->index = 0;
		response->length = length;
	}

	// Initialize content headers.
	if (length == RESPONSE_CHUNKED)
	{
		key = string("Transfer-Encoding");
		value = string("chunked");
		if (!response_header_add(response, &key, &value)) goto error; // memory error
	}
	else
	{
		char buffer[SIZE_LENGTH_MAX];
		struct string *range;

		// Take care of ranges.
		key = string("range");
		if ((response->code == OK) && (range = dict_get(&request->headers, &key)))
		{
			if (status = http_parse_range(range->data, length, &response->ranges, &response->intervals)) return false; // TODO return status;

			// TODO: support multipart/byteranges and remove this
			if (response->intervals > 1) return false; // TODO return RequestedRangeNotSatisfiable; // TODO this return value is not right

			// Content-Range
			// bytes <low>-<high>/<total>
			char buffer[sizeof("bytes ") - 1 + SIZE_LENGTH_MAX + 1 + SIZE_LENGTH_MAX + 1 + SIZE_LENGTH_MAX] = "bytes ", *start = buffer + sizeof("bytes ") - 1;
			start = format_uint(start, response->ranges[0][0]);
			*start++ = '-';
			start = format_uint(start, response->ranges[0][1]);
			*start++ = '/';
			start = format_uint(start, length);
			key = string("Content-Range");
			value = string(buffer, start - buffer);
			if (!response_header_add(response, &key, &value)) goto error; // memory error

			key = string("Accept-Ranges");
			value = string("bytes");
			if (!response_header_add(response, &key, &value)) goto error; // memory error

			length = response->ranges[0][1] - response->ranges[0][0] + 1;
			response->code = PartialContent;
		}

		key = string("Content-Length");
#if !defined(OS_WINDOWS)
		value = string(buffer, format_uint(buffer, length) - buffer);
		if (!response_header_add(response, &key, &value)) goto error; // memory error
#else
		size_t size_length = __int64_length(length, 10);
		char *size_data = malloc(sizeof(char) * (size_length + 1));
		if (!size_data) goto error; // memory error
		_i64toa(length,size_data,10);
		size_data[size_length]=0;
		value = string(size_data, size_length);
		if (!response_header_add(response, &key, &value)) goto error; // memory error
		free(size_data);
#endif
	}

	// Date - Current date and time on the server in UTC/GMT.
	char date[HTTP_DATE_LENGTH + 1];
	http_date(date, time(0));
	key = string("Date");
	value = string(date, HTTP_DATE_LENGTH);
	if (!response_header_add(response, &key, &value)) goto error; // memory error

	// Make sure response code is valid. Find appropriate response phrase.
	struct string phrase;
	#define PHRASE_SET(status) case (status): \
		phrase = string(PHRASE_ ## status); \
		break
	#define PHRASE(status) PHRASE_SET(status)
	switch (response->code)
	{
		PHRASE(OK);
		PHRASE(NoContent);
		PHRASE(PartialContent);
		PHRASE(MovedPermanently);
		PHRASE(NotModified);
		PHRASE(BadRequest);
		PHRASE(Forbidden);
		PHRASE(NotFound);
		PHRASE(MethodNotAllowed);
		PHRASE(RequestTimeout);
		PHRASE(LengthRequired);
		PHRASE(RequestEntityTooLarge);
		PHRASE(RequestURITooLong);
		PHRASE(UnsupportedMediaType);
		PHRASE(RequestedRangeNotSatisfiable);
		PHRASE(InternalServerError);
		PHRASE(NotImplemented);
		PHRASE(ServiceUnavailable);
	default: // TODO is this necessary? should it really just return false?
		goto error; // invalid response code
	}
	#undef PHRASE
	#undef PHRASE_SET

#if RUN_MODE <= Debug
	if (request->URI.data)
	{
		// TODO fix this
		//struct string *s = request_string(request);
		//if (s) printf("REQUEST:\n%s\n", s->data);
		//free(s);
		//printf("[%s] %s %s\n%d %s\n", date, methods[request->method].data, request->URI.data, response->code, phrase.data);
	}
#endif

	char line[64]; // TODO make sure this is enough
	char *end = line;
	struct string fragment;

	// HTTP/1.1 code phrase\r\n
	end = format_bytes(end, version.data, version.length);
	*end++ = ' ';
	end = format_uint(end, response->code, 10, HTTP_CODE_LENGTH);
	*end++ = ' ';
	end = format_bytes(end, phrase.data, phrase.length);
	end = format_bytes(end, terminator.data, terminator.length);

	fragment = string(line, end - line);
	//write(2, fragment.data, fragment.length);
	if (status = stream_write(stream, &fragment))
	{
		error(logs("response_header 0: status="), logi(status));
		goto error;
	}

	fragment = string(response->headers, response->headers_end - response->headers);
	//write(2, fragment.data, fragment.length);
	if (status = stream_write(stream, &fragment))
	{
		error(logs("response_header 1: status="), logi(status));
		goto error;
	}

	if (status = stream_write(stream, &terminator))
	{
		error(logs("response_header 2: status="), logi(status));
		goto error;
	}

	response->content_encoding = content;
	return !stream_write_flush(stream); // TODO is this okay?

error:
	// TODO send internal server error response?
	stream_term(stream);
	return false; // memory error // TODO or invalid response code
}

#if !defined(OS_WINDOWS)
int response_entity_send(struct stream *restrict stream, struct http_response *restrict response, const char *restrict data, off_t length)
#else
int response_entity_send(struct stream *restrict stream, struct http_response *restrict response, const char *restrict data, int64_t length)
#endif
{
	// Do nothing if no entity body is required.
	if (!response->content_encoding) return 0;

	struct string content = string((char *)data, length); // TODO fix this cast
	int status;

	if (response->length == RESPONSE_CHUNKED)
	{
		char buffer[SIZE_LENGTH_MAX + 2], *start;
		start = format_uint(buffer, (uintmax_t)length, 16);
		*start++ = terminator.data[0];
		*start++ = terminator.data[1];
		struct string size = string(buffer, start - buffer);

		status = stream_write(stream, &size);
		if (!status) status = stream_write(stream, &content);
		if (!status) status = stream_write(stream, &terminator);
	}
	else
	{
		// TODO support response->intervals > 1

		if (response->ranges)
		{
			// Find which part of the data to send.
			#if !defined(OS_WINDOWS)
			off_t start = response->ranges[0][0] - response->index;
			#else
			int64_t start = response->ranges[0][0] - response->index;
			#endif
			response->index += length;
			if (start >= length) return 0; // no entity body to send
			if (start > 0)
			{
				data += start;
				length -= start;
			}
			#if !defined(OS_WINDOWS)
			off_t size = response->ranges[0][1] + 1 - response->ranges[0][0];
			#else
			int64_t size = response->ranges[0][1] + 1 - response->ranges[0][0];
			#endif
			if (start < 0)
			{
				size += start;
				if (size <= 0) return 0; // no entity body to send
			}
			if (size > length) size = length;
			content = string((char *)data, size); // TODO fix this cast
		}

		status = stream_write(stream, &content);
	}
if (status) debug(logs("entity status="), logi(status));
	return (status ? status : stream_write_flush(stream));
}
