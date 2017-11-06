#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef OS_WINDOWS
# define WINVER 0x0501
# include <windows.h>
# include <winsock2.h>
# include <ws2tcpip.h>
# include <sys/stat.h>
# include "mingw.h"
# define srandom srand
# define random rand
#endif

#include "types.h"
#include "sha2.h"
#include "security.h"
#include "io.h"

#ifdef OS_ANDROID
// All mobile platforms should choose this case (to save power) but changing this now will break registered devices.
# define HASH_ROUNDS 1
#else
# define HASH_ROUNDS 4096
#endif

#define TIME_RETRY 1

// http://stackoverflow.com/questions/19120515/password-checking-security-and-link-time-optimizations

extern struct string SECRET;

static pthread_mutex_t security = PTHREAD_MUTEX_INITIALIZER;

static void hash(uint8_t out[static restrict HASH_SIZE], const struct string *restrict in, const uint8_t salt[static restrict SALT_SIZE])
{
	SHA2_CTX context;

	SHA256Init(&context);
	SHA256Update(&context, in->data, in->length);
	SHA256Update(&context, salt, SALT_SIZE);
	SHA256Final(out, &context);

	// Stretching.
	unsigned round;
	for(round = 0; round < HASH_ROUNDS; ++round)
	{
		SHA256Init(&context);
		SHA256Update(&context, out, HASH_SIZE);
		SHA256Update(&context, salt, SALT_SIZE);
		SHA256Final(out, &context);
	}

	// The result of the last round is in out.
}

void security_init(void)
{
	srandom(time(0));
}

void security_term(void)
{
	pthread_mutex_destroy(&security);
}

void security_random(uint8_t *restrict result, size_t size)
{
	// Get random number from /dev/urandom (since /dev/random may block).
	int pool = open("/dev/urandom", O_RDONLY);
	if (pool >= 0)
	{
		readall(pool, result, size);
		close(pool);
	}
	else
	{
		// Emulate random number generation with random().
		size_t index;
		for(index = 0; index < size; ++index)
			result[index] = (uint8_t)random();
	}
}

void security_password(const struct string *restrict input, uint8_t password[static restrict SALT_SIZE + HASH_SIZE])
{
	security_random(password, SALT_SIZE);
	hash(password + SALT_SIZE, input, password);
}

bool security_authorize(const struct string *restrict input, const uint8_t password[static restrict SALT_SIZE + HASH_SIZE])
{
	// Compare each byte to prevent timing attacks. Make sure the compiler doesn't optimize the comparison.

	// Prevent bruteforce attacks:
	// Make sure no two simultaneous checks can be done by using a mutex.
	// On password mismatch, wait before return.

	if (pthread_mutex_trylock(&security)) return false; // another login in progress

	volatile unsigned diff = 0;
	size_t index;

	// Generate hash of the input using the salt stored in password.
	uint8_t output_buffer[HASH_SIZE];
	volatile uint8_t *output = output_buffer;
	hash(output_buffer, input, password);

	for(index = 0; index < HASH_SIZE; ++index)
		diff |= (output[index] ^ password[SALT_SIZE + index]);
	if (diff) sleep(TIME_RETRY);

	pthread_mutex_unlock(&security);

	// Don't leave trace of the values of diff and output in the stack.
	for(index = 0; index < HASH_SIZE; ++index) output[index] = 0;
	diff = (diff == 0);

	return diff;
}
