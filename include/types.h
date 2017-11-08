#ifndef _TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <features.h>
#if ((__GLIBC__ == 2) && (__GLIBC_MINOR__ >= 24)) || (__GLIBC__ > 2)
# define READDIR
#endif

// TODO errors should be in another file

#if defined(OS_WINDOWS)
# undef ERROR
#endif

// System resources are not sufficient to handle the request.
#define ERROR_MEMORY				-1

// Invalid input data.
#define ERROR_INPUT					-2

// Request requires access rights that are not available.
#define ERROR_ACCESS				-3

// Entity that is required for the operation is missing.
#define ERROR_MISSING				-4

// Unable to create a necessary entity because it exists.
#define ERROR_EXIST					-5

// Filement filesystem internal error.
#define ERROR_EVFS					-6

// Temporary condition caused error. TODO maybe rename this to ERROR_BUSY
#define ERROR_AGAIN					-7

// Unsupported feature is required to satisfy the request.
#define ERROR_UNSUPPORTED			-8 /* TODO maybe rename to ERROR_SUPPORT */

// Read error.
#define ERROR_READ					-9

// Write error.
#define ERROR_WRITE					-10

// Action was cancelled.
#define ERROR_CANCEL				-11

// An asynchronous operation is now in progress.
#define ERROR_PROGRESS				-12

// Unable to resolve domain.
#define ERROR_RESOLVE				-13

// Network operation failed.
#define ERROR_NETWORK				-14

// An upstream server returned invalid response.
#define ERROR_GATEWAY				-15

// Invalid session.
#define ERROR_SESSION				-16

// Unknown error.
#define ERROR						-32767

/* String */

// TODO: consider using BUFSIZ here
#define BLOCK_SIZE 4096

// String literal. Contains length and pointer to the data. The data is usually NUL-terminated so that it can be passed to standard functions without modification.
struct string
{
	char *data;
	size_t length;
};

// Generates string literal from data and length. If no length is passed, it assumes that string data is static array and determines its length with sizeof.
// Examples:
//  struct string name = string(name_data, name_length);
//  struct string key = string("uuid");
#define string_(data, length, ...) (struct string){(data), (length)}
#define string(...) string_(__VA_ARGS__, sizeof(__VA_ARGS__) - 1)

#define string_equal(s0, s1) (((s0)->length == (s1)->length) && !memcmp((s0)->data, (s1)->data, (s0)->length))

int string_diff(const struct string *s0, const struct string *s1);

// Initializes an existing struct string with data and length. The string data must be freed with free(s->data).
struct string *string_init(struct string *restrict s, const char *data, size_t length);

// Creates a new string and sets its data and length accordingly. The string must be freed with free()
struct string *string_alloc(const char *data, size_t length);

#define string_concat(...) (string_concat)(__VA_ARGS__, (struct string *)0)
struct string *(string_concat)(const struct string *start, ...);

struct string *string_concat_alloc(const char *data_begin, size_t length_begin, const char *data_end, size_t length_end); // TODO: deprecated

struct string *restrict string_serialize(unsigned char *restrict data, int length);

size_t integer_digits(intmax_t number, unsigned base);

void *memdup(const void *restrict data, size_t size);

size_t *restrict kmp_table(const struct string *pattern);
ssize_t kmp_search(const struct string *pattern, const size_t *table, const struct string *search);

/* Vector */

struct vector
{
	void **data;
	size_t length, size;
};

#define VECTOR_SIZE_BASE 4

bool vector_init(struct vector *restrict v, size_t size);
#define vector_get(vector, index) ((vector)->data[index])
bool vector_add(struct vector *restrict v, void *value);
#define vector_term(v) (free(((struct vector *)(v))->data))

/* Dictionary */

#define DICT_SIZE_BASE 16

struct dict
{
	struct dict_item
	{
		size_t key_size;
		const char *key_data;
		void *value;
		struct dict_item *_next;
	} **items;
	size_t count, size;
};

struct dict_iterator
{
	size_t index;
	struct dict_item *item;
};

// Initializes dictionary iterator and returns the first item
const struct dict_item *dict_first(struct dict_iterator *restrict it, const struct dict *d);
// Returns next item in a dictionary iterator
const struct dict_item *dict_next(struct dict_iterator *restrict it, const struct dict *d);

// WARNING: size must be a power of 2
bool dict_init(struct dict *restrict dict, size_t size);

int dict_set(struct dict *restrict dict, const struct string *key, void *value, void **result);
#define dict_add(dict, key, value) dict_set((dict), (key), (value), 0)

void *dict_get(const struct dict *dict, const struct string *key);

void *dict_remove(struct dict *restrict dict, const struct string *key);

void dict_term(struct dict *restrict dict);
void dict_term_custom(struct dict *restrict dict, void (*custom)(void *));

/* Queue */

struct queue
{
	struct queue_item
	{
		void *data;
		struct queue_item *next;
	} *start, **end;
	size_t length;
};

void queue_init(struct queue *restrict queue);
struct queue *queue_alloc(void);
bool queue_push(struct queue *restrict queue, void *restrict data);
void *queue_pop(struct queue *restrict queue);
void *queue_remove(struct queue *restrict queue, struct queue_item **item);
void queue_term(struct queue *restrict queue);

#define _TYPES_H
#endif
