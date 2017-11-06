#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <pthread.h>		// libpthread

#include "types.h"
#include "log.h"

#include <stdio.h>

#define KEEP 10

struct status
{
	struct string *key;
	struct string *name;
	#if !defined(OS_WINDOWS)
	off_t value;
	#else
	int64_t value;
	#endif
	int state;
	time_t update;
};

static struct vector status;
static pthread_mutex_t status_mutex;

bool status_init(void)
{
	if (!vector_init(&status, VECTOR_SIZE_BASE)) return false;
	return !pthread_mutex_init(&status_mutex, 0);
}

#if !defined(OS_WINDOWS)
void status_set(const struct string *key, off_t value, int state)
#else
void status_set(const struct string *key, int64_t value, int state)
#endif
{
	if (!key) return;

	size_t index;
	struct status *temp, *item = 0;
	time_t now = time(0);

	// Always keep one item - no need to free() and then malloc()

	pthread_mutex_lock(&status_mutex);

	for(index = 0; index < status.length; ++index)
	{
		temp = vector_get(&status, index);
		if (string_equal(key, temp->key))
		{
			if (value >= 0) temp->value = value;
			temp->state = state;
			temp->update = now;
			free(item);

			goto finally;
		}
		else if ((now - temp->update) > KEEP) // If this status is outdated
		{
			// Delete current item. Place the last item on its place
			status.length -= 1;
			vector_get(&status, index--) = vector_get(&status, status.length);
			free(item);
			item = temp;
			free(item->key);
			free(item->name);
		}
	}

	// Add new status item
	if (!item)
	{
		item = malloc(sizeof(struct status));
		if (!item) fail(1, "Memory error");
	}
	item->key = string_alloc(key->data, key->length);
	if (!item->key) fail(1, "Memory error");
	item->name = 0;
	item->value = value;
	item->state = state;
	item->update = now;
	vector_add(&status, item);

	pthread_mutex_unlock(&status_mutex);
	return;

finally:
	// Look for outdated statuses
	for(++index; index < status.length; ++index)
	{
		temp = vector_get(&status, index);
		if ((now - temp->update) > KEEP)
		{
			// Delete current item. Place the last item on its place
			free(temp->key);
			free(temp->name);
			free(temp);
			status.length -= 1;
			vector_get(&status, index--) = vector_get(&status, status.length);
		}
	}

	pthread_mutex_unlock(&status_mutex);
}

static struct string *basename_(const struct string *dest)
{
    size_t last = dest->length - 1, index = last;
    while (index && (dest->data[index - 1] != '/'))
        index -= 1;
    return string_alloc(dest->data + index, last - index + 1);
}

void status_set_name(const struct string *key, const struct string *path)
{
	if (!key) return;

	size_t index;
	struct status *temp;
	struct string *name = basename_(path);
	if (!name) fail(1, "Memory error");

	pthread_mutex_lock(&status_mutex);

	for(index = 0; index < status.length; ++index)
	{
		temp = vector_get(&status, index);
		if (string_equal(key, temp->key))
		{
			free(temp->name);
			temp->name = name;
			break;
		}
	}

	pthread_mutex_unlock(&status_mutex);
}

#if !defined(OS_WINDOWS)
const off_t status_get(const struct string *key, int *restrict state)
#else
const int64_t status_get(const struct string *key, int *restrict state)
#endif
{
	if (!key) return -1;

	size_t index;
	struct status *temp;
	time_t now = time(0);
	#if !defined(OS_WINDOWS)
	off_t value = -1;
	#else
	int64_t value = -1;
	#endif

	pthread_mutex_lock(&status_mutex);

	for(index = 0; index < status.length; ++index)
	{
		temp = vector_get(&status, index);

		if (string_equal(key, temp->key))
		{
			value = temp->value;
			if (state) *state = temp->state;
		}

		if ((now - temp->update) > KEEP) // If this status is outdated
		{
			// Delete current item. Place the last item on its place
			free(temp->key);
			free(temp->name);
			free(temp);
			status.length -= 1;
			vector_get(&status, index--) = vector_get(&status, status.length);
		}
	}

	pthread_mutex_unlock(&status_mutex);

	return value;
}

struct string *status_get_name(const struct string *key)
{
	if (!key) return 0;

	size_t index;
	struct status *temp;
	struct string *value = 0;

	pthread_mutex_lock(&status_mutex);

	for(index = 0; index < status.length; ++index)
	{
		temp = vector_get(&status, index);

		if (string_equal(key, temp->key))
		{
			value = temp->name;
			temp->name = 0;
		}
	}

	pthread_mutex_unlock(&status_mutex);

	return value;
}

void status_term(void)
{
	pthread_mutex_destroy(&status_mutex);
	vector_term(&status);
}

// For testing
/*#include <stdio.h>
#include <inttypes.h>

void lookup(struct string key)
{
	int status;
	off_t value = status_get(&key, &status);
	printf("%s: ", key.data);
	if (value >= 0) printf("%jd %d\n", (intmax_t)value, status);
	else printf("not found\n");
}

#define lookup(key) (lookup)(string(key))

int main(void)
{
	struct string key;
	status_init();

	key = string("test");
	status_set(&key, 1, 1);

	key = string("kuchence");
	status_set(&key, 2, 2);

	key = string("olele");
	status_set(&key, 4, 4);

	key = string("mqu");
	status_set(&key, 5, 5);

	sleep(3);

	key = string("kotence");
	status_set(&key, 3, 3);

	lookup("test");
	lookup("aaa");
	lookup("kotence");

	sleep(3);

	lookup("test");
	lookup("aaa");
	lookup("kotence");

	status_term();
	return 0;
}*/
