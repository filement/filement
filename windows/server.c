int g_listening_port[PORT_DIFF];
void *pthread_upnp_forward_port(void *arg);
// TODO: send static response on memory error
void *server_serve_windows(void *arg)
{
	struct resources *resources = (struct resources *)arg;

	struct http_request request;
	struct http_response response;
	struct string key, value;
	int status;
	bool last;
	struct string *connection;

#if defined(OS_WINDOWS)
	int oldtype;
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);
#endif

	// TODO outdated
	// The connection should be closed if one of the following occurs:
	//  protocol parse error
	//  insufficient memory
	//  client specifies "connection: close"
	//  action handler closed the connection

	while (true)
	{
		request = (struct http_request){.method = 0};

		// Initialize response.
		if (!response_init(&response))
		{
			stream_term(&resources->stream);
			http_close(resources->stream.fd);
			free(resources);
			return 0; // memory error
		}

		// Server - Server name and version.
		key = string("Server");
		if (!header_add(&response.headers, &key, &SERVER)) goto finally; // memory error

		// Parse request. Parsing allocates memory for URI and request headers.
		if (status = http_parse_windows(&request, &resources->stream))
		{
			if (status < 0) goto finally; // protocol parse error; terminate the connection
			/*{
				if (errno == ETIMEDOUT) response.code = RequestTimeout;
				else goto finally;
			}*/
			else if (status == InternalServerError) goto finally;
			else response.code = status;
			break;
		}

		// Allow cross-origin requests.
		key = string("origin");
		if (dict_get(&request.headers, &key))
		{
			// TODO: maybe allow only some domains as origin. is origin always in the same format as allow-origin ?
			key = string("Access-Control-Allow-Origin");
			value = string("*");
			if (!header_add(&response.headers, &key, &value)) goto finally; // memory error

			// TODO is this okay
			key = string("Access-Control-Expose-Headers");
			value = string("Server, UUID");
			if (!header_add(&response.headers, &key, &value)) goto finally; // memory error
		}

#if defined(DEVICE)
# if !defined(OS_WINDOWS)
		extern struct string UUID;
# else
		extern struct string UUID_WINDOWS;
		struct string UUID = UUID_WINDOWS;
# endif
		key = string("UUID");
		if (!header_add(&response.headers, &key, &UUID)) goto finally; // memory error
#endif

		// Terminate the connection if the client specified so.
		key = string("connection");
		value = string("close");
		connection = dict_get(&request.headers, &key);
		last = (connection && string_equal(connection, &value));

		// TODO close the connection on error on POST request (because there will usually be unread content that will otherwise cause parse error)

		// TODO: change this to do stuff properly
		if (request.method == METHOD_OPTIONS)
		{
			// TODO: Access-Control-Request-Headers
			key = string("Access-Control-Allow-Headers");
			value = string("Cache-Control, X-Requested-With, Filename, Filesize, Content-Type, Content-Length, Authorization, Range");
			if (!header_add(&response.headers, &key, &value)) goto finally; // memory error

			// TODO is this okay
			key = string("Access-Control-Expose-Headers");
			value = string("Server, UUID");
			if (!header_add(&response.headers, &key, &value)) goto finally; // memory error

			// TODO: Access-Control-Request-Method
			key = string("Access-Control-Allow-Methods");
			value = string("GET, POST, OPTIONS, PUT, DELETE, SUBSCRIBE, NOTIFY");
			if (!header_add(&response.headers, &key, &value)) goto finally; // memory error

			response.code = OK;
		}
		else
		{
			// Parse request URI and call appropriate handler.
			if (response.code = http_parse_uri(&request))
			{
				if (response.code == InternalServerError) goto finally; // no reliable way to send error response
				else break; // send error response
			}
			else
			{
				// If there is a query, generate dynamic content and send it. Otherwise send static content.
				// Call request handler to send response back to the client.
				response.code = InternalServerError; // default response code
				if (request.query)
				{
					status = handler_dynamic(&request, &response, resources);
					json_free(request.query);
				}
				else status = handler_static(&request, &response, resources);

				free(request.path.data);

				if (status > 0) response.code = status; // TODO this is a fix for old actions that return HTTP status codes
				else
				{
					switch (status)
					{
					case ERROR_CANCEL:
						key = string("Connection");
						value = string("close");
						if (!header_add(&response.headers, &key, &value)) goto last; // TODO static response?
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
						key = string("Connection");
						value = string("close");
						if (!header_add(&response.headers, &key, &value)) goto last; // TODO static response?
						last = true;
						response.code = BadGateway;
						break;

					case ERROR_MEMORY:
						response.code = ServiceUnavailable;
						// TODO send response
					case ERROR_NETWORK:
						goto last;
					}
				}
			}
		}

		// Send default response if specified but only if none is sent until now.
		if (response.content_encoding < 0)
			last |= !response_header(&resources->stream, &request, &response, 0);

		if (last) goto last;

		response_term(&response);

		free(request.URI.data);
		dict_term(&request.headers);
	}

	// Terminate the connection. Send response to indicate the reason for termination.
	key = string("Connection");
	value = string("close");
	if (header_add(&response.headers, &key, &value)) response_header(&resources->stream, &request, &response, 0);

finally:
	if (request.URI.data)
	{
last:
		free(request.URI.data);
		dict_term(&request.headers);
	}
	response_term(&response);

	stream_term(&resources->stream);
	http_close(resources->stream.fd);
	free(resources);
	return 0;
}

// Listen for incoming HTTP connections.
void server_listen_windows(void *storage)
{
	struct sockaddr_in address[PORT_DIFF];
	int client_socket;
	socklen_t address_len;
	struct pollfd pfd[PORT_DIFF]; 
	int i;

	// Create listening sockets.
	for(i = 0; i < PORT_DIFF; ++i)
	{
#ifdef OS_BSD
		pfd[i].fd = socket(PF_INET, SOCK_STREAM, 0);
#else
		pfd[i].fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
#endif
		if (pfd[i].fd < 0) fail(1, "Socket can not be initialized");

		// Disable TCP time_wait
		int optval = 1;
		if (setsockopt(pfd[i].fd, SOL_SOCKET, SO_REUSEADDR, (void *)&optval, sizeof(optval)) < 0)
			fail(2, "Socket can not be initialized");

		// Connect to one of the HTTP ports.
		int port = PORT_HTTP_MIN + i;
		address[i].sin_family = AF_INET;
		address[i].sin_addr.s_addr = INADDR_ANY;
		address[i].sin_port = htons(port);
		if (bind(pfd[i].fd, (struct sockaddr *)&address[i], sizeof(address[i])))
		{
			g_listening_port[i] = 0;
			continue;
		}
		g_listening_port[i] = port;
	   
		pfd[i].events = POLLIN;
		pfd[i].revents = 0;

		if (listen(pfd[i].fd, LISTEN_MAX) < 0) fail(4, "Listen error");
	}

	//for(i = 0; i < PORT_DIFF && !g_listening_port[i]; i++)
	//	;
	//if (i == PORT_DIFF) fail(3, "Bind error");
	
#if defined(DEVICE) && !defined(OS_IOS)
	// Start a thread to notify routers for port forwarding.
	pthread_t upnp_forwarding_thread;
	pthread_create(&upnp_forwarding_thread, NULL, pthread_upnp_forward_port, &g_listening_port[i]);
#endif

	pthread_t thread_id;
	struct resources *info = 0;

	// Wait for clients to connect
	while (true)
	{
		if (poll(pfd, PORT_DIFF, -1) < 0) continue;

		for(i = 0; i < PORT_DIFF; i++)
		{
			if (pfd[i].revents & POLLIN)
			{
				if (!info)
				{
					info = malloc(sizeof(struct resources));
					if (!info) fail(1, "Memory error");
					memset(info, 0, sizeof(struct resources));
				}

				// Establish a connection with a client
				address_len = sizeof(info->address);
				if ((client_socket = accept(pfd[i].fd, (struct sockaddr *)&info->address, &address_len)) < 0)
				{
					warning_("Unable to accept");
					continue;
				}
				if (stream_init(&info->stream, client_socket))
				{
					close(client_socket);
					warning_("Unable to initialize stream");
					continue;
				}

				http_open(client_socket);

				info->storage = storage;
				
				pthread_create(&thread_id, 0, &server_serve_windows, info);
				pthread_detach(thread_id);
				
				info = 0;
				pfd[i].revents = 0;
			}
		}
	}
}
