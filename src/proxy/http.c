#define _POSIX_C_SOURCE 200809L 
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "types.h"
#include "stream.h"
#include "http.h"

// Returns the specified time in the format specified by RFC 1123:
// Sun, 06 Nov 1994 08:49:37 GMT
void http_date(char buffer[HTTP_DATE_LENGTH + 1], time_t timestamp)
{
	static const char days[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
	static const char months[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

	struct tm info;
	gmtime_r(&timestamp, &info);
	
	// Generate date string. Assume sprintf will not fail
	sprintf(buffer, "%3s, %02u %3s %04u %02u:%02u:%02u GMT",
		days[info.tm_wday],
		info.tm_mday,
		months[info.tm_mon],
		info.tm_year + 1900,
		info.tm_hour,
		info.tm_min,
		info.tm_sec
	);
}

char *restrict http_uuid(struct stream *restrict stream)
{
	struct string buffer;
	size_t index = 0;

	// Read enough bytes from the request to be sure that we have the UUID.
	// A valid request is always long enough for this operation to be successful.

	#define METHOD_LENGTH_MAX 7

	// Start reading the request
	if (stream_read(stream, &buffer, METHOD_LENGTH_MAX + 1 + 1 + UUID_LENGTH)) return 0;

	// Check whether the request looks authentic. Find the position of the UUID.
	while (buffer.data[index++] != ' ') // method name terminator
		if (index > METHOD_LENGTH_MAX)
			return 0;
	if (buffer.data[index] != '/') return 0;

	#undef METHOD_LENGTH_MAX

	char *uuid = malloc(sizeof(char) * (UUID_LENGTH + 1));
	if (!uuid) return 0;
	memcpy(uuid, buffer.data + index + 1, UUID_LENGTH);
	uuid[UUID_LENGTH] = 0;
	return uuid;
}
