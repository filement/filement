int http_parse_windows(struct http_request *restrict request, struct stream *restrict stream)
{
	struct http_context context;
	http_parse_init(&context);

	int status;
	while (true)
	{
		status = http_parse(&context, stream);
		if (status == ERROR_AGAIN) continue;
		else if (!status) break;
		else
		{
			dict_term(&context.request.headers);
			return status;
		}
	}

	*request = context.request;

	// Check if host header is specified.
	struct string name = string("host");
	request->hostname = dict_get(&request->headers, &name);
	if (!request->hostname)
	{
		free(request->URI.data);
		request->URI.data = 0;
		dict_term(&request->headers);
		return BadRequest;
	}

	return 0;
}
