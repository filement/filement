#include "arch.h"

#define CACHE_KEY_SIZE ((sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint64_t)) * 4 / 3) /* 16 */

#if defined(ENDIAN_BIG)
# define CACHE_FILE			0
# define CACHE_LIST			1
# define CACHE_PROGRESS		2
# define CACHE_SESSION		3
# define CACHE_URL			4
# define CACHE_PROXY		5
#else /* defined(ENDIAN_LITTLE) */
# define CACHE_FILE			(0 << 8)
# define CACHE_LIST			(1 << 8)
# define CACHE_PROGRESS		(2 << 8)
# define CACHE_SESSION		(3 << 8)
# define CACHE_URL			(4 << 8)
# define CACHE_PROXY		(5 << 8)
#endif

struct cache
{
	union json *value;
	volatile unsigned _links;
};

struct resources;

bool cache_init(void);
void cache_term(void);

bool cache_create(char key[restrict CACHE_KEY_SIZE], uint16_t class, union json *restrict value, time_t expire);
uint16_t cache_class(const char key[restrict CACHE_KEY_SIZE]);
union json *cache_keys(uint16_t class);
void cache_destroy(const char key[restrict CACHE_KEY_SIZE]);

const struct cache *cache_use(const char key[restrict CACHE_KEY_SIZE]);
void cache_finish(const struct cache *restrict data);

struct cache *restrict cache_load(const char key[restrict CACHE_KEY_SIZE]);
void cache_save(const char key[restrict CACHE_KEY_SIZE], struct cache *restrict data);
void cache_discard(const char key[restrict CACHE_KEY_SIZE], struct cache *data);

int cache_enable(uint16_t class, const struct string *restrict location, const char key[restrict CACHE_KEY_SIZE]);
const char *cache_key(uint16_t class, const struct string *restrict location);
char *cache_disable(uint16_t class, const struct string *restrict location);

// TODO: these should not be here
char *restrict session_id_alloc(void);
bool session_create(struct resources *restrict resources);
bool session_edit(struct resources *restrict resources);
void session_save(struct resources *restrict resources);
void session_discard(struct resources *restrict resources);
void session_destroy(struct resources *restrict resources);
