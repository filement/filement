#define PROXY_HTTP			1
#define PROXY_HTTPS			2

struct connection_proxy
{
	int client;
	struct sockaddr_storage address;
	int protocol;
};

void *main_proxy(void *storage);

#if defined(OS_WINDOWS)
void *main_proxy_windows(void *storage);
#endif
