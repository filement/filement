#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>

#include "types.h"
#include "stream.h"
#include "json.h"
#include "http.h"
#include "http_parse.h"
#include "http_response.h"
#include "storage.h"
#include "io.h"
#include "cache.h"
#include "server.h"
#include "ftp/curl_core.h"
#include "ftp/upload.h"
#include "status.h"
#include "log.h"

/*
#include "types.h"
#include "log.h"
#include "io.h"
#include "stream.h"
#include "http.h"
#include "http_parse.h"
#include "http_response.h"
#include "upload.h"
#include "status.h"
#include "json.h"
#include "curl_core.h"
*/

//#define BUFFER_SIZE_MAX 0x10000 /* 64 KiB */

#define NETWORK_TIMEOUT 10000 /* 10s */

struct io_html5
{
	const struct string *status_key;
	struct string buffer;
	off_t length;
	off_t progress;
	int fd;
};

struct io_html
{
	const struct string *status_key;
	struct string *buffer;
	struct stream *stream;
	struct string *pattern;
	size_t *table;
	ssize_t	*size;
	off_t *progress;
};

static size_t transfer(void *buffer, size_t size, size_t nmemb, void *arg)
{
	struct io_html5 *io = arg;
	size_t length = size * nmemb;
	size_t cached = 0;

	//off_t progress = status_get(io->status_key, 0);
	//if(io->status_key)status_set(io->status_key, io->progress, STATE_PENDING);

	// Look for cached data
	if (io->buffer.length)
	{
		if (length < io->buffer.length)
		{
			// The cache is more than the requested amount
			memcpy(buffer, io->buffer.data, length);
			io->progress += length;
			//if(io->status_key)status_set(io->status_key, io->progress + length, STATE_PENDING);
			io->buffer.data += length;
			io->buffer.length -= length;
			return length;
		}
		else
		{
			cached = io->buffer.length;
			memcpy(buffer, io->buffer.data, cached);
			io->progress += cached;
			io->buffer.length = 0;
			buffer = (char *)buffer + cached;
			length -= cached;
		}
	}

	struct pollfd source;
	source.fd = io->fd;
	source.events = POLLIN;
	source.revents = 0;

	// Download the specified content
	while (true)
	{
		if (io->progress < io->length)
		{
			if (poll(&source, 1, NETWORK_TIMEOUT) < 0)
			{
				if (errno == EINTR) continue;
				else break;
			}
 
			if (!(source.revents & POLLIN)) break;
			else
			{
				// Make sure nothing after the specified length is read
				size = read(io->fd, buffer, length);
				if (size < 0) break;
				// TODO: SIGINT

				//if (!writeall(output, buffer, size)) break;
			}
		}
		else size = 0;

		// Update download status
		//if(io->status_key)status_set(io->status_key, io->progress, size ? STATE_PENDING : STATE_FINISHED);

		io->progress += size;
		return cached + size;
	}

	//if(io->status_key)status_set(io->status_key, io->progress, STATE_ERROR);
	return CURL_READFUNC_ABORT;
}

// TODO: remake this function
static struct string *post_filename(const struct string *root, const struct string *content_disposition)
{
	struct dict options;
	if (!dict_init(&options, DICT_SIZE_BASE)) return 0;
	if (http_parse_content_disposition(&options, content_disposition))
	{
		dict_term(&options);
		return 0;
	}

	struct string field = string("filename");
	struct string *filename = dict_remove(&options, &field);
	dict_term(&options);
	if (!filename) return 0;

	struct string *path = malloc(sizeof(struct string) + sizeof(char) * (root->length + 1 + filename->length + 1));
	if (!path)
	{
		free(filename);
		return 0;
	}
	path->length = root->length + 1 + filename->length;
	path->data = (char *)(path + 1);
	memcpy(path->data, root->data, root->length);
#ifdef OS_WINDOWS
	path->data[root->length] = '\\';
#else
	path->data[root->length] = '/';
#endif
	memcpy(path->data + root->length + 1, filename->data, filename->length);
	path->data[path->length] = 0;

	free(filename);

	return path;
}

static inline void file_finish(struct string *restrict filename, int file, bool success)
{
	close(file);
	if (!success)
	{
#ifdef OS_BSD
		unlink(filename->data);
#else
		remove(filename->data);
#endif
	}
	free(filename);
}

static size_t transfer_html(void *output, size_t size, size_t nmemb, void *arg)
{
	struct io_html *io = arg;
	size_t length = size * nmemb;
	size_t offset = 0;

	while (true)
	{
		if (stream_read(io->stream, io->buffer, io->pattern->length + 4)) // \r\n and possibly -- before that (marking end of body)
		{
			status_set(io->status_key, *io->progress, STATE_ERROR);
			return CURL_READFUNC_ABORT;
		}

		// Look for end boundary
		*io->size = kmp_search(io->pattern, io->table, io->buffer);
		if (*io->size >= 0)
		{
			// Content should end with \r\n
			// Check for it and don't write it to the file

			if ((io->buffer->data[*io->size - 2] != '\r') || (io->buffer->data[*io->size - 1] != '\n'))
			{
				status_set(io->status_key, *io->progress, STATE_ERROR);
				return CURL_READFUNC_ABORT;
			}

			size_t available = *io->size - 2;

			if (length < available) 
			{
				memcpy(output + offset, io->buffer->data, length);
				stream_read_flush(io->stream, length);
				*io->progress += length;
				status_set(io->status_key, *io->progress, STATE_PENDING);
				return offset + length;
			}
			else if (offset + available)
			{
				if (available)
				{
					memcpy(output + offset, io->buffer->data, available);
					stream_read_flush(io->stream, available); // There are 2 unnecessary characters after the boundary
					*io->progress += available;
					status_set(io->status_key, *io->progress, STATE_PENDING);
				}
				return offset + available;
			}
			else
			{
				stream_read_flush(io->stream, 2 + io->pattern->length + 2);
				*io->progress += 2 + io->pattern->length + 2;
				status_set(io->status_key, *io->progress, STATE_FINISHED);
				return 0;
			}
		}
		else
		{
			// Write the data that is guaranteed to be in the content which is
			//  everything except the terminating \r\n and a propper prefix of the boundary
			//  as they can be in the buffer

			size_t available = io->buffer->length - io->pattern->length - 1;

			if (length <= available)
			{
				memcpy(output + offset, io->buffer->data, length);
				stream_read_flush(io->stream, length);
				*io->progress += length;
				status_set(io->status_key, *io->progress, STATE_PENDING);
				return offset + length;
			}
			else
			{
				memcpy(output + offset, io->buffer->data, available);
				stream_read_flush(io->stream, available);
				*io->progress += available;
				offset += available;
				length -= available;
				status_set(io->status_key, *io->progress, STATE_PENDING);
			}
		}
	}
}

// TODO: this can be optimized for faster upload using ioctl
static int upload_multipart(const struct string *root, const struct dict *options, struct resources *restrict resources, struct stream *restrict stream, const struct string *status_key)
{
	struct string buffer, field;
	CURL *curl=0;
	// Get boundary. We don't need any other options.
	struct string key = string("boundary");
	struct string *boundary = dict_get(options, &key);
	if (!boundary) return UnsupportedMediaType; // TODO: check whether this is the right response

	// Generate pattern
	struct string pattern;
	pattern.length = 2 + boundary->length; // 2 for initial --
	pattern.data = malloc(sizeof(char) * (pattern.length + 1));
	if (!pattern.data) return InternalServerError;
	pattern.data[0] = '-';
	pattern.data[1] = '-';
	memcpy(pattern.data + 2, boundary->data, boundary->length);
	pattern.data[pattern.length] = 0;

	// TODO: check for Expect header
	/*key = string("expect");
	value = dict_get(&request->headers, &key);
	buffer = 
	if ()
	buffer = string("HTTP/1.1 100 Continue\r\n");
	stream_write(stream, &buffer) && stream_write_flush(stream);*/

	int status = 0;
	size_t *table = 0;

	off_t progress = 0;
	status_set(status_key, progress, STATE_PENDING);

	if (stream_read(stream, &buffer, pattern.length + 4)) // \r\n and possibly -- before that (marking end of body)
	{
		status = NotFound; // TODO: this should not be not found
		goto finally;
	}

	// Body should start with boundary
	if (memcmp(buffer.data, pattern.data, pattern.length))
	{
		status = NotFound; // TODO: this should not be not found
		goto finally;
	}

	stream_read_flush(stream, pattern.length + 2); // There are 2 unnecessary characters after the boundary
	progress += pattern.length + 2;
	status_set(status_key, progress, STATE_PENDING);

	// Generate table for boundary search
	table = kmp_table(&pattern);
	if (!table)
	{
		status = InternalServerError; // TODO: free memory
		goto finally;
	}

	// TODO: make this work for more general cases

	ssize_t size = 0; // Size of the data before the boundary
	struct dict part_header;
	int file;
	while (memcmp(buffer.data + size + pattern.length, "--\r\n", 4)) // Parse and handle each part of the body
	{
		if ((buffer.data[size + pattern.length] != '\r') || (buffer.data[size + pattern.length + 1] != '\n'))
		{
			status = BadRequest; // TODO: invalid request. is this the right response code?
			goto finally;
		}

		// Check whether filename is specified
		// TODO: use content disposition to determinte what to do
		status = http_parse_header(&part_header, stream);
		if (status) goto finally;
		field = string("content-disposition");
		struct string *content_disposition = dict_get(&part_header, &field);
		if (!content_disposition)
		{
			dict_term(&part_header);
			status = BadRequest; // TODO
			goto finally;
		}
		struct string *filename = post_filename(root, content_disposition);

		// Free all part headers and their dictionary
		dict_term(&part_header);

		// TODO: uploading multiple files can lead to undeleted files on error

		// If this part is file contents, download the file to the server. Otherwise, just find the next boundary
		if (filename)
		{
			const union json *login=0;
			if(resources->session_access && json_type(resources->session.ro)==OBJECT)login=resources->session.ro;
			else if(resources->auth && resources->auth->login && json_type(resources->auth->login)==OBJECT)login=resources->auth->login;
			else return Forbidden;

			curl=curl_easy_init();
			if(!curl)return NotFound;
			CURLcode res;
			char *url=0,*tmp=0;
			union json *item;
			struct string key;
			
			struct io_html io = {
				.stream = stream,
				.status_key = status_key,
				.pattern = &pattern,
				.table = table,
				.size = &size,
				.progress = &progress,
				.buffer = &buffer
			};
			
			key = string("host");
			item=dict_get(login->object, &key);
			if(!item)return 0;
			if(json_type(item)!=STRING)return 0;

			if(filename->length>1)
			{
			tmp=curl_escape(filename->data+1,filename->length-1);
			}
			else tmp=filename->data;
			
			int tmp_len=strlen(tmp);
			url=(char *)malloc(sizeof(char)*( 6 + item->string_node.length +1+ tmp_len +1 ));
			if(!url){
			if(filename->length>1)curl_free(tmp);
			free(filename);
			return 0;
			}
			replace_2F(tmp,tmp_len);
			if(sprintf(url,"ftp://%s/%s",item->string_node.data,tmp)<0)
			{
			if(filename->length>1)curl_free(tmp);
			free(filename);
			free(url);
			return 0;
			}
			
			if(filename->length>1)curl_free(tmp);
			free(filename);
			filename=0;

			if(curl && curl_set_login(curl,login)) {
			
			curl_easy_setopt(curl, CURLOPT_URL, url);

			curl_easy_setopt(curl, CURLOPT_READFUNCTION, transfer_html);
			curl_easy_setopt(curl, CURLOPT_READDATA, &io);
		 
			//TODO: To check do I really need the size, because it may stuck the connection on error.
			//curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE,(curl_off_t)length);
			
			curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);


				struct curl_slist *headerlist=NULL;
			headerlist = curl_slist_append(headerlist, "OPTS UTF8 ON");
			curl_easy_setopt(curl, CURLOPT_POSTQUOTE, headerlist);

			// Write the cached stream data
			//status_set(status_key, 0, STATE_PENDING);
			size_t cached = stream_cached(stream);
			stream_read(stream, io.buffer, cached);
			//if (!writeall(file, buffer.data, cached)) return false;
			//status_set(status_key, cached, STATE_PENDING);
				
			res = curl_easy_perform(curl);
			
			curl_slist_free_all (headerlist);
			curl_easy_reset(curl);
				/* Check for errors */ 
			
			
				if(res != CURLE_OK)
				{
				free(url);url=0;
				status = NotFound; // TODO: this should not be not found
				fprintf(stderr, "curl_easy_perform() host:%s failed: %s\n",url,curl_easy_strerror(res));
				goto finally;
				}

			}
			else
			{
				free(url);url=0;
				status = InternalServerError; // TODO: this should not be not found
				goto finally;
			}
			
			free(url);
		}
		else while (true)
		{
			// Skip data with no filename specified.

			// TODO: do something with non-file data

			if (stream_read(stream, &buffer, pattern.length + 4)) // \r\n and possibly -- before that (marking end of body)
			{
				status = NotFound; // TODO: this should not be not found
				goto finally;
			}

			// Look for end boundary
			size = kmp_search(&pattern, table, &buffer);
			if (size >= 0)
			{
				// Check for \r\n after the value
				if ((buffer.data[size - 2] != '\r') || (buffer.data[size - 1] != '\n'))
				{
					status = NotFound; // TODO: this should not be not found
					goto finally;
				}

				stream_read_flush(stream, size + pattern.length + 2); // There are 2 unnecessary characters after the boundary
				progress += size + pattern.length + 2;
				status_set(status_key, progress, STATE_PENDING);
				break;
			}
			else
			{
				stream_read_flush(stream, buffer.length - pattern.length - 1);
				progress += buffer.length - pattern.length - 1;
				status_set(status_key, progress, STATE_PENDING);
			}
		}
	}

	// Flush the terminating \r\n
	stream_read_flush(stream, 2);
	progress += 2;
	status_set(status_key, progress, STATE_FINISHED);

finally:

	if(curl)curl_easy_cleanup(curl);

	free(table);
	free(pattern.data);

	return status;
}

int http_upload(const struct string *root, const struct string *filename, struct resources *restrict resources, const struct http_request *request, struct stream *restrict stream, const struct string *status_key)
{
	struct string key;
	size_t length;

	// Get content length
	key = string("content-length");
	struct string *content_length = dict_get(&request->headers, &key);
	if (!content_length) return LengthRequired;
	errno = 0;
	length = (size_t)strtol(content_length->data, 0, 10);
	if (!length && (errno == EINVAL)) return BadRequest;

	// If filename is specified, use HTML 5 upload.
	if (filename)
	{
		// Generate path
		char *path = malloc(sizeof(char) * (root->length + 1 + filename->length + 1));
		if (!path) return 0; // TODO: this should fail the program
		memcpy(path, root->data, root->length);
		path[root->length] = '/';
		memcpy(path + root->length + 1, filename->data, filename->length);
		path[root->length + 1 + filename->length] = 0;
		//TODO vav filename ne trqbva da ima / 

		const union json *login=0;
	if(resources->session_access && json_type(resources->session.ro)==OBJECT)login=resources->session.ro;
	else if(resources->auth && resources->auth->login && json_type(resources->auth->login)==OBJECT)login=resources->auth->login;
	else return Forbidden;

	CURL *curl=curl_easy_init();
	if(!curl)return NotFound;
	CURLcode res;
	char *url=0,*tmp=0;
	union json *item;
	struct string key;

	struct io_html5 io = {.fd = stream->fd, .status_key = status_key, .length=length};
	
	key = string("host");
	item=dict_get(login->object, &key);
	if(!item)return 0;
	if(json_type(item)!=STRING)return 0;

	

	if((root->length + 1 + filename->length)>1)
	{
	tmp=curl_escape(path+1,(root->length + 1 + filename->length)-1);
	}
	else return false;
	int tmp_len=strlen(tmp);
	url=(char *)malloc(sizeof(char)*( 6 + item->string_node.length + 1 + tmp_len +1 ));
	if(!url){curl_free(tmp);free(path);return 0;}
	replace_2F(tmp,tmp_len);
	if(sprintf(url,"ftp://%s/%s",item->string_node.data,tmp)<0)
	{
	curl_free(tmp);
	free(path);
	free(url);
	return 0;
	}
	curl_free(tmp);
	free(path);



    if(curl && curl_set_login(curl,login)) {
    
	curl_easy_setopt(curl, CURLOPT_URL, url);

    curl_easy_setopt(curl, CURLOPT_READFUNCTION, transfer);
    curl_easy_setopt(curl, CURLOPT_READDATA, &io);
 
	//TODO: To check do I really need the size, because it may stuck the connection on error.
    //curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE,(curl_off_t)length);
	
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);


		struct curl_slist *headerlist=NULL;
	headerlist = curl_slist_append(headerlist, "OPTS UTF8 ON");
    curl_easy_setopt(curl, CURLOPT_POSTQUOTE, headerlist);

	// Write the cached stream data
	//status_set(status_key, 0, STATE_PENDING);
	size_t cached = stream_cached(stream);
	stream_read(stream, &io.buffer, cached);
	//if (!writeall(file, buffer.data, cached)) return false;
	//status_set(status_key, cached, STATE_PENDING);
		
	res = curl_easy_perform(curl);
	
	curl_slist_free_all (headerlist);
	curl_easy_cleanup(curl);

	stream_read_flush(stream, cached); // TODO: is this right

        /* Check for errors */ 
	
	
		if(res != CURLE_OK)
		{
			fprintf(stderr, "curl_easy_perform() host:%s failed: %s\n",url,curl_easy_strerror(res));
			free(url);url=0;
			return 0;
		}

		free(url);
		return OK;
	}
	else
	{
		free(url);url=0;
		return 0;
	}
	}// HTML5 
	else // Normal HTTP Upload
	{
		size_t index = 0;
		struct string *content_type;

		key = string("content-type");
		content_type = dict_get(&request->headers, &key);
		if (!content_type) return UnsupportedMediaType;

		#define CONTENT_TYPE_LENGTH_MAX 64

		char type[CONTENT_TYPE_LENGTH_MAX];

		// Parse content type
		// Main part's end is marked by end of header field or semicolon
		while ((index < content_type->length) && (content_type->data[index] != ';'))
		{
			// Remember lowercased content type
			type[index] = tolower(content_type->data[index]);
			++index;

			if (index == CONTENT_TYPE_LENGTH_MAX)
				return UnsupportedMediaType;
		}

		#undef CONTENT_TYPE_LENGTH_MAX

		#define CONTENT_TYPE_IS(t) ((index == (sizeof(t) - 1)) && !memcmp(type, (t), (sizeof(t) - 1)))
		if (CONTENT_TYPE_IS("application/x-www-form-urlencoded"))
		{
			return UnsupportedMediaType; // TODO: support this type
		}
		else if (CONTENT_TYPE_IS("multipart/form-data"))
		{
			// This content type requires options
			if (index == content_type->length) return UnsupportedMediaType; // TODO: check whether this is the right response

			// There must be space character before the options
			if ((++index == content_type->length) || (content_type->data[index] != ' ')) return BadRequest;

			struct dict options;

			// Parse options
			if (!dict_init(&options, DICT_SIZE_BASE)) return InternalServerError;
			struct string buffer = string(content_type->data + index + 1, content_type->length - index - 1);
			int status = http_parse_options(&options, &buffer);
			if (status)
			{
				dict_term(&options);
				return status;
			}

			// TODO: content-length is not used here

			status = upload_multipart(root, &options, resources, stream, status_key);

			dict_term(&options);
			return status;
		}
		else return UnsupportedMediaType;
		#undef CONTENT_TYPE_IS
	}
}
