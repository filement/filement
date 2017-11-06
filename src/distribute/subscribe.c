#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "types.h"
#include "format.h"
#include "stream.h"
#include "io.h"
#include "protocol.h"
#include "cache.h"
#include "server.h"
#include "uuid.h"
#include "subscribe.h"

#define CONNECTION_TIMEOUT 300
#define SUBSCRIBE_TIMEOUT 20

#define INBOX_REQUEST_HEADER (sizeof(struct message *) + sizeof(size_t))

#define EVENTS_MAX 32

#define SUBSCRIPTION_REQUEST (sizeof(struct subscriber *) + sizeof(uint32_t))

#include "log.h"
#define close(f) do {warning(logs("close: "), logi(f), logs(" file="), logs(__FILE__), logs(" line="), logi(__LINE__)); (close)(f);} while (0)

// WARNING: Current implementation limits clients to CLIENTS_LIMIT.

/*
The purpose of the event server is to deliver events to clients that subscribed for them.

A client subscribes by making an HTTP request to the dynamic API action "subscribe". As an argument to the action, the client sends UUID. By doing this the client requests to receives the events of the user owning the UUID.
When there is an event, the server will send it as a response to the "subscribe" request and the connection will be closed. If the client wishes to receive more events, it must make a new "subscribe" request. An event received while the client has no connection to the server will be delivered to the client when it connects. If the client does not connect for a given amount of time (currently SUBSCRIBE_TIMEOUT seconds), the client is considered to be gone and the events waiting for it will be discarded.

The event server provides the ability to send events from external sources. A client willing to send an event connects to the event protocol of the event server.

IMPLEMENTATION
The event server runs a thread designated for delivering events to subscribers (see main_subscribe()). The thread is started by subscribe_init().
Other threads call subscribe_connect() to make the thread deliver events to a given socket.
Events are sent by calling subscribe_message().
*/

// TODO: consider ring buffers. ring buffer can be optimized using mmap:
// http://en.wikipedia.org/wiki/Circular_buffer#Absolute_indices

/*#include <stdio.h>
#define epoll_ctl(...) _epoll_ctl(__VA_ARGS__)
int _epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
	char *str[] = {[EPOLL_CTL_ADD] = "add", [EPOLL_CTL_MOD] = "mod", [EPOLL_CTL_DEL] = "del"};
	fprintf(stderr, "epoll (%d): %s %d", epfd, str[op], fd);
	if ((epoll_ctl)(epfd, op, fd, event) < 0) fprintf(stderr, " error=%d", errno);
	fprintf(stderr, "\n");
	fflush(stderr);
}*/

struct message
{
	struct string text;
	struct message *_next;
	unsigned _links;
	unsigned char uuid[UUID_SIZE];
};

// TODO reduce memory usage of this?
struct subscriber
{
	// Subscriber UUID stored in binary.
	unsigned char uuid[UUID_SIZE];
	unsigned target;

	struct subscriber *next;
	struct queue_item **queue_node;

	struct message *messages;
	size_t start;

	int handle, control;

	// Time of last activity (state change). Used to determine when to close socket and when to unsibscribe.
	time_t active;

	bool ready;
};

static struct subscriber *subscribers[CLIENTS_LIMIT];
static struct queue connected, wait;

static int subscribtion[2];		// event subscribtion pipe
static int inbox[2];			// incoming messages pipe

static int epoll;

static struct message *restrict message_alloc(const struct string *text)
{
	uint16_t size = format_uint_length(text->length, 16);
	struct string before = string("\r\n"), after = string("\r\n0\r\n\r\n");

	size_t length = size + before.length + text->length + after.length;
	struct message *message = malloc(sizeof(struct message) + length + 1);
	if (!message) return 0; // memory error
	message->text = string((char *)(message + 1), length);

	size_t index;
	index = format_uint(message->text.data, text->length, 16, size) - message->text.data;
	memcpy(message->text.data + index, before.data, before.length); index += before.length;
	memcpy(message->text.data + index, text->data, text->length); index += text->length;
	memcpy(message->text.data + index, after.data, after.length + 1); // + 1 for \0
	message->_next = 0;
	message->_links = 0;

	return message;
}

// WARNING: no overflow protection
static void message_retain(struct message *restrict message)
{
	if (!message) return;
	message->_links += 1;
}

static void message_release(struct message *restrict message)
{
	if (!message) return;
	if (!--message->_links)
	{
		message_release(message->_next);
		free(message);
	}
}

static int subscribe_add(void)
{
	int subscribed = 0;
	struct subscriber **slot, *item;

	struct subscriber *subscriber;
	uint32_t client_id;
	char input[SUBSCRIPTION_REQUEST];

	int bytes;
	ioctl(subscribtion[0], FIONREAD, &bytes); // always successful when used properly

	while (bytes >= SUBSCRIPTION_REQUEST)
	{
		readall(subscribtion[0], input, sizeof(input));
		subscriber = *(struct subscriber **)input;
		client_id = *(uint32_t *)(input + sizeof(subscriber));
		bytes -= SUBSCRIPTION_REQUEST;

		// Add subscriber to the slot for the client_id it belongs to.
		slot = subscribers + client_id;
		while (1)
		{
			item = *slot;
			if (!item)
			{
				item = *slot = subscriber;
				break;
			}

			// Check whether this subscriber was connected before.
			if ((item->handle < 0) && !memcmp(subscriber->uuid, item->uuid, UUID_SIZE))
			{
				//printf("REPLACE %p %p\n", slot, *slot);
				if ((*item->queue_node)->next) // fix next item's double pointer since this item will be freed
				{
					struct subscriber *next = *(struct subscriber **)(*item->queue_node)->next->data;
					next->queue_node = item->queue_node;
				}
				queue_remove(&wait, item->queue_node);
				item->handle = subscriber->handle;
				free(subscriber);
				break;
			}

			slot = &item->next;
		}

		// Current subscriber is now accessible through item.
		item->queue_node = connected.end;
		item->start = 0;
		item->active = time(0);
		item->ready = false;
		queue_push(&connected, slot);

		//printf("STORE %p %p\n", slot, *slot);

		struct epoll_event event = {.events = EPOLLET | EPOLLOUT | EPOLLRDHUP, .data.ptr = slot};
		if (epoll_ctl(epoll, EPOLL_CTL_ADD, item->handle, &event) < 0) ; // TODO: error
		subscribed += 1;
	}

	return subscribed;
}

static int subscribe_wait(struct subscriber **slot)
{
	//printf("WAIT %p %p\n", slot, *slot);

	struct subscriber *subscriber = *slot;

	if ((*subscriber->queue_node)->next) // fix next item's double pointer since this item will be freed
	{
		struct subscriber *next = *(struct subscriber **)(*subscriber->queue_node)->next->data;
		next->queue_node = subscriber->queue_node;
	}
	queue_remove(&connected, subscriber->queue_node);

	if (epoll_ctl(epoll, EPOLL_CTL_DEL, subscriber->handle, 0) < 0) ; // TODO: error
	close(subscriber->handle);
	connection_release(subscriber->control, 0);

	subscriber->queue_node = wait.end;
	subscriber->handle = -1;
	subscriber->active = time(0);
	queue_push(&wait, slot);

	return -1;
}

// WARNING: This should be called only for subscribers in wait state.
static void subscribe_remove(struct subscriber **slot)
{
	struct subscriber *subscriber = *slot;
	if ((*subscriber->queue_node)->next) // fix next item's double pointer since this item will be freed
	{
		struct subscriber *next = *(struct subscriber **)(*subscriber->queue_node)->next->data;
		next->queue_node = subscriber->queue_node;
	}
	queue_remove(&wait, subscriber->queue_node);

	//printf("REMOVE %p %p\n", slot, *slot);

	// Each item utilizes pointer to a location in the item before it (via the wait double pointer).
	// So removing subscriber could break subscriber->next or epoll.
	if (subscriber->next)
	{
		struct queue_item *next = *subscriber->next->queue_node;
		next->data = slot;

		if (subscriber->next->handle >= 0)
		{
			struct epoll_event event = {.events = EPOLLET | EPOLLOUT | EPOLLRDHUP, .data.ptr = slot};
			if (epoll_ctl(epoll, EPOLL_CTL_MOD, subscriber->next->handle, &event) < 0) ; // TODO: error
		}
	}
	*slot = subscriber->next;

	// Remove unsent subscriber messages.
	message_release(subscriber->messages);

	free(subscriber);
}

static int subscribe_write(struct subscriber **slot)
{
	struct subscriber *subscriber = *slot;
	struct message *message = subscriber->messages;

	// If the subscriber target is a UUID, skip messages sent to other UUIDs.
	if ((subscriber->target == SUBSCRIBE_UUID) && !subscriber->start)
	{
		while (memcmp(message->uuid, subscriber->uuid, UUID_SIZE))
		{
			subscriber->messages = message->_next;
			message_retain(subscriber->messages);
			message_release(message);
			message = subscriber->messages;
			if (!message) return 0;
		}
	}

	ssize_t status;
	size_t length;
	while (1)
	{
		length = message->text.length - subscriber->start;
		status = write(subscriber->handle, message->text.data + subscriber->start, length);
		if (status < 0)
		{
			error(logs("subscribe_write errno="), logi(errno), logs(" fd="), logi(subscriber->handle));
			switch (errno)
			{
			case EAGAIN:
#if EAGAIN != EWOULDBLOCK
			case EWOULDBLOCK:
#endif
				subscriber->ready = false;
				return 0;
			case EINTR:
				break;
			default:
				// Connection problem. Keep the message so it can be sent if the client subscribes again.
				return subscribe_wait(slot);
			}
		}
		else if (status == length)
		{
			error(logs("subscribe_write finished fd="), logi(subscriber->handle));

			// Message sent. Terminate the connection.
			subscriber->messages = message->_next;
			message_retain(subscriber->messages);
			message_release(message);
			return subscribe_wait(slot);
		}
		else subscriber->start += status;
	}
}

static int subscribe_queue(void)
{
	int subscribed = 0;
	char input[INBOX_REQUEST_HEADER + sizeof(uint32_t) * INBOX_RECIPIENTS_LIMIT];

	readall(inbox[0], input, INBOX_REQUEST_HEADER);

	char *start = input;
	struct message *restrict message = *(struct message *restrict *)start;
	start += sizeof(struct message *);
	message_retain(message);

	size_t recipients_count = *(size_t *)start;
	start += sizeof(size_t);
	readall(inbox[0], start, sizeof(uint32_t) * recipients_count);

	struct subscriber **slot, *subscriber;
	struct message **messages;

	bool added;
	size_t index;
	uint32_t client_id;
	for(index = 0; index < recipients_count; ++index)
	{
		client_id = *(uint32_t *)start;
		start += sizeof(uint32_t);

		// Add the message to the queue of each subscriber for client_id.
		added = false;
		for(slot = subscribers + client_id; subscriber = *slot; slot = &subscriber->next)
		{
			if (!subscriber->messages) subscriber->messages = message;
			else if (!added)
			{
				// Find queue end and add the message there.
				for(messages = &subscriber->messages; *messages; messages = &(*messages)->_next)
					;
				*messages = message;
				added = true;
			}

			message_retain(message);

			// TODO maybe this is useful only if there were no messages for the subscriber
			if (subscriber->ready && (subscriber->handle >= 0)) subscribed += subscribe_write(slot);
		}
	}

	message_release(message);

	return subscribed;
}

// TODO: should I use persistent connections
// TODO: also listen for events here ?
static void *main_subscribe(void *arg)
{
	int ready;

	queue_init(&connected); // TODO: this can be optimized
	queue_init(&wait); // TODO: this can be optimized

	unsigned connections = 2;
	epoll = epoll_create1(0);
	if (epoll < 0) return 0; // TODO: error

	struct epoll_event event = {.events = EPOLLIN, .data.ptr = subscribtion}, response[EVENTS_MAX];
	if (epoll_ctl(epoll, EPOLL_CTL_ADD, subscribtion[0], &event) < 0) ; // TODO: error

	event = (struct epoll_event){.events = EPOLLIN, .data.ptr = inbox};
	if (epoll_ctl(epoll, EPOLL_CTL_ADD, inbox[0], &event) < 0) ; // TODO: error

	void *argument;
	time_t now;
	struct subscriber **slot;

	while (1)
	{
		//printf("connections=%d\n", connections);

		// TODO fix timeout
		//ready = epoll_wait(epoll, response, EVENTS_MAX, -1);
		ready = epoll_wait(epoll, response, EVENTS_MAX, 10);
		if (ready < 0) continue; // errno == EINTR
		while (ready--)
		{
			// Call handler for the current connection.
			argument = response[ready].data.ptr;
			if (argument == (void *)subscribtion) connections += subscribe_add(); // new subscriber
			else if (argument == (void *)inbox) connections += subscribe_queue(); // message received
			else
			{
				struct subscriber *subscriber = *(struct subscriber **)argument;
				if (subscriber->handle >= 0) // subscriber may be in wait state
				{
					if (response[ready].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) connections += subscribe_wait((struct subscriber **)argument);
					else if (subscriber->messages) connections += subscribe_write(argument);
					else subscriber->ready = true;
				}
			}
		}

		now = time(0);

		// Remove connections after a timeout.
		while (connected.length)
		{
			slot = connected.start->data;
			if ((now - (*slot)->active) < CONNECTION_TIMEOUT) break;
			subscribe_wait(slot);
		}

		// Remove disconnected subscribers.
		while (wait.length)
		{
			slot = wait.start->data;
			if ((now - (*slot)->active) < SUBSCRIBE_TIMEOUT) break;
			subscribe_remove(slot);
		}
	}
}

bool subscribe_init(void)
{
	// Test whether the necessary pipe writes are atomic on this system.
	// TODO: I'll need a pipe bigger than this for incoming messages
	if (PIPE_BUF < SUBSCRIPTION_REQUEST) return false;

	if (pipe(subscribtion) < 0) return false;
	if (pipe(inbox) < 0) return false;

	pthread_t thread_id;
	pthread_create(&thread_id, 0, &main_subscribe, 0);
	pthread_detach(thread_id);

	return true;
}

void subscribe_term(void)
{
	// TODO: queue_term()
}

bool subscribe_connect(const struct string *uuid, int fd, int control, unsigned target)
{
	struct subscriber *subscriber;
	uint32_t client_id;
	char output[SUBSCRIPTION_REQUEST];

	// Initialize subscriber.
	subscriber = malloc(sizeof(*subscriber));
	if (!subscriber) return false; // memory error
	hex2bin(subscriber->uuid, uuid->data, uuid->length);
	subscriber->target = target;
	subscriber->next = 0;
	// subscriber->queue_node is set in subscribe_add()
	subscriber->messages = 0;
	// subscriber->start is set in subscribe_add()
	subscriber->handle = fd;
	subscriber->control = control;
	// subscriber->ready is set in subscribe_add()

	// Extract client_id from the passed UUID.
	uuid_extract(subscriber->uuid, &client_id, 0);
	if (client_id >= CLIENTS_LIMIT)
	{
		free(subscriber);
		return false; // invalid client_id
	}

	// Make socket non-blocking.
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

	// Send connect event through pipe.
	*(struct subscriber **)output = subscriber;
	*(uint32_t *)(output + sizeof(subscriber)) = client_id;
	writeall(subscribtion[1], output, sizeof(output));
	return true;
}

bool subscribe_message(const struct string *text, const struct vector *to)
{
	struct message *restrict message = message_alloc(text);
	if (!message) return false; // memory error

	char output[INBOX_REQUEST_HEADER + sizeof(uint32_t) * INBOX_RECIPIENTS_LIMIT];
	char *start = output;

	*(struct message **)start = message;
	start += sizeof(struct message *);

	*(size_t *)start = to->length;
	start += sizeof(size_t);

	size_t index;
	uint32_t client_id;
	for(index = 0; index < to->length; ++index) // to->length is always 1
	{
		hex2bin(message->uuid, ((struct string *)vector_get(to, index))->data, UUID_LENGTH);
		uuid_extract(message->uuid, &client_id, 0);
		if (client_id >= CLIENTS_LIMIT) return false; // invalid client_id
		*(uint32_t *)start = client_id;
		start += sizeof(uint32_t);
	}

	writeall(inbox[1], output, INBOX_REQUEST_HEADER + sizeof(uint32_t) * to->length);
	return true;
}
