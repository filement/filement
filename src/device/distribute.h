#if !defined(OS_WINDOWS)
# include "protocol.h"
#else
# include "../protocol.h"
#endif

//#define distribute_vector(size) (3 + (size))

void distribute_term(void);

struct stream *distribute_request_start(const char uuid[restrict UUID_SIZE], uint16_t cmd, const struct string *restrict request);
void distribute_request_finish(bool success);

#if defined(DEVICE)
char *distribute_register(uint16_t cmd, const struct string *restrict id, const struct string *restrict devname, uint32_t *restrict client_id);
# define distribute_register_email(email, devname, client_id) distribute_register(CMD_REGISTER_EMAIL, (email), (devname), (client_id))
# define distribute_register_auth(auth_id, devname, client_id) distribute_register(CMD_REGISTER_AUTH, (auth_id), (devname), (client_id))
#endif

#if defined(BELKIN)
int32_t distribute_user_add(const struct string *restrict auth_id, const struct string *restrict devname);
bool distribute_user_rm(uint32_t client_id);
#endif
