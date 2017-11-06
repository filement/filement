struct resources
{
	struct stream stream;
	struct sockaddr_storage address;
	void *storage;

	char session_id[CACHE_KEY_SIZE + 1];
	enum {SESSION_RO = 1, SESSION_RW} session_access;
	union
	{
		const struct cache *ro;
		struct cache *rw;
	} _session;
	union
	{
		const union json *ro;
		union json *rw;
	} session;

	struct string *auth_id;
	struct auth *auth;

#if defined(DISTRIBUTE)
	int control;
#endif

#ifdef FTP
	void *handle;
#endif
};

void connection_release(int control, int status);

void server_daemon(void);

void server_init(void);
void server_term(void);

void server_listen(void *storage);

bool address_local_host(const struct sockaddr_storage *restrict address);
bool address_local_network(const struct sockaddr_storage *restrict address);

#if defined(OS_WINDOWS)
void server_listen_windows(void *storage);
void *server_serve_windows(void *arg);
#endif
