void *main_proxy_windows(void *storage)
{
	int bytes;
	unsigned wait = PROXY_WAIT_MIN;

	int fd;
	int protocol;
	struct host *proxies = 0;
	size_t proxy, count;

#if defined(OS_WINDOWS)
	unsigned long iMode = 0;
	int iResult = 0;
#endif

	struct resources *info = 0;
	pthread_t thread;

	// TODO design thread cancellation better

	// Connect to a discovered proxy server. Wait for a client.
	// On client request, invoke a thread to handle it and use the current thread to start a new connection to wait for more clients.
	while (true)
	{
		// Make sure proxy list is initialized.
		if (!proxies)
		{
			count = PROXIES_COUNT;
			proxies = proxy_discover(&count);
			if (!proxies)
			{
				warning_("Unable to discover proxies.");
				goto error;
			}
			proxy = 0;
		}

		while (true)
		{
			// Connect the device to a proxy.
			fd = proxy_connect(proxies + proxy);
			if (fd < 0)
			{
				warning(logs("Unable to connect to "), logs(proxies[proxy].name, strlen(proxies[proxy].name)), logs(":"), logi(proxies[proxy].port));
				if (++proxy < count) continue;
				else
				{
					// All proxies have failed. Wait a while and get new proxy list.
					warning_("All proxies failed.");
					free(proxies);
					proxies = 0;
					break;
				}
			}

			debug(logs("Connected to "), logs(proxies[proxy].name, strlen(proxies[proxy].name)), logs(":"), logi(proxies[proxy].port));
			wait = PROXY_WAIT_MIN;

			proxy = 0;

			if (!info)
			{
				info = malloc(sizeof(struct resources));
				if (!info) fail(1, "Memory error");
				memset(info, 0, sizeof(struct resources));
				info->storage = storage;
			}

			// Wait for a request. On timeout, make sure the connection is still established.
			protocol = proxy_request(fd, control[0]);
#if defined(FILEMENT_TLS)
			if (protocol == PROXY_HTTPS)
			{
				debug_("Incoming HTTPS request");
				if (stream_init_tls_connect(&info->stream, fd, 0)) // TODO: set third argument
				{
					close(fd);
					continue; // TODO should this continue?
				}
			}
			else
#endif
			if (protocol == PROXY_HTTP)
			{
				debug_("Incoming HTTP request");
				if (stream_init(&info->stream, fd)) fail(1, "Memory error");
			}
			else if (!protocol)
			{
				debug_("Reconnect to the proxy.");
				continue; // connection expired; start a new one
			}
			else
			{
				warning_("Broken proxy connection.");
				break;
			}

			// Start a thread to handle the established connection.
			// TODO: check for errors; stream_term() on error
			pthread_create(&thread, 0, &server_serve_windows, info);
			pthread_detach(thread);
			info = 0;
		}

error:

		// TODO read the bytes and check their value when necessary to support different commands
#if !defined(OS_WINDOWS)
		ioctl(control[0], FIONREAD, &bytes);
		if (bytes)
		{
			close(control[0]);
			warning_("Filement stopped.");
			break;
		}
#else
		iResult = ioctlsocket(control[0], FIONREAD, &iMode);
		if (iResult != NO_ERROR)
		  printf("ioctlsocket failed with error: %ld\n", iResult);
		  
		 if (iMode)
		{
			close(control[0]);
			warning("Filement stopped.");
			break;
		}
#endif

		// Wait before trying to connect to the proxy again
		warning_("Wait and reconnect.");
		sleep(wait);
		if (wait < PROXY_WAIT_MAX) wait *= 2;
	}

	free(info);
	free(proxies);

	return 0;
}
