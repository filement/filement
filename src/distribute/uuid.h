#define CLIENTS_LIMIT (1 << 24)

struct string *restrict uuid_alloc(uint32_t client_id, uint16_t platform_id);
void uuid_extract(const char uuid[restrict UUID_SIZE], uint32_t *restrict client_id, uint16_t *restrict platform_id);
