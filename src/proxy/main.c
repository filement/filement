#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <pthread.h>		// libpthread

#include "types.h"
#include "log.h"
#include "format.h"
#include "stream.h"
#include "io.h"
#include "http.h"
#include "notify.h"
#include "recognition.h"
#include "proxy.h"

#define ROOT "/tmp/"

#define PORT_PROXY 443

#define ERROR_DAEMON "Failed to daemonize\n"
#define ERROR_SOCKET "Socket error\n"
#define ERROR_BIND "Bind error\n"
#define ERROR_LISTEN "Listen error\n"
#define ERROR_EPOLL "Epoll error\n"
#define ERROR_SIGNAL "Signal handling error\n"
#define ERROR_THREAD "Unable to start a thread\n"

// TODO: choose good values below in order to maximize efficiency
#define LISTEN_MAX 10
#define EVENTS_MAX 8

// TODO: are these values good
#define TIMEOUT_DISCONNECT 10
#define TIMEOUT_PING 250

struct device_state
{
	struct event_info *connection;
	time_t expire;
	size_t activity;
	char *uuid;
};

// Consider as disconnected devices whose connectivity has not changed for TIMEOUT_DISCONNECT seconds or who have not sent ping for TIMEOUT_PING seconds.

typedef struct device_state *type;

struct heap
{
	type *data; // Array with the elements.
	size_t _size; // Number of elements that the heap can hold withour resizing.
	size_t count; // Number of elements actually in the heap.
};

// Returns the biggest element in the heap
#define heap_front(h) (*(h)->data)

// Frees the allocated memory
#define heap_term(h) (free((h)->data))

// true		a is in front of b
// false	b is in front of a
#define CMP(a, b) ((a)->expire <= (b)->expire)

#define BASE_SIZE 16

static bool heap_init(struct heap *restrict h)
{
	h->data = malloc(sizeof(type) * BASE_SIZE);
	if (!h->data) return false; // memory error
	h->_size = BASE_SIZE;
	h->count = 0;
	return true;
}

// Inserts element to the heap.
static bool heap_push(struct heap *restrict h, type value)
{
	size_t index, parent;

	// Resize the heap if it is too small to hold all the data.
	if (h->count == h->_size)
	{
		type *buffer;
		h->_size <<= 1;
		buffer = realloc(h->data, sizeof(type) * h->_size);
		if (h->data) h->data = buffer;
		else return false;
	}

	// Find out where to put the element and put it.
	for(index = h->count++; index; index = parent)
	{
		parent = (index - 1) >> 1;
		if CMP(h->data[parent], value) break;
		h->data[index] = h->data[parent];
		h->data[index]->activity = index;
	}
	h->data[index] = value;
	h->data[index]->activity = index;

	return true;
}

// Removes the biggest element from the heap.
static void heap_pop(struct heap *restrict h)
{
	size_t index, swap, other;

	// Remove the biggest element.
	type temp = h->data[--h->count];

	// Resize the heap if it's consuming too much memory.
	if ((h->count <= (h->_size >> 2)) && (h->_size > BASE_SIZE))
	{
		h->_size >>= 1;
		h->data = realloc(h->data, sizeof(type) * h->_size);
	}

	// Reorder the elements.
	for(index = 0; true; index = swap)
	{
		// Find which child to swap with.
		swap = (index << 1) + 1;
		if (swap >= h->count) break; // If there are no children, the heap is reordered.
		other = swap + 1;
		if ((other < h->count) && CMP(h->data[other], h->data[swap])) swap = other;
		if CMP(temp, h->data[swap]) break; // If the bigger child is less than or equal to its parent, the heap is reordered.

		h->data[index] = h->data[swap];
		h->data[index]->activity = index;
	}
	h->data[index] = temp;
	h->data[index]->activity = index;
}

static void heap_ascend(struct heap *restrict h, size_t index)
{
	size_t parent;

	type temp = h->data[index];

	// Reorder the elements.
	while (index)
	{
		parent = (index - 1) >> 1;
		if CMP(h->data[parent], temp) break;

		h->data[index] = h->data[parent];
		h->data[index]->activity = index;
		index = parent;
	}
	h->data[index] = temp;
	h->data[index]->activity = index;
}

static void heap_descend(struct heap *restrict h, size_t index)
{
	size_t swap, other;

	type temp = h->data[index];

	// Reorder the elements.
	while (true)
	{
		// Find which child to swap with.
		swap = (index << 1) + 1;
		if (swap >= h->count) break; // If there are no children, the heap is reordered.
		other = swap + 1;
		if ((other < h->count) && CMP(h->data[other], h->data[swap])) swap = other;
		if CMP(temp, h->data[swap]) break; // If the bigger child is less than or equal to its parent, the heap is reordered.

		h->data[index] = h->data[swap];
		h->data[index]->activity = index;
		index = swap;
	}
	h->data[index] = temp;
	h->data[index]->activity = index;
}

static struct dict devices, clients;

// Devices that have no recent activity and are possibly no longer online.
static struct heap activity;

static int client_finished(int epoll, struct event_info *restrict info, uint32_t error);

// TODO: think about what signals might screw
// TODO: think about signals. epoll_pwait could be useful?

/*
The main thread listens for devices and clients. Each incoming connection is added to epoll.
When a device is recognized, it is listed as connected (struct dict devices).
When a client requests a resource from a specific device, it is connected to that device. The connection is handled in a separate thread while the main thread is responsible to handle connection hang ups.
If a client requests resource from a connected device that is currently unavailable, the client is added into a queue (struct dict clients) so the device can handle the request when available.
*/

// TODO: find a better fix for this
// Shows whether a handler has modified the epoll pool to indicate whether epoll_wait() should be called again.
static bool epoll_sticky = false;

static inline void trace_(const char operation[restrict 3], const char uuid[restrict UUID_LENGTH])
{
	// WARNING: Race conditions may occur.
	// WARNING: write() may not write all the data

	// TODO change writeall to write and remove the mutex (for performance) TODO wtf?

//#if defined(DEBUG)
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

	char buffer[3 + 1 + UUID_LENGTH + 1], *start = buffer;
	start = format_bytes(start, operation, 3);
	*start++ = ' ';
	*format_bytes(start, uuid, UUID_LENGTH) = '\n';
	pthread_mutex_lock(&mutex);
	writeall(2, buffer, sizeof(buffer));
	pthread_mutex_unlock(&mutex);
//#endif
}

static int server_socket(void)
{
	int ssocket;
	int optval;

	// Create socket
	ssocket = socket(PF_INET6, SOCK_STREAM, 0);
	if (ssocket < 0) fail(3, ERROR_SOCKET);

	// Disable TCP time_wait
	optval = 1;
	if (setsockopt(ssocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0)
		fail(3, ERROR_SOCKET);

	return ssocket;
}

static int device_finished(int epoll, struct event_info *restrict info, uint32_t error)
{
	epoll_ctl(epoll, EPOLL_CTL_DEL, info->fd, 0); // TODO: maybe check for error here
	close(info->fd);

	// Get device state.
	struct device_state *state = dict_get(&devices, &info->uuid);
	char *uuid = info->uuid.data;
	free(info);

	trace_(" - ", uuid);

	// If the device is still connected but this is the connection waiting for requests, set device as busy.
	if (state && (state->connection == info))
	{
		time_t old = state->expire;
		state->connection = 0;
		state->expire = time(0) + TIMEOUT_DISCONNECT;
		((old >= state->expire) ? heap_ascend : heap_descend)(&activity, state->activity);
	}
	else free(uuid);

	return -1;
}

static int device_connected(int epoll, struct event_info *restrict info, uint32_t error)
{
	if (error) return device_finished(epoll, info, error);

	// Handle ping requests
	unsigned char ping;
	if (read(info->fd, &ping, 1) == 1)
	{
		// Keep device as active for TIMEOUT_PING more seconds.
		struct device_state *state = dict_get(&devices, &info->uuid);
		state->expire = time(0) + TIMEOUT_PING;
		heap_descend(&activity, state->activity);
	}

	return 0;
}

static int device_waiting(int epoll, struct event_info *restrict info, uint32_t error)
{
	struct string uuid = {.length = UUID_LENGTH};
	int fd;

	int connections = -1;
	epoll_ctl(epoll, EPOLL_CTL_DEL, info->fd, 0); // TODO: maybe check for error here

	// Read the recognition information
	int bytes;
	ioctl(info->fd, FIONREAD, &bytes);
	if (bytes != (sizeof(uuid.data) + sizeof(fd)))
	{
		close(info->fd);
		free(info);
		return connections;
	}
	readall(info->fd, (char *)&uuid.data, sizeof(uuid.data));
	readall(info->fd, (char *)&fd, sizeof(fd));

	close(info->fd);
	info->fd = fd;

	time_t now = time(0);

	// Update device state.
	struct device_state *state = dict_get(&devices, &uuid);
	if (state)
	{
		// If the device is already connected, terminate the old connection.
		if (state->connection)
		{
			connections -= 1;
			epoll_ctl(epoll, EPOLL_CTL_DEL, state->connection->fd, 0); // TODO: maybe check for error here
			epoll_sticky = true;
			close(state->connection->fd);
			free(state->connection);

			trace_(" ! ", uuid.data);
		}

		state->expire = now + TIMEOUT_PING;
		heap_descend(&activity, state->activity);
	}
	else
	{
		state = malloc(sizeof(struct device_state));
		if (!state) fail(1, "Memory error");

		// TODO: make the code below better

		state->uuid = memdup(uuid.data, uuid.length);
		if (!state->uuid)
		{
			free(state);
			free(uuid.data);
			close(info->fd);
			free(info);
			return connections; // memory error
		}

		if (dict_add(&devices, &uuid, state))
		{
			free(state->uuid);
			free(state);
			free(uuid.data);
			close(info->fd);
			free(info);
			return connections; // memory error
		}

		state->expire = now + TIMEOUT_PING;
		if (!heap_push(&activity, state))
		{
			dict_remove(&devices, &uuid);
			free(state->uuid);
			free(state);
			free(uuid.data);
			close(info->fd);
			free(info);
			return connections; // memory error
		}

		notify_distribute(&uuid, EVENT_ON);

		trace_(" * ", uuid.data);
	}

	info->uuid = uuid;

	trace_(" + ", uuid.data);

	struct queue *queue = dict_get(&clients, &uuid);
	if (queue) // If there are clients waiting for this device
	{
		info->handler = &device_finished;

		struct request *request = queue_pop(queue);
		if (!queue->length) free(dict_remove(&clients, &uuid)); // remove the queue if no clients are waiting

		// Change device state to connected and busy.
		state->connection = 0;
		state->expire = now + TIMEOUT_DISCONNECT;
		heap_ascend(&activity, state->activity);

		struct thread_info *thread_info = malloc(sizeof(struct thread_info));
		if (!thread_info) fail(1, "Memory error");
		thread_info->device = info;
		thread_info->client = request->info;
		thread_info->request = request;
		thread_info->epoll = epoll;

		trace_("<->", info->uuid.data);

		request->info->handler = &client_finished;

		// Two new epoll items will be added after the thread started below finishes (so += 1 instead of -= 1).
		connections += 1;

		epoll_ctl(epoll, EPOLL_CTL_DEL, request->stream.fd, 0); // TODO: maybe check for error here
		epoll_sticky = true;

		// Initiate proxy connection between the client and the device.
		// TODO: check for error
		// fail(8, ERROR_THREAD);
		pthread_create(&thread_info->thread, 0, &main_proxy, thread_info);
		pthread_detach(thread_info->thread);
	}
	else
	{
		info->handler = &device_connected;

		// List the device as connected if it is not connected yet. Mark it as free
		state->connection = info;

		struct epoll_event event = {.events = EPOLLIN | EPOLLRDHUP, .data.ptr = info};
		connections += 1;
		epoll_ctl(epoll, EPOLL_CTL_ADD, info->fd, &event); // TODO: maybe check for error here
	}

	return connections;
}

static int device_pending(int epoll, struct event_info *restrict info, uint32_t error)
{
	struct sockaddr_in6 address;
	socklen_t length = sizeof(address);
	int fd = accept(info->fd, (struct sockaddr *)&address, &length);
	if (fd < 0) return 0;

	int fifo[2];
	if (pipe(fifo) < 0) return 0; // System resources not sufficient

	struct event_info *device = malloc(sizeof(struct event_info));
	if (!device) fail(1, "Memory error");
	device->handler = &device_waiting;
	device->fd = fifo[0];
	//device->uuid.data = 0;
	//device->address = address;

	// Start recognition thread
	{
		int *rec = malloc(sizeof(int) * 2);
		if (!rec) fail(1, "Memory error");

		rec[0] = fd;
		rec[1] = fifo[1];

		pthread_t thread;
		pthread_create(&thread, 0, &recognize_device, rec);
		pthread_detach(thread);
	}

	// Wait for device recognition
	struct epoll_event event = {.events = EPOLLIN | EPOLLRDHUP, .data.ptr = device};
	return !epoll_ctl(epoll, EPOLL_CTL_ADD, device->fd, &event); // TODO: handle error conditions
}

static int client_finished(int epoll, struct event_info *restrict info, uint32_t error)
{
	// Here error is always true

	epoll_ctl(epoll, EPOLL_CTL_DEL, info->fd, 0); // TODO: maybe check for error here
	close(info->fd);

	free(info);

	return -1;
}

static int client_connected(int epoll, struct event_info *restrict info, uint32_t error)
{
	// Here error is always true

	// If the device the current client is waiting for is still connected, remove current client from requests queue.

	struct queue *queue = dict_get(&clients, &info->uuid);
	struct request *request;
	struct queue_item **slot = &queue->start, *item = *slot;

	// Find current request in the queue.
	// TODO: this is slow
	while (true)
	{
		request = item->data;
		if (request->info == info) break;
		slot = &item->next;
		item = *slot;
	}

	// Remove the request from the queue.
	*slot = item->next;
	if (!item->next) queue->end = slot;
	if (!--queue->length) free(dict_remove(&clients, &info->uuid));

	epoll_ctl(epoll, EPOLL_CTL_DEL, info->fd, 0); // TODO: maybe check for error here
	epoll_sticky = true;

	free(info->uuid.data);
	free(info);
	free(item);

	// WARNING: Assuming that request == &request->stream since main_notify will free &request->stream when it's done
	notify_not_found(&request->stream);

	return -1;
}

static int client_waiting(int epoll, struct event_info *restrict info, uint32_t error)
{
	struct string uuid = {.length = UUID_LENGTH};
	struct request *request;

	int connections = -1;
	epoll_ctl(epoll, EPOLL_CTL_DEL, info->fd, 0); // TODO: maybe check for error here

	// Read the recognition information
	int bytes;
	if ((ioctl(info->fd, FIONREAD, &bytes) < 0) || (bytes != (sizeof(uuid.data) + sizeof(request))))
	{
		close(info->fd);
		free(info);
		return -1;
	}
	readall(info->fd, (char *)&uuid.data, sizeof(uuid.data));
	readall(info->fd, (char *)&request, sizeof(request));

	close(info->fd);
	info->fd = request->stream.fd;

	extern struct dict devices, clients;

	time_t now = time(0);

	// Check whether the requested device is connected.
	struct device_state *state = dict_get(&devices, &uuid);
	if (!state)
	{
		free(uuid.data);
		free(info);

		// WARNING: Assuming that request == &request->stream since main_notify will free &request->stream when it's done
		notify_not_found(&request->stream); // the device is not connected

		return connections;
	}

	info->uuid = uuid;

	if (state->connection)
	{
		trace_("<->", uuid.data);

		info->handler = &client_finished;

		struct thread_info *thread_info = malloc(sizeof(struct thread_info));
		if (!thread_info) fail(1, "Memory error");
		thread_info->device = state->connection;
		thread_info->client = info;
		thread_info->request = request;
		thread_info->epoll = epoll;

		// This socket will be managed by a separate thread so epoll should ignore it.
		epoll_ctl(epoll, EPOLL_CTL_DEL, state->connection->fd, 0); // TODO: handle error conditions
		epoll_sticky = true;

		// Change device state to inactive.
		time_t old = state->expire;
		state->connection = 0;
		state->expire = now + TIMEOUT_DISCONNECT;
		((old >= state->expire) ? heap_ascend : heap_descend)(&activity, state->activity);

		// Initiate proxy connection between the client and the device.
		pthread_create(&thread_info->thread, 0, &main_proxy, thread_info);
		pthread_detach(thread_info->thread);
	}
	else
	{
		// Add the client as waiting for this uuid
		struct queue *queue = dict_get(&clients, &uuid);

		trace_(" ? ", uuid.data);

		info->handler = &client_connected;

		if (!queue)
		{
			queue = queue_alloc();
			if (!queue) fail(1, "Memory error");
			if (dict_add(&clients, &uuid, queue)) fail(1, "Memory error");
		}

		request->info = info;
		if (!queue_push(queue, request)) fail(1, "Memory error");

		// Make epoll detect only errors for this connection.
		struct epoll_event event = {.events = EPOLLRDHUP, .data.ptr = info};
		epoll_ctl(epoll, EPOLL_CTL_ADD, info->fd, &event); // TODO: maybe check for error here
	}

	connections += 1;
	return 0;
}

static int client_pending(int epoll, struct event_info *restrict info, uint32_t error)
{
	struct sockaddr_in6 address;
	socklen_t length = sizeof(address);
	int fd = accept(info->fd, (struct sockaddr *)&address, &length);
	if (fd < 0) return 0;

	int *fifo = malloc(sizeof(int) * 2);
	if (!fifo) fail(1, "Memory error");
	if (pipe(fifo) < 0)
	{
		close(fd);
		free(fifo);
		return 0; // TODO: can this be error?
	}

	struct event_info *client = malloc(sizeof(struct event_info));
	if (!client) fail(1, "Memory error");
	client->handler = &client_waiting;
	client->fd = fifo[0];
	//client->uuid.data = 0;
	//client->address = address;

	// Start recognition thread.
	fifo[0] = fd;
	pthread_t thread;
#if defined(TLS)
	if (info->tls) pthread_create(&thread, 0, &recognize_client_tls, fifo);
	else
#endif
		pthread_create(&thread, 0, &recognize_client, fifo);
	pthread_detach(thread);

	// Wait for client recognition
	struct epoll_event event = {.events = EPOLLIN | EPOLLRDHUP, .data.ptr = client};
	return !epoll_ctl(epoll, EPOLL_CTL_ADD, client->fd, &event); // TODO: handle error conditions
}

int main(void)
{
	// Daemonize
	{
#if RUN_MODE > Debug
		pid_t pid = fork();
		if (pid < 0) fail(2, ERROR_DAEMON);
		if (pid) return 0; // Terminate the parent process

		setsid();
#endif

		umask(0);

		close(0);
		close(1);
		close(2);

		open("/dev/null", O_RDONLY);
		open("/dev/null", O_WRONLY);
		if (open("/var/log/proxy", O_CREAT | O_WRONLY | O_APPEND, 0644) < 0) fail(2, ERROR_DAEMON);

		// TODO finish this
		// Write PID to a file and lock it to allow only one instance of the daemon
		/*int pid_fd = creat("/var/run/" NAME ".pid", 0644);
		if (pid_fd < 0) fail(2, ERROR_DAEMON);
		if (lockf(pid_fd, F_TLOCK, 0) < 0) fail(2, ERROR_DAEMON);
		char *pid_str;
		ssize_t size = asprintf(&pid_str, "%d\n", getpid());
		if (size < 0) fail(2, ERROR_DAEMON);
		write(pid_fd, pid_str, (off_t)size); // TODO: check for errors
		free(pid_str);*/

		chdir(ROOT);
		//chroot(ROOT); TODO: don't set this. it causes getaddrinfo to fail

		// TODO: setuid, setgid
	}

	// Test whether the necessary pipe operations are atomic on this system.
	if (PIPE_BUF < NOTIFY_BUFFER) fail(2, "Pipes support not sufficient");

	if (!notify_init()) fail(2, "Unable to initialize notification subsystem");

	struct sigaction action = {
		.sa_handler = SIG_IGN,
		.sa_mask = 0,
		.sa_flags = 0
	};
	sigaction(SIGPIPE, &action, 0); // this never fails if properly used

#if defined(TLS)
	if (tls_init() < 0) fail(2, "SSL initialization error");
#endif

	// Support both IPv4 and IPv6
	// client - connection between the web client and the proxy
	// device - connection between the proxy and a device with filement web server installed
#if defined(TLS)
	int socket_device = server_socket(), socket_client = server_socket(), socket_client_tls = server_socket();
#else
	int socket_device = server_socket(), socket_client = server_socket();
#endif
	struct sockaddr_in6 address = {
		.sin6_family = AF_INET6,
		.sin6_addr = in6addr_any,
		.sin6_port = htons(PORT_PROXY)
	};

	// Start listening for filement web servers
	if (bind(socket_device, (struct sockaddr *)&address, sizeof(address)) < 0)
		fail(4, ERROR_BIND);
	if (listen(socket_device, LISTEN_MAX) < 0)
		fail(5, ERROR_LISTEN);

	// Start listening for web clients
	address.sin6_port = htons(PORT_HTTP);
	if (bind(socket_client, (struct sockaddr *)&address, sizeof(address)) < 0)
		fail(4, ERROR_BIND);
	if (listen(socket_client, LISTEN_MAX) < 0)
		fail(5, ERROR_LISTEN);

#if defined(TLS)
	// Start listening for web clients with TLS
	address.sin6_port = htons(PORT_HTTPS);
	if (bind(socket_client_tls, (struct sockaddr *)&address, sizeof(address)) < 0)
		fail(4, ERROR_BIND);
	if (listen(socket_client_tls, LISTEN_MAX) < 0)
		fail(5, ERROR_LISTEN);
#endif

#if defined(TLS)
	unsigned connections = 3;
#else
	unsigned connections = 2;
#endif
	int epoll = epoll_create1(0);
	if (epoll < 0) fail(6, ERROR_EPOLL);

	struct epoll_event event = {.events = EPOLLIN}, response[EVENTS_MAX];

	struct event_info *event_info;

	// Initialize epoll for devices
	event_info = malloc(sizeof(struct event_info));
	if (!event_info) fail(1, "Memory error");
	event_info->handler = &device_pending;
	event_info->fd = socket_device;
	event_info->tls = false;
	//event_info->uuid.data = 0;
	event.data.ptr = event_info;
	epoll_ctl(epoll, EPOLL_CTL_ADD, socket_device, &event);
	//if (epoll_ctl(epoll, EPOLL_CTL_ADD, socket_device, &event) < 0) ; // TODO: check for error

	// Initialize epoll for clients
	event_info = malloc(sizeof(struct event_info));
	if (!event_info) fail(1, "Memory error");
	event_info->handler = &client_pending;
	event_info->fd = socket_client;
	event_info->tls = false;
	//event_info->uuid.data = 0;
	event.data.ptr = event_info;
	epoll_ctl(epoll, EPOLL_CTL_ADD, socket_client, &event);
	//if (epoll_ctl(epoll, EPOLL_CTL_ADD, socket_client, &event) < 0) ; // TODO: check for error

#if defined(TLS)
	// Initialize epoll for clients with TLS
	event_info = malloc(sizeof(struct event_info));
	if (!event_info) fail(1, "Memory error");
	event_info->handler = &client_pending;
	event_info->fd = socket_client_tls;
	event_info->tls = true;
	//event_info->uuid.data = 0;
	event.data.ptr = event_info;
	epoll_ctl(epoll, EPOLL_CTL_ADD, socket_client_tls, &event);
	//if (epoll_ctl(epoll, EPOLL_CTL_ADD, socket_client_tls, &event) < 0) ; // TODO: check for error
#endif

	int ready;
	int status;

	uint32_t errors;

	struct string key;
	struct device_state *state;
	time_t now;
	struct queue *queue;
	struct request *request;

	// Initialize dictionaries for waiting devices and clients
	dict_init(&devices, DICT_SIZE_BASE); // TODO: error
	dict_init(&clients, DICT_SIZE_BASE); // TODO: error

	heap_init(&activity);

	while (true)
	{
		ready = epoll_wait(epoll, response, EVENTS_MAX, TIMEOUT);
		if (ready < 0) continue; // errno == EINTR
		while (ready--)
		{
			event_info = response[ready].data.ptr;
			errors = response[ready].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP);

			// Call handler for the current connection
			status = (*event_info->handler)(epoll, event_info, errors);
#if defined(DEBUG)
			//if (status) printf("		%u -> %u\n", connections, connections + status);
#endif
			connections += status;

			// TODO: fix this
			// Check whether the epoll pool was modified. If so, response array is now obsolete.
			if (epoll_sticky)
			{
				epoll_sticky = false;
				break;
			}
		}

		// Remove disconnected devices from the queue of inactive devices.
		now = time(0);
		while (activity.count)
		{
			state = heap_front(&activity);
			if (state->expire > now) break; // the rest of the devices are still considered connected
			key = string(state->uuid, UUID_LENGTH);

			// Disconnect all the clients waiting for this device.
			if (queue = dict_remove(&clients, &key))
			{
				while (request = queue_pop(queue))
				{
					epoll_ctl(epoll, EPOLL_CTL_DEL, request->stream.fd, 0); // TODO: maybe check for error here

					// WARNING: Assuming that request == &request->stream since main_notify() will free &request->stream when it's done
					free(request->info);
					notify_not_found(&request->stream); // the device is not connected

					connections -= 1;
				}
				free(queue);
			}

			notify_distribute(&key, EVENT_OFF);

			trace_("XXX", key.data);

			// If there is a device connection that timed out, free its data.
			if (state->connection)
			{
				struct event_info *info = state->connection;

				epoll_ctl(epoll, EPOLL_CTL_DEL, info->fd, 0); // TODO: maybe check for error here
				close(info->fd);
				free(info->uuid.data);
				free(info);

				connections -= 1;
			}

			dict_remove(&devices, &key);
			free(state->uuid);
			free(state);
			heap_pop(&activity);
		}
	}

	// This is never reached

	// heap_term(&activity); TODO free each item

	// dict_term(devices);
	// dict_term(clients);

	// close(socket_client);
	// close(socket_device);

	// TODO: event_info for the server sockets are never freed
	// TODO: to terminate devices properly one must ignore all device_busy entries (the ones with fd == -1)

	// close(epoll);

	// tls_term();

#ifndef DEBUG
	//close(pid_fd);
#endif

	return 0;
}
