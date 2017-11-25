#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef OS_WINDOWS
#define WINVER 0x0501
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <sys/stat.h>
#include "mingw.h"
#endif

#include "types.h"
#include "actions.h"

// TODO deprecated. use remote_json_chunked_start instead
bool remote_json_chunked_init(const struct http_request *request,struct http_response *restrict response, struct resources *restrict resources) //TODO error handling
{
	struct string s;
	union json *protocol=NULL,*function,*request_id;
	struct string buffer;
	int status = false;

	if(json_type(request->query)!=OBJECT) return 0;
	s = string("protocol");
	protocol=(union json *)dict_get(request->query->object, &s);
	if(!protocol) return 0;

	if(json_type(protocol)==OBJECT)
	{
		//tuka moje i name da vzemam da proverqvam dali trqbva da vurna json

		s = string("function");
		function=(union json *)dict_get(protocol->object, &s);
		if (!function || (json_type(function) != STRING)) return 0;
		
		s = string("request_id");
		request_id=(union json *)dict_get(protocol->object, &s);
		if (!request_id || (json_type(request_id) != STRING)) return 0;
		
	}
	else return 0;

	//size e vzet, izprashtam dannite

	response->code = OK;

	struct string key, value;

	key = string("Content-Type");
	value = string("text/plain; charset=UTF-8");
	if (!response_header_add(response, &key, &value)) return 0;

	if (!response_headers_send(&resources->stream, request, response, RESPONSE_CHUNKED)) return 0;

	if (!response_content_send(&resources->stream, response, function->string_node.data, function->string_node.length)) return 0;

	buffer = string("(\"");
	if (!response_content_send(&resources->stream, response, buffer.data, buffer.length)) return 0;

	if (!response_content_send(&resources->stream, response, request_id->string_node.data, request_id->string_node.length)) return 0;

	buffer = string("\",\"");
	if (!response_content_send(&resources->stream, response, buffer.data, buffer.length)) return 0;

	return true;
}

bool remote_json_chunked_start(struct stream *restrict stream, const struct http_request *request, struct http_response *restrict response) //TODO error handling
{
	struct string s;
	union json *protocol=NULL,*function,*request_id;
	struct string buffer;
	int status = false;

	if(json_type(request->query)!=OBJECT) return 0;
	s = string("protocol");
	protocol=(union json *)dict_get(request->query->object, &s);
	if(!protocol) return 0;

	if(json_type(protocol)==OBJECT)
	{
		//tuka moje i name da vzemam da proverqvam dali trqbva da vurna json

		s = string("function");
		function=(union json *)dict_get(protocol->object, &s);
		if (!function || (json_type(function) != STRING)) return 0;
		
		s = string("request_id");
		request_id=(union json *)dict_get(protocol->object, &s);
		if (!request_id || (json_type(request_id) != STRING)) return 0;
		
	}
	else return 0;

	//size e vzet, izprashtam dannite

	response->code = OK;

	struct string key, value;

	key = string("Content-Type");
	value = string("text/plain; charset=UTF-8");
	if (!response_header_add(response, &key, &value)) return 0;

	if (!response_headers_send(stream, request, response, RESPONSE_CHUNKED)) return 0;

	if (!response_content_send(stream, response, function->string_node.data, function->string_node.length)) return 0;

	buffer = string("(\"");
	if (!response_content_send(stream, response, buffer.data, buffer.length)) return 0;

	if (!response_content_send(stream, response, request_id->string_node.data, request_id->string_node.length)) return 0;

	buffer = string("\",");
	if (!response_content_send(stream, response, buffer.data, buffer.length)) return 0;

	return true;
}

bool remote_json_chunked_end(struct stream *restrict stream, struct http_response *restrict response)
{
	struct string buffer = string(");");
	if (!response_content_send(stream, response, buffer.data, buffer.length)) return false;
	if (!response_chunk_last(stream, response)) return false;
	return true;
}

// TODO deprecated. use remote_json_chunked_end instead
bool remote_json_chunked_close(struct http_response *restrict response, struct resources *restrict resources)
{
	struct string buffer = string("\");");
	if (!response_content_send(&resources->stream, response, buffer.data, buffer.length)) return false;
	if (!response_chunk_last(&resources->stream, response)) return false;
	return true;
}

bool remote_json_send(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, struct string *restrict json_serialized)
{
/*
Example response:
kernel.remotejson.response("1347971213468",{'session_id': "77091be6ccee82847e2faec4c0fd6b13",});
*/
	struct string s;
	union json *protocol=NULL,*function,*request_id;
	int status;
	size_t len=json_serialized->length;

	if(json_type(request->query)!=OBJECT) return false;
	s = string("protocol");
	protocol=(union json *)dict_get(request->query->object, &s);
	if(!protocol) return false;

	if(json_type(protocol)==OBJECT)
	{
	//tuka moje i name da vzemam da proverqvam dali trqbva da vurna json
		s = string("function");
		function=(union json *)dict_get(protocol->object, &s);
		if (!function || (json_type(function) != STRING)) return false;
		len+=function->string_node.length;
		
		s = string("request_id");
		request_id=(union json *)dict_get(protocol->object, &s);
		if(!request_id || (json_type(request_id) != STRING)) return false;
		len+=request_id->string_node.length;
	}
	else return false;

	len+=6;//other bytes
	//size e vzet, izprashtam dannite

	struct string key, value;
	key = string("Content-Type");
	value = string("text/plain; charset=UTF-8");
	if (!response_header_add(response, &key, &value)) return false; // memory error

	response->code = OK;

	if (!response_headers_send(&resources->stream, request, response, len)) return false;

	struct string begin = string("(\""), middle = string("\","), end = string(");");
	return
		response_content_send(&resources->stream, response, function->string_node.data, function->string_node.length) &&
		response_content_send(&resources->stream, response, begin.data, begin.length) &&
		response_content_send(&resources->stream, response, request_id->string_node.data, request_id->string_node.length) &&
		response_content_send(&resources->stream, response, middle.data, middle.length) &&
		response_content_send(&resources->stream, response, json_serialized->data, json_serialized->length) &&
		response_content_send(&resources->stream, response, end.data, end.length);
}

int remote_json_error(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, int code)
{
	union json *root, *node;
	struct string key, *json_serialized;

	// Create error response in JSON format
	root = json_object_old(false);
	if (!root) return -1;
	node = json_integer(code);
	if (!node)
	{
		json_free(root);
		return -1;
	}
	key = string("error");
	if (json_object_insert_old(root, &key, node))
	{
		json_free(node);
		json_free(root);
		return -1;
	}

	// Send response
	json_serialized = json_serialize(root);
	json_free(root);
	if (!json_serialized) return -1;

	bool success = remote_json_send(request, response, resources, json_serialized);
	free(json_serialized);
	return (success ? 0 : -1);
}

int ping(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *options)
{
	struct string buffer = string("{\"p\":1}");
	remote_json_send(request, response, resources, &buffer);
	return 0;
}
