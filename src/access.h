struct string *access_path_compose(struct string *restrict prefix, struct string *restrict suffix, bool modify);

struct string *access_fs_concat_path(struct string *restrict core_path,struct string *restrict path,bool modify);

#if defined(DEVICE) || defined(PUBCLOUD) || defined(FTP)
struct blocks_array *access_get_blocks_array(struct resources *restrict resources);
struct blocks *access_get_blocks(struct resources *restrict resources, int block_id);
#endif

struct blocks *access_auth_get_blocks(struct resources *restrict resources, int block_id);
bool access_auth_check_location(struct resources *restrict resources,struct string *request_location,int block_id);
bool access_check_write_access(struct resources *restrict resources);
bool access_check_host(struct string *host);
bool access_is_session_granted(struct resources *restrict resources);
void free_blocks_array(struct blocks_array *blocks_array);
struct blocks_array *access_granted_get_blocks_array(void);
int access_fs_get_relative_path(struct string *restrict initial_path,struct string *restrict path,bool win_slash);
