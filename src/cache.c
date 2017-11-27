/*
each cache stores data about itself:
list - location, depth, filters
	{
		"location": STRING,
		"depth": INTEGER,
		"filters": {STRING: JSON, ...}
	}
	location is FURI; filters are stored the same way as in the ffs.search request

TODO can I replace entry->_links with pthread_mutex_trylock() in cache_destroy?
*/

// TODO move session functions to a separate file and remove #include <server.h>

// TODO fix the binary data in session

// TODO understand this better
//  http://en.wikipedia.org/wiki/Memory_barrier

#include <pthread.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#if !defined(OS_WINDOWS)
# include <arpa/inet.h>
# include <dirent.h>
#else
# include <sys/stat.h>
# include <windows.h>
# define random rand
# define unlink remove
#endif

#include "types.h"
#include "format.h"
#include "json.h"
#include "stream.h"
#include "cache.h"
#include "server.h"

#if defined(READDIR)
# include <errno.h>
#endif

#define SESSION_EXPIRE 10800 /* 3 hours */

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER, mutex_enabled = PTHREAD_MUTEX_INITIALIZER;

static struct dict cache;
static struct dict cache_files, cache_lists;

struct cache_entry
{
	pthread_mutex_t mutex;
	unsigned _links;
	struct cache *cache;
	struct life
	{
		char key[CACHE_KEY_SIZE];
		time_t duration, expire;
		size_t position;
	} life;
};

typedef struct life *type;

struct heap
{
	type *data; // Array with the elements.
	size_t _size; // Number of elements that the heap can hold withour resizing.
	size_t count; // Number of elements actually in the heap.
};

// Heap containing the caches with limited life.
static struct heap lifes;

// Returns the biggest element in the heap
#define heap_front(h) (*(h)->data)

// Frees the allocated memory
#define heap_term(h) (free((h)->data))

// true		a is in front of b
// false	b is in front of a
#define CMP(a, b) ((a)->expire <= (b)->expire)

#define BASE_SIZE 4

static bool heap_init(struct heap *restrict h)
{
	h->data = malloc(sizeof(type) * BASE_SIZE);
	if (!h->data) return false; // memory error
	h->_size = BASE_SIZE;
	h->count = 0;
	return true;
}

// Inserts element to the heap.
static bool heap_push(struct heap *restrict h, type value)
{
	size_t index, parent;

	// Resize the heap if it is too small to hold all the data.
	if (h->count == h->_size)
	{
		type *buffer;
		h->_size <<= 1;
		buffer = realloc(h->data, sizeof(type) * h->_size);
		if (h->data) h->data = buffer;
		else return false;
	}

	// Find out where to put the element and put it.
	for(index = h->count++; index; index = parent)
	{
		parent = (index - 1) >> 1;
		if CMP(h->data[parent], value) break;
		h->data[index] = h->data[parent];
		h->data[index]->position = index;
	}
	h->data[index] = value;
	h->data[index]->position = index;

	return true;
}

// Removes the biggest element from the heap.
static void heap_pop(struct heap *restrict h)
{
	size_t index, swap, other;

	// Remove the biggest element.
	type temp = h->data[--h->count];

	// Resize the heap if it's consuming too much memory.
	if ((h->count <= (h->_size >> 2)) && (h->_size > BASE_SIZE))
	{
		h->_size >>= 1;
		h->data = realloc(h->data, sizeof(type) * h->_size);
	}

	// Reorder the elements.
	for(index = 0; true; index = swap)
	{
		// Find which child to swap with.
		swap = (index << 1) + 1;
		if (swap >= h->count) break; // If there are no children, the heap is reordered.
		other = swap + 1;
		if ((other < h->count) && CMP(h->data[other], h->data[swap])) swap = other;
		if CMP(temp, h->data[swap]) break; // If the bigger child is less than or equal to its parent, the heap is reordered.

		h->data[index] = h->data[swap];
		h->data[index]->position = index;
	}
	h->data[index] = temp;
	h->data[index]->position = index;
}

static void heap_remove(struct heap *restrict h, size_t index)
{
	size_t swap, other;

	// Remove the biggest element.
	type temp = h->data[--h->count];

	// Resize the heap if it's consuming too much memory.
	if ((h->count <= (h->_size >> 2)) && (h->_size > BASE_SIZE))
	{
		h->_size >>= 1;
		h->data = realloc(h->data, sizeof(type) * h->_size);
	}

	// Reorder the elements.
	while (true)
	{
		// Find which child to swap with.
		swap = (index << 1) + 1;
		if (swap >= h->count) break; // If there are no children, the heap is reordered.
		other = swap + 1;
		if ((other < h->count) && CMP(h->data[other], h->data[swap])) swap = other;
		if CMP(temp, h->data[swap]) break; // If the bigger child is less than or equal to its parent, the heap is reordered.

		h->data[index] = h->data[swap];
		h->data[index]->position = index;
		index = swap;
	}
	h->data[index] = temp;
	h->data[index]->position = index;
}

static void heap_descend(struct heap *restrict h, size_t index)
{
	size_t swap, other;

	type temp = h->data[index];

	// Reorder the elements.
	while (true)
	{
		// Find which child to swap with.
		swap = (index << 1) + 1;
		if (swap >= h->count) break; // If there are no children, the heap is reordered.
		other = swap + 1;
		if ((other < h->count) && CMP(h->data[other], h->data[swap])) swap = other;
		if CMP(temp, h->data[swap]) break; // If the bigger child is less than or equal to its parent, the heap is reordered.

		h->data[index] = h->data[swap];
		h->data[index]->position = index;
		index = swap;
	}
	h->data[index] = temp;
	h->data[index]->position = index;
}

// TODO implement creating temporary files here (not in evfs.c)

#define DIRECTORY "/tmp/"
#define CACHE_PREFIX "filement_"
#define KEY_POSITION (sizeof(DIRECTORY) - 1 + sizeof(CACHE_PREFIX) - 1)

bool cache_init(void)
{
#if defined(OS_MAC) || defined(OS_LINUX) || defined(OS_FREEBSD)
	// Delete old filement temporary files.
	// TODO fix cache deletion on some systems (e.g. android)

	struct string filename;
	char path[KEY_POSITION + CACHE_KEY_SIZE + 1] = DIRECTORY CACHE_PREFIX;

	struct dirent *entry;
# if !defined(READDIR)
	struct dirent *more;
	entry = malloc(offsetof(struct dirent, d_name) + pathconf(DIRECTORY, _PC_NAME_MAX) + 1);
	if (!entry)
		return false;
# endif

	DIR *dir = opendir(DIRECTORY);
	if (!dir) return false;

	while (true)
	{
# if defined(READDIR)
		errno = 0;
		if (!(entry = readdir(dir)))
		{
			if (errno)
			{
				closedir(dir);
				return false;
			}
			break; // no more entries
		}
# else
		if (readdir_r(dir, entry, &more))
		{
			closedir(dir);
			free(entry);
			return false;
		}
		if (!more) break; // no more entries
# endif

# if defined(_DIRENT_HAVE_D_NAMLEN)
		filename = string(entry->d_name, entry->d_namlen);
# else
		filename = string(entry->d_name, strlen(entry->d_name));
# endif

		if ((filename.length == (sizeof(CACHE_PREFIX) - 1 + CACHE_KEY_SIZE)) && !memcmp(filename.data, CACHE_PREFIX, sizeof(CACHE_PREFIX) - 1))
		{
			*format_bytes(path + KEY_POSITION, filename.data + sizeof(CACHE_PREFIX) - 1, CACHE_KEY_SIZE) = 0;
			unlink(path); // ignore errors
		}
	}
	closedir(dir);
# if !defined(READDIR)
	free(entry);
# endif
#endif

	if (!dict_init(&cache, DICT_SIZE_BASE)) return false;

	if (!dict_init(&cache_files, DICT_SIZE_BASE))
	{
		dict_term(&cache);
		return false;
	}

	if (!dict_init(&cache_lists, DICT_SIZE_BASE))
	{
		dict_term(&cache_files);
		dict_term(&cache);
		return false;
	}

	if (!heap_init(&lifes))
	{
		dict_term(&cache_lists);
		dict_term(&cache_files);
		dict_term(&cache);
		return false;
	}

	return true;
}

void cache_term(void)
{
	// TODO delete cache files
	// TODO make sure no race conditions occur

	pthread_mutex_destroy(&mutex_enabled);
	pthread_mutex_destroy(&mutex);

	heap_term(&lifes);
	dict_term(&cache_lists);
	dict_term(&cache_files);
	dict_term(&cache);
}

// Creates a new cache entry of class class with value value. Stores its key in key.
// WARNING: There is a theoritical possibility that the cache key is taken.
// Key format: xxyyzzzzzzzz
// xx					16 bit cache class (base64)
// yy					16 bit pseudo-random number (base64)
// zzzzzzzz				64 bit current UNIX time in microseconds (base64)
bool cache_create(char key[restrict CACHE_KEY_SIZE], uint16_t class, union json *restrict value, time_t duration)
{
	uint16_t chance = htobe16(random() & 0xffff);

	// Get current time in microseconds.
	struct timeval current;
	gettimeofday(&current, 0);
	uint64_t now = htobe64((uint64_t)current.tv_sec * 1000000 + current.tv_usec);

	// Generate base64 representation of the key.
	char key_raw[sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint64_t)];
#if !defined(OS_IOS)
	format(key_raw, str((char *)&class, sizeof(uint16_t)), str((char *)&chance, sizeof(uint16_t)), str((char *)&now, sizeof(uint64_t)));
#else
	format_ios(key_raw, str((char *)&class, sizeof(uint16_t)), str((char *)&chance, sizeof(uint16_t)), str((char *)&now, sizeof(uint64_t)));
#endif
	format_base64(key, key_raw, sizeof(key_raw));

	// Initialize cache entry.
	struct cache_entry *entry = malloc(sizeof(struct cache_entry));
	if (!entry) return false;
	entry->cache = malloc(sizeof(struct cache));
	if (!entry->cache)
	{
		free(entry);
		return false;
	}
	if (pthread_mutex_init(&entry->mutex, 0))
	{
		free(entry->cache);
		free(entry);
		return false;
	}
	entry->_links = 0;
	entry->cache->value = value;
	entry->cache->_links = 1;

	// Set cache life parameters.
	if (entry->life.duration = duration)
	{
		format_bytes(entry->life.key, key, CACHE_KEY_SIZE);
		entry->life.expire = current.tv_sec + duration;
	}

	struct string key_string = string(key, CACHE_KEY_SIZE);

	pthread_mutex_lock(&mutex);

	// Remove expired caches.
	struct life *life;
	while (lifes.count)
	{
		life = heap_front(&lifes);
		if (life->expire > current.tv_sec) break; // the rest of the caches are still valid

		// Check whether cache with such key exists.
		struct string key_old = string(life->key, CACHE_KEY_SIZE);
		struct cache_entry *entry = dict_get(&cache, &key_old);
		if (entry->_links) // increase cache life if something is using it
		{
			life->expire = current.tv_sec + life->duration;
			heap_descend(&lifes, 0);
			continue;
		}

		// Remove cache entry from application data structures.
		dict_remove(&cache, &key_old);
		heap_pop(&lifes);

		// A thread could have just started using the cache. Ensure that the mutex is not used before destroying it.
		// When the lock succeeds, no other thread is using the mutex. Since the entry is already removed from cache, no other thread can access the mutex.
		if (pthread_mutex_trylock(&entry->mutex))
		{
			pthread_mutex_unlock(&mutex);
			break;
		}
		pthread_mutex_unlock(&entry->mutex);
		pthread_mutex_destroy(&entry->mutex);

		// Free allocated resources.
		entry->cache->_links -= 1;
		if (!entry->cache->_links)
		{
			json_free(entry->cache->value);
			free(entry->cache);
		}
		free(entry);

		// TODO cache_disable?
		// TODO delete file?
	}

	// Add cache entry.
	bool success = !dict_add(&cache, &key_string, entry);
	// TODO check success here
	if (duration)
		if (!heap_push(&lifes, &entry->life))
			; // TODO

	pthread_mutex_unlock(&mutex);

	if (!success)
	{
		pthread_mutex_destroy(&entry->mutex);
		free(entry);
	}

	return success;
}

uint16_t cache_class(const char key[restrict CACHE_KEY_SIZE])
{
	uint16_t class[2];
	parse_base64((const unsigned char *)key, (unsigned char *)class, sizeof(uint16_t) * 2);
	return *class;
}

union json *cache_keys(uint16_t class)
{
	union json *array = json_array();
	if (!array) return 0;

	pthread_mutex_lock(&mutex);

	struct dict_iterator it;
	const struct dict_item *item;
	struct string key;
	for(item = dict_first(&it, &cache); item; item = dict_next(&it, &cache))
	{
		if (cache_class(item->key_data) != class) continue;
		key = string((char *)item->key_data, item->key_size);
		if (json_array_insert_old(array, json_string_old(&key)))
		{
			pthread_mutex_unlock(&mutex);
			json_free(array);
			return 0;
		}
	}

	pthread_mutex_unlock(&mutex);

	return array;
}

const struct cache *cache_use(const char key[restrict CACHE_KEY_SIZE])
{
	struct string key_string = string((char *)key, CACHE_KEY_SIZE);

	pthread_mutex_lock(&mutex);

	struct cache_entry *entry = dict_get(&cache, &key_string);
	if (!entry)
	{
		pthread_mutex_unlock(&mutex);
		return 0;
	}

	// Check cache expiration time. Update it if the cache is still valid.
	time_t now = time(0);
	if (entry->life.duration)
	{
		if (entry->life.expire <= now)
		{
			pthread_mutex_unlock(&mutex);
			return 0;
		}
		else entry->life.expire = now + entry->life.duration;
	}

	struct cache *data = entry->cache;
	data->_links += 1; // data will remain valid as long as _links is positive

	pthread_mutex_unlock(&mutex);

	return data;
}

void cache_finish(const struct cache *restrict data)
{
	pthread_mutex_lock(&mutex);
	((struct cache *)data)->_links -= 1;
	bool keep = data->_links;
	pthread_mutex_unlock(&mutex);

	if (!keep)
	{
		json_free(data->value);
		free((void *)data);
	}
}

struct cache *restrict cache_load(const char key[restrict CACHE_KEY_SIZE])
{
	struct string key_string = string((char *)key, CACHE_KEY_SIZE);

	volatile struct cache_entry *entry;

	pthread_mutex_lock(&mutex);

	entry = dict_get(&cache, &key_string);
	if (!entry)
	{
		pthread_mutex_unlock(&mutex);
		return 0;
	}

	// Check cache expiration time. Update it if the cache is still valid.
	time_t now = time(0);
	if (entry->life.duration)
	{
		if (entry->life.expire <= now)
		{
			pthread_mutex_unlock(&mutex);
			return 0;
		}
		else entry->life.expire = now + entry->life.duration;
	}

	entry->_links += 1; // entry will remain valid as long as _links is positive

	pthread_mutex_unlock(&mutex);

	// Use a mutex to ensure that only one thread can modify the entry at any given moment.
	pthread_mutex_lock((pthread_mutex_t *)&entry->mutex);

	// Duplicate cache entry.
	struct cache *copy = malloc(sizeof(struct cache));
	if (!copy)
	{
		pthread_mutex_unlock((pthread_mutex_t *)&entry->mutex);
		return 0;
	}
	copy->value = json_clone(entry->cache->value);
	if (!copy->value)
	{
		pthread_mutex_unlock((pthread_mutex_t *)&entry->mutex);
		free(copy);
		return 0;
	}
	copy->_links = 1;

	return copy;
}

void cache_save(const char key[restrict CACHE_KEY_SIZE], struct cache *data)
{
	struct string key_string = string((char *)key, CACHE_KEY_SIZE);

	struct cache_entry *entry;
	struct cache *data_old;

	pthread_mutex_lock(&mutex);
	entry = dict_get(&cache, &key_string);
	data_old = entry->cache;
	entry->cache = data;
	bool keep = (data_old->_links -= 1);
	entry->_links -= 1;
	pthread_mutex_unlock(&mutex);
	pthread_mutex_unlock(&entry->mutex);

	if (!keep)
	{
		json_free(data_old->value);
		free(data_old);
	}
}

void cache_discard(const char key[restrict CACHE_KEY_SIZE], struct cache *data)
{
	struct string key_string = string((char *)key, CACHE_KEY_SIZE);

	struct cache_entry *entry;

	pthread_mutex_lock(&mutex);
	entry = dict_get(&cache, &key_string);
	entry->_links -= 1;
	pthread_mutex_unlock(&mutex);
	pthread_mutex_unlock((pthread_mutex_t *)&entry->mutex);

	json_free(data->value);
}

void cache_destroy(const char key[restrict CACHE_KEY_SIZE])
{
	struct string key_string = string((char *)key, CACHE_KEY_SIZE);

	pthread_mutex_lock(&mutex);

	// Check whether cache with such key exists.
	// Don't destroy cache if it is loaded somewhere.
	struct cache_entry *entry = dict_get(&cache, &key_string);
	if (!entry || entry->_links)
	{
		pthread_mutex_unlock(&mutex);
		return;
	}

	// Remove cache entry from application data structures.
	dict_remove(&cache, &key_string);
	if (entry->life.duration) heap_remove(&lifes, entry->life.position);

	bool keep = (entry->cache->_links -= 1);

	pthread_mutex_unlock(&mutex);

	// A thread could have just started using the cache. Ensure that the mutex is not locked before destroying it.
	// When the lock succeeds, no other thread is using the mutex. Since the entry is already removed from cache, no other thread can access the mutex.
	pthread_mutex_lock(&entry->mutex);
	pthread_mutex_unlock(&entry->mutex);
	pthread_mutex_destroy(&entry->mutex);

	// Free allocated resources.
	if (!keep)
	{
		json_free(entry->cache->value);
		free(entry->cache);
	}
	free(entry);
}

int cache_enable(uint16_t class, const struct string *restrict location, const char key[restrict CACHE_KEY_SIZE])
{
	int status;

	char *value = memdup(key, CACHE_KEY_SIZE);
	if (!value) return ERROR_MEMORY;

	pthread_mutex_lock(&mutex_enabled);

	switch (class)
	{
	case CACHE_FILE:
		status = dict_add(&cache_files, location, value);
		break;
	case CACHE_LIST:
		status = dict_add(&cache_lists, location, value);
		break;
	}

	pthread_mutex_unlock(&mutex_enabled);

	if (status) free(value);
	return status;
}

const char *cache_key(uint16_t class, const struct string *restrict location)
{
	const char *key = 0;

	pthread_mutex_lock(&mutex_enabled);

	switch (class)
	{
	case CACHE_FILE:
		key = dict_get(&cache_files, location);
		break;
	case CACHE_LIST:
		key = dict_get(&cache_lists, location);
		break;
	}

	pthread_mutex_unlock(&mutex_enabled);

	return key;
}

char *cache_disable(uint16_t class, const struct string *restrict location)
{
	char *key = 0;

	pthread_mutex_lock(&mutex_enabled);

	switch (class)
	{
	case CACHE_FILE:
		key = dict_remove(&cache_files, location);
		// TODO delete cache file
		break;
	case CACHE_LIST:
		key = dict_remove(&cache_lists, location);
		break;
	}

	pthread_mutex_unlock(&mutex_enabled);

	return key;
}

/////////////////////////////////////////////////////

char *restrict session_id_alloc(void)
{
	char buffer[sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint64_t)]; //, session[16 + 1];

	char *session = malloc(16 + 1);
	if (!session) return 0;

	// TODO this is not secure
	uint16_t chance = htobe16(random() & 0xffff);

	// Get current time in microseconds.
	struct timeval current;
	gettimeofday(&current, 0);
	uint64_t now = htobe64((uint64_t)current.tv_sec * 1000000 + current.tv_usec);

	uint16_t class = 42; // TODO change this

#if !defined(OS_IOS)
	format(buffer, str((char *)&class, sizeof(uint16_t)), str((char *)&chance, sizeof(uint16_t)), str((char *)&now, sizeof(uint64_t)));
#else
	format_ios(buffer, str((char *)&class, sizeof(uint16_t)), str((char *)&chance, sizeof(uint16_t)), str((char *)&now, sizeof(uint64_t)));
#endif
	*format_base64(session, buffer, sizeof(buffer)) = 0;
	return session;
}

bool session_create(struct resources *restrict resources)
{
	union json *value = json_object();
	if (!value) return false;

	if (!cache_create(resources->session_id, CACHE_SESSION, value, SESSION_EXPIRE))
	{
		json_free(value);
		return false;
	}
	resources->session_id[CACHE_KEY_SIZE] = 0;

	resources->_session.ro = cache_use(resources->session_id);
	// assert(resources->_session.ro);
	resources->session.ro = resources->_session.ro->value;
	resources->session_access = SESSION_RO;

	return true;
}

bool session_edit(struct resources *restrict resources)
{
	cache_finish(resources->_session.ro);

	resources->_session.rw = cache_load(resources->session_id);
	if (!resources->_session.rw) return false;
	resources->session.rw = resources->_session.rw->value;
	resources->session_access = SESSION_RW;

	return true;
}

void session_save(struct resources *restrict resources)
{
	cache_save(resources->session_id, resources->_session.rw);

	resources->_session.ro = cache_use(resources->session_id);
	// assert(resources->_session.ro);
	resources->session.ro = resources->_session.ro->value;
	resources->session_access = SESSION_RO;
}

void session_discard(struct resources *restrict resources)
{
	cache_discard(resources->session_id, resources->_session.rw);

	resources->_session.ro = cache_use(resources->session_id);
	// assert(resources->_session.ro);
	resources->session.ro = resources->_session.ro->value;
	resources->session_access = SESSION_RO;
}

void session_destroy(struct resources *restrict resources)
{
	if (resources->session_access == SESSION_RW) cache_discard(resources->session_id, resources->_session.rw);
	else cache_finish(resources->_session.ro);
	cache_destroy(resources->session_id);
	resources->session_access = 0;
}
