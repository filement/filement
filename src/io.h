#if !defined(OS_WINDOWS)
int errno_error(int error);
#else
# define ELOOP -1
# undef ERROR
# define errno_error(e) -1
#endif

struct blocks
{
	long int block_id;
	long int user_id;
	struct string *location;
	unsigned long location_id;
	struct string *name;
	long int size;

#if defined(PUBCLOUD)
	unsigned uid, gid; // TODO make this uid_t
#endif
};

struct blocks_array
{
	unsigned blocks_size;
	struct blocks *blocks[];
};

struct auth
{
	struct string *auth_id;
	struct blocks_array *blocks_array;
	struct string *name;
	struct string *data;
	int count; 
	int rw;
#if defined(PUBCLOUD)
	unsigned uid, gid; // TODO make this uid_t
#endif
#ifdef FTP
	time_t time;
	union json *login;
#endif
};

struct auth_array
{
	unsigned count;
	struct auth *auth[];
};

struct fs_print_status_struct
{
	struct string *key;
	unsigned long sum_size;
	unsigned long last_size;
	time_t time_start;
	int time_init;
};


bool writeall(int fd, const char *buffer, size_t total);
bool readall(int fd, char *restrict buffer, size_t total);

#ifdef OS_WINDOWS
bool WRITEALL(int file, char *buffer, size_t total);
bool READALL(int file, char *restrict buffer, size_t total);
#endif

int socket_connect(const char *hostname, unsigned port);

ssize_t socket_read(int fd, char *restrict buffer, size_t total);
ssize_t socket_write(int fd, char *restrict buffer, size_t total);
