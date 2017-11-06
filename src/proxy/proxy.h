// TODO: think of better data structures

struct event_info
{
	int (*handler)(int, struct event_info *restrict, uint32_t);
	struct string uuid;
	int fd;
	bool tls;
	//struct sockaddr_in6 address;
};

struct request
{
	struct stream stream;
	struct event_info *info; // used only if the client request is added to a queue
};

struct thread_info
{
	pthread_t thread; // TODO: remove this
	struct event_info *device, *client;
	struct request *request;
	int epoll;
};

void *main_proxy(void *arg);
