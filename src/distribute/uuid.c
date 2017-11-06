#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include "types.h"
#include "aes.h"
#include "arch.h"
#include "protocol.h"
#include "uuid.h"

// TODO: is this okay? no, it's not
#define KEY_SIZE 16
static const unsigned char aes_key[KEY_SIZE] = "KU#rYI&\xb3H9Uw\xefMio";

struct string SECRET = {.data = (char *)aes_key, .length = KEY_SIZE};

// UUID is stored as AES-128 encrypted value of a 16-byte string:
// xxxxxxxxyyyyzzrr
// xxxxxxxx		lowest 64 bits of current UNIX time (in microseconds)
// yyyy			32b client_id
// zz			16b platform_id
// rr			16b pseudo-random number

// Returns pointer to a newly allocated UUID.
// WARNING: It is assumed that the generated UUID is unique.
struct string *restrict uuid_alloc(uint32_t client_id, uint16_t platform_id)
{
	// Limit client_id.
	if (client_id >= CLIENTS_LIMIT) return 0;

	// Get current time.
	struct timeval now;
	gettimeofday(&now, 0);

	// TODO: generate random number securely

	// Generate data.
	char container[UUID_SIZE];
	uint64_t time = (uint64_t)now.tv_sec * 1000000 + now.tv_usec;
	uint16_t chance = random() & 0xffff;
	endian_big64(container, &time);
	endian_big32(container + 8, &client_id);
	endian_big16(container + 12, &platform_id);
	endian_big16(container + 14, &chance);

	// Encrypt data.
	unsigned char encrypted[UUID_SIZE];
	struct aes_context context;
	aes_setup(aes_key, sizeof(aes_key), &context);
	aes_encrypt(container, encrypted, &context);

	return string_alloc(encrypted, UUID_SIZE);
}

void uuid_extract(const char uuid[restrict UUID_SIZE], uint32_t *restrict client_id, uint16_t *restrict platform_id)
{
	char data[UUID_SIZE];
	struct aes_context context;

	aes_setup(aes_key, sizeof(aes_key), &context);
	aes_decrypt(uuid, data, &context);

	if (client_id) *client_id = be32toh(*(uint32_t *)(data + 8));
	if (platform_id) *platform_id = be16toh(*(uint16_t *)(data + 12));
}

#if !defined(DISTRIBUTE)
# include "format.h"

int main(void)
{
	char buffer[UUID_SIZE * 2 + 1];
	struct string *uuid = uuid_alloc(0, 0);
	if (!uuid) return 1;
	*format_hex(buffer, uuid->data, uuid->length) = '\n';
	free(uuid);
	write(1, buffer, sizeof(buffer));
	return 0;
}
#endif
