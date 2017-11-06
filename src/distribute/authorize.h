#define RIGHT_PROXY 0x1

#define ADDRESS_SIZE 16

#if !TEST
# define OLD_HOSTNAME "172.16.1.200"
#else
# define OLD_HOSTNAME "217.18.246.187"
#endif
#define OLD_PORT     3306
#define OLD_USERNAME "root"
#define OLD_PASSWORD "parola"
#define OLD_SCHEMA   "filement_profiles"

struct credential
{
	uint8_t address[ADDRESS_SIZE];
	struct string hostname;
	unsigned port;
	unsigned rights;
};

const struct credential *authorize_rights(const struct sockaddr_storage *restrict storage);

int32_t authorize_id(const char *restrict buffer, char key[restrict 16 + 1]);

// WARNING: This function may modify email.
int32_t authorize_email(struct string *restrict email);

bool authorize_secret(const char uuid[UUID_SIZE], const char secret[SECRET_SIZE]);
