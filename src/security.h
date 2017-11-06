#define SALT_SIZE 8
#define HASH_SIZE 32

void security_init(void);
void security_term(void);

void security_random(uint8_t *restrict result, size_t size);

void security_password(const struct string *restrict input, uint8_t password[static restrict SALT_SIZE + HASH_SIZE]);
bool security_authorize(const struct string *restrict input, const uint8_t password[static restrict SALT_SIZE + HASH_SIZE]);
