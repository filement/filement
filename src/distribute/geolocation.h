#include "ipdb/ipdb.h"

struct host
{
	int32_t coords[2];	// latitude, longitude
	const struct string *name;
	uint16_t port; // TODO: this should be unsigned short
};

bool closest(uint32_t ip, struct host *restrict list, size_t length, size_t count);
