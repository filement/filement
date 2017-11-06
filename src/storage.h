void *storage_init(void);
bool storage_create(void *restrict storage);
bool storage_latest(void *restrict storage);
bool storage_setup(void *restrict storage);
bool storage_term(void *restrict storage);
void storage_reset(void *restrict storage);

bool storage_local_settings_add_value(void *restrict storage, const struct string *key, const struct string *value);
bool storage_local_settings_delete_value(void *restrict storage, const struct string *key);
bool storage_local_settings_set_value(void *restrict storage, const struct string *key, const struct string *value);
char *storage_local_settings_get_value(void *restrict storage, const struct string *key);

bool storage_local_users_add(void *restrict storage, struct string *user_name, struct string *password);
bool storage_local_users_set(void *restrict storage, struct string *user_name, struct string *password);
int storage_local_users_get_id(void *restrict storage, struct string *user_name, struct string *password);

bool storage_passport_users_add(void *restrict storage, unsigned passport_id);
bool storage_passport_users_check(void *restrict storage, struct string *passport_id);

bool storage_blocks_add(void *restrict storage,long block_id, int user_id, struct string *location, struct string *size);
bool storage_blocks_del(void *restrict storage, int user_id, int block_id);
struct blocks *storage_blocks_get_by_block_id(void *restrict storage, long block_id, long user_id);
struct blocks_array *storage_blocks_get_blocks(void *restrict storage, long user_id);
bool storage_blocks_truncate(void *restrict storage);
#if defined(OS_QNX) || defined(OS_ANDROID)
bool storage_blocks_reinit(void *restrict storage);
#endif

//bool storage_insert_proxy_list(void *restrict storage, struct proxy_list *);
//struct proxy_list *storage_get_proxy_list(void *restrict storage);


struct string *storage_auth_add(void *restrict storage,struct string *auth_id, struct blocks_array *blocks_array, int rw, int count, int user_id, struct string *name, struct string *data);
bool storage_auth_set_count(void *restrict storage,struct string *auth_id,int count);
bool storage_auth_delete_location(void *restrict storage,struct string *auth_id,int location_id);
bool storage_auth_delete_value(void *restrict storage,struct string *auth_id);
struct auth *storage_auth_get(void *restrict storage,struct string *auth_id);
struct auth_array *storage_auth_list(void *restrict storage, int user_id, int count);

