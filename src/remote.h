//typedef char device_uuid_t[UUID_SIZE * 2];

//bool storage_cache(void);

//void *main_event(void *argument);

//device_uuid_t *restrict remote_locations(union json *restrict storage, bool tls, size_t *restrict count);

#define REMOTE_DLNA "getdlnaplaylists.php"
#define REMOTE_GET "getdevicemem.php"
#define REMOTE_SET "updatedevicemem.php"

union json *storage_json(const char *target, size_t length, char uuid[UUID_SIZE * 2]);

int dlna_init(void);
int dlna_reset(void);
int dlna_key(char key[CACHE_KEY_SIZE]);
void dlna_term(void);
