#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef OS_BSD
# include <arpa/inet.h>
# include <poll.h>
# if !defined(OS_ANDROID)
#  include <execinfo.h>
# endif
#else
# define WINVER 0x0501
# include <windows.h>
# include <winsock2.h>
# include <ws2tcpip.h>
# include "mingw.h"
# undef close
# define close CLOSE
#endif

#ifdef OS_ANDROID
# include <netinet/in.h>
#endif

#include "types.h"
#include "format.h"
#include "log.h"
#include "stream.h"
#include "protocol.h"
#include "security.h"
#include "json.h"
#include "http.h"
#include "http_parse.h"
#include "http_response.h"
#include "cache.h"
#include "server.h"
#include "storage.h"
#include "dlna/ssdp.h"

#include "status.h" /* TODO deprecated */

#if defined(FILEMENT_UPNP)
# include "miniupnpc_filement.h"
#endif

#ifdef FTP
# include "ftp/auth.h"
#endif

#define LISTEN_MAX 10

#define STATUS_BUFFER 64

#if defined(DEVICE)
# include "device/proxy.h"
# define DLNA_HEADER " DLNADOC/1.50 UPnP/1.0"
#endif

struct string SERVER;

#define IP4(o0, o1, o2, o3) htonl(((o0) << 24) | ((o1) << 16) | ((o2) << 8) | (o3))

#if defined(OS_WINDOWS)
# include "../windows/server.c"
#endif

#ifdef OS_ANDROID
int g_server_socket = -1;
int g_proxy_socket = -1;
int g_proxy_tid = 0;
int g_server_tid = 0;

void g_kill_server_thrd(int sig)
{
if(g_server_socket>=0){close(g_server_socket);g_server_socket=-1;}
g_server_tid=0;
pthread_exit(0);
}
#endif

#define close(f) do {warning(logs("close: "), logi(f), logs(" line="), logi(__LINE__)); (close)(f);} while (0)

// http://www.greenend.org.uk/rjk/tech/poll.html

struct connection
{
	enum {Listen = 1, Proxy, Parse, ResponseStatic, ResponseDynamic} type;
	struct http_context context;
	struct resources resources;
	int control;
	time_t activity;
};

static const struct string key_connection = {"Connection", 10}, value_close = {"close", 5};

// Checks whether a given address refers to the localhost.
bool address_local_host(const struct sockaddr_storage *restrict address)
{
	if (address->ss_family == AF_INET6)
	{
		const char *ip = ((struct sockaddr_in6 *)address)->sin6_addr.s6_addr;
		const char localhost4[][16] = {
			{0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 127, 0, 0, 0},
			{0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 127, 255, 255, 255}
		};
		const char localhost6[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
		return (!memcmp(ip, localhost6, 16) || ((memcmp(localhost4[0], ip, 16) <= 0) && (memcmp(ip, localhost4[1], 16) <= 0)));
	}
	else if (address->ss_family == AF_INET)
	{
		const unsigned long localhost[] = {IP4(127, 0, 0, 0), IP4(127, 255, 255, 255)};
		unsigned long ip = ((struct sockaddr_in *)address)->sin_addr.s_addr;
		return ((localhost[0] <= ip) && (ip <= localhost[1]));
	}
	else return false;
}

// Checks whether a given address is located in a local network.
bool address_local_network(const struct sockaddr_storage *restrict address)
{
	if (address_local_host(address)) return true;
	if (address->ss_family == AF_INET6)
	{
		const char *ip = ((struct sockaddr_in6 *)address)->sin6_addr.s6_addr;
		const char local[][16] = {
			{0xfc, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
			{0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}
		};
		const char link_local[][16] = {
			{0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
			{0xfe, 0xbf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}
		};
		return (((memcmp(local[0], ip, 16) <= 0) && (memcmp(ip, local[1], 16) <= 0)) || ((memcmp(link_local[0], ip, 16) <= 0) && (memcmp(ip, link_local[1], 16) <= 0)));
	}
	else if (address->ss_family == AF_INET)
	{
		const unsigned long a[] = {IP4(10, 0, 0, 0), IP4(10, 255, 255, 255)};
		const unsigned long b[] = {IP4(172, 16, 0, 0), IP4(172, 31, 255, 255)};
		const unsigned long c[] = {IP4(192, 168, 0, 0), IP4(192, 168, 255, 255)};
		const unsigned long link_local[] = {IP4(169, 254, 1, 0), IP4(169, 254, 254, 255)};
		unsigned long ip = ((struct sockaddr_in *)address)->sin_addr.s_addr;

		return (
			((a[0] <= ip) && (ip <= a[1])) ||
			((b[0] <= ip) && (ip <= b[1])) ||
			((c[0] <= ip) && (ip <= c[1])) ||
			((link_local[0] <= ip) && (ip <= link_local[1]))
		);
	}
	else return false;
}

#if defined(FILEMENT_UPNP)
void *pthread_upnp_forward_port(void *arg)
{
	int port = *(int *)arg;
	filement_set_upnp_forwarding(port);
}
#endif

// Releases control of the connection-related resources.
// Uses a pipe to tell the dispatcher what to do with the socket.
void connection_release(int control, int status)
{
	if (control >= 0)
	{
		write(control, &status, sizeof(status));
		close(control);
	}
}

static void response_init(struct http_response *restrict response)
{
	response->headers_end = response->headers;
	response->content_encoding = -1;
	response->ranges = 0;
}

static void response_term(struct http_response *restrict response)
{
	if (response->content_encoding < 0) return; // no response initialized
	response->content_encoding = -1;
	free(response->ranges);
}

static void *server_serve(void *argument)
{
	struct connection *connection = argument;

	struct http_request *request = &connection->context.request;
	struct http_response response;
	struct string key, value;
	int status;
	bool last;

	// Remember to terminate the connection if the client specified so.
	{
		struct string *connection = dict_get(&request->headers, &key_connection);
		last = (connection && string_equal(connection, &value_close));
	}

	response_init(&response);

	// Assume that response_header_add() will always succeed before the response handler is called.

	// Server - Server name and version.
	key = string("Server");
	response_header_add(&response, &key, &SERVER);

	// Allow cross-origin requests.
	key = string("origin");
	if (dict_get(&request->headers, &key))
	{
		// TODO: maybe allow only some domains as origin. is origin always in the same format as allow-origin ?
		key = string("Access-Control-Allow-Origin");
		value = string("*");
		response_header_add(&response, &key, &value);

		// TODO is this okay
		key = string("Access-Control-Expose-Headers");
		value = string("Server, UUID");
		response_header_add(&response, &key, &value);
	}

	// UUID - Unique identifier of the device.
#if defined(DEVICE) || defined(PUBCLOUD)
# if !defined(OS_WINDOWS)
	extern struct string UUID;
	key = string("UUID");
	response_header_add(&response, &key, &UUID);
# endif
#endif

	// TODO: change this to do stuff properly
	if (request->method == METHOD_OPTIONS)
	{
		// TODO: Access-Control-Request-Headers
		key = string("Access-Control-Allow-Headers");
		value = string("Cache-Control, X-Requested-With, Filename, Filesize, Content-Type, Content-Length, Authorization, Range");
		response_header_add(&response, &key, &value);

		// TODO is this okay
		key = string("Access-Control-Expose-Headers");
		value = string("Server, UUID");
		response_header_add(&response, &key, &value);

		// TODO: Access-Control-Request-Method
		key = string("Access-Control-Allow-Methods");
		value = string("GET, POST, OPTIONS, PUT, DELETE, SUBSCRIBE, NOTIFY");
		response_header_add(&response, &key, &value);

		status = 0;
		response.code = OK;
	}
	else
	{
		// Parse request URI and call appropriate handler.
		if (response.code = http_parse_uri(request)) goto finally;
		else
		{
			// If there is a query, generate dynamic content and send it. Otherwise send static content.
			response.code = InternalServerError; // default response code
			if (request->query)
			{
#if defined(DISTRIBUTE)
				connection->resources.control = connection->control;
#endif
				status = handler_dynamic(request, &response, &connection->resources);
				json_free(request->query);
			}
			else status = handler_static(request, &response, &connection->resources);
			free(request->path.data);

			// Close the connection on error with a request containing body.
			// TODO is this okay?
			if (status && ((request->method == METHOD_POST) || (request->method == METHOD_PUT)))
			{
				if (!response_header_add(&response, &key_connection, &value_close)) goto error;
				last = true;
			}

			// TODO this is a fix for old actions that return HTTP status codes
			if (status > 0)
			{
				response.code = status;
				goto finally;
			}

			switch (status)
			{
			case ERROR_CANCEL:
				if (!response_header_add(&response, &key_connection, &value_close)) goto error;
				last = true;
			case ERROR_PROGRESS:
				response.code = OK;
			case 0:
				break;

			case ERROR_ACCESS:
			case ERROR_SESSION:
				response.code = Forbidden;
				break;

			case ERROR_INPUT:
			case ERROR_EXIST:
			case ERROR_MISSING:
			case ERROR_READ:
			case ERROR_WRITE:
			case ERROR_RESOLVE:
				response.code = NotFound;
				break;

			case ERROR_EVFS:
			default:
				response.code = InternalServerError;
				break;

			case ERROR_UNSUPPORTED:
				response.code = NotImplemented;
				break;
			
			case ERROR_AGAIN:
				response.code = ServiceUnavailable;
				break;

			case ERROR_GATEWAY:
				if (!response_header_add(&response, &key_connection, &value_close)) goto error;
				last = true;
				response.code = BadGateway;
				break;

			case ERROR_MEMORY:
				response.code = ServiceUnavailable;
				// TODO send response
			case ERROR_NETWORK:
				goto error; // not possible to send response
			}
		}
	}

finally:

	if (status == ERROR_PROGRESS)
	{
		response_term(&response);
		return 0;
	}

	// Send default response if specified but only if none is sent until now.
	if (response.content_encoding < 0)
	{
#define S(s) s, sizeof(s) - 1
		char json[STATUS_BUFFER], *end = json;
		end = format_bytes(end, S("{\"error\":"));
		end = format_int(end, status);
		*end++ = '}';

		last |= !response_headers_send(&connection->resources.stream, request, &response, end - json);
		last |= response_entity_send(&connection->resources.stream, &response, json, end - json);
#undef S
	}

	if (last) status = 1;

	if (0)
	{
error:
		status = -1;
	}

	response_term(&response);
	connection_release(connection->control, status); // TODO should this be in a function?

	return 0;
}

#if !defined(OS_WINDOWS)
// Run the process as a daemon.
void server_daemon(void)
{
	#define ERROR_DAEMON "Unable to run as daemon"

#if RUN_MODE > Debug
	int temp = fork();
	if (temp < 0) fail(2, ERROR_DAEMON);
	if (temp) _exit(0); // stop parent process

	if (setsid() < 0) fail(2, ERROR_DAEMON);
#endif

	if (chdir("/") < 0) fail(2, ERROR_DAEMON);

	umask(0);

	// TODO: this is not the best solution but it works for most cases
	// TODO: check for errors?
	//close(0);
	//close(1);
	//close(2);

	// TODO: check for errors?
	//open("/dev/null", O_RDONLY);
	//open("/dev/null", O_WRONLY);
	//dup(1);

	#undef ERROR_DAEMON
}
#endif

void segfault(int number)
{
#if !defined(OS_ANDROID)
	void *array[16];

	size_t frames = backtrace(array, (sizeof(array) / sizeof(*array)));
	//error(logi(frames));

	//char **syms = backtrace_symbols(array, frames);
	backtrace_symbols_fd(array, frames, 2);

	/*size_t index = 0;
	for(index = 0; index < frames; ++index)
	{
		write(2, syms[index], strlen(syms[index]));
		write(2, "\n", 1);
	}*/
#endif

	_exit(ERROR_MEMORY);
}

void server_init(void)
{
#if !defined(OS_WINDOWS)
	struct sigaction action = {
		.sa_handler = SIG_IGN,
		.sa_mask = 0,
		.sa_flags = 0
	};
	sigaction(SIGPIPE, &action, 0); // never fails when used properly
	action.sa_handler = segfault;
	sigaction(SIGSEGV, &action, 0);
#endif

	// Prepare server for secure operations.
	security_init();

	if (!cache_init()) fail(2, "Unable to initialize sessions");
	status_init(); // TODO deprecated

#ifdef FTP
	if (!auth_init()) fail(2, "Unable to initialize auth");
#endif

#if defined(FILEMENT_TLS)
	if (tls_init() < 0) fail(2, "SSL initialization error");
#endif

	// Initialize server identification.

	extern const struct string app_name;
	extern const struct string app_version;

	//bool DLNA = DLNAisEnabled();

	SERVER.length = app_name.length + 1 + app_version.length;
#if defined(DEVICE)
	/*if (DLNA)*/
	SERVER.length += sizeof(DLNA_HEADER) - 1;
#endif
	SERVER.data = malloc(sizeof(char) * (SERVER.length + 1));
	if (!SERVER.data) fail(1, "Memory error");

	memcpy(SERVER.data, app_name.data, app_name.length);
	SERVER.data[app_name.length] = '/';
	memcpy(SERVER.data + app_name.length + 1, app_version.data, app_version.length);

#if defined(DEVICE)
	// If DLNA is enabled, add DLNA information in the Server header.
	/*if (DLNA) */
	memcpy(SERVER.data + app_name.length + 1 + app_version.length, DLNA_HEADER, sizeof(DLNA_HEADER) - 1);
#endif

	SERVER.data[SERVER.length] = 0;
}

// Listen for incoming HTTP connections.
void server_listen(void *storage)
{
	size_t connections_count = 0, connections_size;
	struct pollfd *wait;
	struct connection **connections, *connection;
#if defined(DEVICE)
	struct connection_proxy proxy;
#endif

	size_t i;
	struct sockaddr_in address;
	socklen_t address_len;
	pthread_t thread_id;

	// Allocate memory for connection data.
	connections_size = 8; // TODO change this
	wait = malloc(connections_size * sizeof(*wait));
	connections = malloc(connections_size * sizeof(*connections));
	if (!wait || !connections)
	{
		error(logs("Unable to allocate memory"));
		goto error;
	}

	// Create listening sockets.
	for(i = 0; i < PORT_DIFF; ++i)
	{
		int value = 1;

#if !defined(OS_WINDOWS)
		wait[i].fd = socket(PF_INET, SOCK_STREAM, 0);
#else
		wait[i].fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
#endif
		if (wait[i].fd < 0)
		{
			error(logs("Unable to create socket"));
			goto error;
		}
		wait[i].events = POLLIN;
		wait[i].revents = 0;

		// disable TCP time_wait
		// TODO should I do this?
		setsockopt(wait[i].fd, SOL_SOCKET, SO_REUSEADDR, (void *)&value, sizeof(value)); // TODO can this fail

		address.sin_family = AF_INET;
		address.sin_addr.s_addr = INADDR_ANY;
		address.sin_port = htons(PORT_HTTP_MIN + i);
		if (bind(wait[i].fd, (struct sockaddr *)&address, sizeof(address)))
		{
			error(logs("Unable to bind to port "), logi(PORT_HTTP_MIN + i));
			goto error;
		}
		if (listen(wait[i].fd, LISTEN_MAX))
		{
			error(logs("Listen error"));
			goto error;
		}

		connections[i] = malloc(sizeof(**connections));
		if (!connections[i])
		{
			error(logs("Unable to allocate memory"));
			goto error;
		}

		connections[i]->type = Listen;
		// TODO set other fields
	}

#if defined(DEVICE)
	// TODO merge proxy listen code with this code
	extern int pipe_proxy[2];
	wait[PORT_DIFF].fd = pipe_proxy[0];
	wait[PORT_DIFF].events = POLLIN;
	wait[PORT_DIFF].revents = 0;
	connections[PORT_DIFF] = malloc(sizeof(**connections));
	if (!connections[PORT_DIFF])
	{
		error(logs("Unable to allocate memory"));
		goto error;
	}
	connections[PORT_DIFF]->type = Proxy;
	connections_count = PORT_DIFF + 1;
#else
	connections_count = PORT_DIFF;
#endif

#if defined(FILEMENT_UPNP)
	// Start a thread to notify routers for port forwarding.
	// TODO is this okay?
	int listening_port[PORT_DIFF];
	for(i = 0; i < PORT_DIFF; ++i) listening_port[i] = PORT_HTTP_MIN + i;
	pthread_create(&thread_id, 0, pthread_upnp_forward_port, &listening_port[i]);
	pthread_detach(thread_id);
#endif

	int client;
	int status;
	size_t poll_count;
	time_t now;

	// TODO connections elements may be changed by another thread; think about this (are there caching problems?)
	// http://stackoverflow.com/questions/26097773/non-simultaneous-memory-use-from-multiple-threads-caching

	// TODO add one more listening socket for https

	// Start an event loop to handle the connections.
	while (1)
	{
		if (poll(wait, connections_count, -1) < 0) continue; // TODO set timeout

		now = time(0);

		poll_count = connections_count;
		for(i = 0; i < poll_count; ++i)
		{
			if (wait[i].revents & POLLIN)
			{
				wait[i].revents = 0;

				switch (connections[i]->type)
				{
				case Listen:
					// A client has connected to the server. Accept the connection and prepare for parsing.

					// Make sure there is enough allocated memory to store connection data.
					if (connections_count == connections_size)
					{
						void *p;

						connections_size *= 2;

						p = realloc(wait, connections_size * sizeof(*wait));
						if (!p)
						{
							error(logs("Unable to allocate memory"));
							goto error;
						}
						wait = p;

						p = realloc(connections, connections_size * sizeof(*connections));
						if (!p)
						{
							error(logs("Unable to allocate memory"));
							goto error;
						}
						connections = p;
					}

					connections[connections_count] = malloc(sizeof(**connections));
					if (!connections[connections_count])
					{
						error(logs("Unable to allocate memory"));
						goto error;
					}
					connection = connections[connections_count];
					memset(&connection->resources, 0, sizeof(connection->resources));

					address_len = sizeof(connection->resources.address);
					if ((client = accept(wait[i].fd, (struct sockaddr *)&connection->resources.address, &address_len)) < 0)
					{
						warning(logs("Unable to accept (errno="), logi(errno), logs(")"));
						continue;
					}
					http_open(client);
					if (stream_init(&connection->resources.stream, client))
					{
						warning(logs("Unable to initialize stream"));
						http_close(client);
						continue;
					}
					warning(logs("accepted (http): "), logi(client)); // TODO remove this
					connection->type = Parse;
					connection->activity = now;
					http_parse_init(&connection->context); // TODO error check

					wait[connections_count].fd = client;
					wait[connections_count].events = POLLIN;
					wait[connections_count].revents = 0;

					connections_count += 1;
					break;

#if defined(DEVICE)
				case Proxy:
					read(wait[i].fd, (void *)&proxy, sizeof(proxy));

					// Make sure there is enough allocated memory to store connection data.
					if (connections_count == connections_size)
					{
						void *p;

						connections_size *= 2;

						p = realloc(wait, connections_size * sizeof(*wait));
						if (!p)
						{
							error(logs("Unable to allocate memory"));
							goto error;
						}
						wait = p;

						p = realloc(connections, connections_size * sizeof(*connections));
						if (!p)
						{
							error(logs("Unable to allocate memory"));
							goto error;
						}
						connections = p;
					}

					connections[connections_count] = malloc(sizeof(**connections));
					if (!connections[connections_count])
					{
						error(logs("Unable to allocate memory"));
						goto error;
					}
					connection = connections[connections_count];
					memset(&connection->resources, 0, sizeof(connection->resources));
					connection->resources.address = proxy.address;

					http_open(proxy.client);

					if (proxy.protocol == PROXY_HTTPS)
					{
# if defined(FILEMENT_TLS)
						status = stream_init_tls_connect(&connection->resources.stream, proxy.client, 0); // TODO set third argument
# else
						warning(logs("Unable to handle incoming HTTPS request because TLS is not supported"));
						status = -1;
# endif
					}
					else status = stream_init(&connection->resources.stream, proxy.client);
					if (status)
					{
						warning(logs("Unable to initialize stream"));
						http_close(proxy.client);
						continue;
					}
					connection->type = Parse;
					connection->activity = now;
					http_parse_init(&connection->context); // TODO error check

					wait[connections_count].fd = proxy.client;
					wait[connections_count].events = POLLIN;
					wait[connections_count].revents = 0;

					connections_count += 1;
					break;
#endif

				case Parse:
					connection = connections[i];

					// Request data received. Try parsing it.

					status = http_parse(&connection->context, &connection->resources.stream);
					if (!status)
					{
						// Request parsed successfully.

						// Check if host header is specified.
						struct string name = string("host");
						connection->context.request.hostname = dict_get(&connection->context.request.headers, &name);
						if (!connection->context.request.hostname)
						{
							// TODO send BadRequest
							status = BadRequest;
							warning(logs("goto term "), logi(__LINE__));
							goto term;
						}

						// Use a separate thread to handle the request and send response.
						// Create a pipe for communication between the two threads.

						connection->resources.storage = storage;

						// Create a pipe that will be polled to determine the response status.
						int control[2];
						if (pipe(control))
						{
							error(logs("Unable to create pipe"));
							status = ERROR_MEMORY;
							goto term;
						}
						warning(logs("pipe (Parse): "), logi(control[0]), logs(" "), logi(control[1])); // TODO remove this
						wait[i].fd = control[0];
						connection->control = control[1];

						connection->type = ResponseDynamic;
						connection->activity = now;

						// TODO determine whether the request is static or dynamic
						// TODO handle static requests separately

						pthread_create(&thread_id, 0, &server_serve, connection);
						pthread_detach(thread_id);
					}
					else if (status != ERROR_AGAIN) {warning(logs("goto term "), logi(__LINE__)); goto term;}
					else connection->activity = now;
					break;

				/*case ResponseStatic:
					break;*/

				case ResponseDynamic:
					connection = connections[i];

					read(wait[i].fd, &status, sizeof(status));
					// assert(read() returned sizeof(status));

					close(wait[i].fd);
					wait[i].fd = connection->resources.stream.fd;

					if (status) {warning(logs("goto term "), logi(__LINE__)); goto term;}
					else
					{
						connection->type = Parse;
						connection->activity = now;
					}

					http_parse_term(&connection->context); // TODO race condition here?
					http_parse_init(&connection->context); // TODO error check

					break;
				}
			}
			else if (wait[i].revents)
			{
				if (connections[i]->type == ResponseDynamic)
				{
					close(wait[i].fd);
					wait[i].fd = connections[i]->resources.stream.fd;
				}
				// else assert(connections[i]->type == Parse);

				status = -1;
				warning(logs("goto term type="), logi(connections[i]->type), logs(" fd="), logi(wait[i].fd), logs(" "), logi(__LINE__));
				goto term; // TODO race condition here?
			}
			else if ((connections[i]->type == Parse) && ((now - connections[i]->activity) > (TIMEOUT / 1000)))
			{
				status = ERROR_AGAIN;
				warning(logs("goto term "), logi(__LINE__));
				goto term;
			}

			continue;

term:
			http_parse_term(&connections[i]->context);
			stream_term(&connections[i]->resources.stream);
			if (status >= 0) http_close(connections[i]->resources.stream.fd);
			else close(connections[i]->resources.stream.fd); // close with RST
			free(connections[i]);

			// Fill the entry freed by the terminated connection.
			// Make sure the moved entry is inspected (if included in poll_count).
			if (i != --connections_count)
			{
				wait[i] = wait[connections_count];
				connections[i] = connections[connections_count];
			}
			if (connections_count < poll_count)
			{
				poll_count -= 1;
				i -= 1;
			}

			// Shrink the memory allocated for connection data when appropriate to save memory.
			/*if (((connections_count * 4) <= connections_size) && (8 < connections_size)) // TODO remove this 8
			{
				connections_size /= 2;
				wait = realloc(wait, connections_size * sizeof(*wait));
				connections = realloc(connections, connections_size * sizeof(*connections));
			}*/
		}
	}

error:
	free(wait);
	for(i = 0; i < connections_count; ++i)
		free(connections[i]);
	free(connections);
}

void server_term(void)
{
	// TODO: this should wait for all threads to exit (if there are threads)

	free(SERVER.data);

#if defined(FILEMENT_TLS)
	tls_term();
#endif

	// TODO ?auth_term()

	cache_term();

	security_term();
}
