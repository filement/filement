#define MAGIC_SIZE 16

extern const struct string type_unknown;

const struct string *restrict mime(const unsigned char *restrict magic, size_t size);
