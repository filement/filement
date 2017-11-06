#define STATE_ERROR 1
#define STATE_PENDING -1
#define STATE_FINISHED 0

// TODO all these functions are deprecated. use cache.h functions instead

bool status_init(void);
void status_set_name(const struct string *key, const struct string *path); // TODO: temporary ?
#if !defined(OS_WINDOWS)
void status_set(const struct string *key, off_t value, int state);
const off_t status_get(const struct string *key, int *restrict state);
#else
void status_set(const struct string *key, int64_t value, int state);
const int64_t status_get(const struct string *key, int *restrict state);
#endif
struct string *status_get_name(const struct string *key); // TODO: temporary ?
void status_term(void);
