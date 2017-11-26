#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#if defined(PUBCLOUD)
# include <sys/fsuid.h>
#endif

#ifdef OS_BSD
# include <arpa/inet.h>
# include <netdb.h>
# include <poll.h>
# include <sys/mman.h>
# include <sys/socket.h>
#endif

#ifdef OS_WINDOWS
#define _WIN32_WINNT 0x0501
#define WINVER 0x0501
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "mingw.h"
#endif

#include "types.h"
#include "format.h"
#include "aes.h"

#ifdef OS_WINDOWS
#include "../actions.h"
#include "../storage.h"
#include "../io.h"
#include "../device/distribute.h"
#include "../access.h"
#else
# include "actions.h"
# include "storage.h"
# include "io.h"
# include "device/distribute.h"
# include "access.h"
#endif

#include "fs.h"
#include "session.h"

#ifdef OS_WINDOWS
const char *inet_ntop(int af, const void *src, char *dst, socklen_t cnt)
{
	if (af == AF_INET)
	{
		struct sockaddr_in srcaddr;

		memset(&srcaddr, 0, sizeof(struct sockaddr_in));
		memcpy(&(srcaddr.sin_addr), src, sizeof(srcaddr.sin_addr));
	 
		srcaddr.sin_family = af;
		if (WSAAddressToString((struct sockaddr*) &srcaddr, sizeof(struct sockaddr_in), 0, dst, (LPDWORD) &cnt) != 0) {
			DWORD rv = WSAGetLastError();
			printf("WSAAddressToString() : %d\n",rv);
			return NULL;
		}
		
	}
	else if (af == AF_INET6)
	{
			struct sockaddr_in6 in;
			memset(&in, 0, sizeof(in));
			in.sin6_family = AF_INET6;
			memcpy(&in.sin6_addr, src, sizeof(struct in_addr6));
			getnameinfo((struct sockaddr *)&in, sizeof(struct
sockaddr_in6), dst, cnt, NULL, 0, NI_NUMERICHOST);
			return dst;
	}
	return NULL;
}
#endif

static void blocks_array_free(struct blocks_array *blocks_array)
{
	if(!blocks_array)return;
	
int i;
	for(i=0;i<blocks_array->blocks_size;i++)
		{
			free(blocks_array->blocks[i]);
		}
free(blocks_array);
}

static const struct
{
	struct string name;
	int (*handler)(const struct http_request *, struct http_response *restrict, struct resources *restrict, const union json *);
} actions[] = {
	ACTIONS
};


union json *session_actions_json(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *options, int *local_errno)
{
//TODO to get from one place the ACTIONS also to show the actions with the authentication depending on the user
union json *root;
unsigned int i=0;

if(resources->auth_id)auth_id_check(resources);
if(!resources->auth && !session_is_logged_in(resources)){*local_errno=1001;return 0;}

root = json_array();
if(!root)return 0;

	struct string value;
	for(;i<(sizeof(actions) / sizeof(*actions));i++)
	{
		value = string(actions[i].name.data, actions[i].name.length);
		if(json_array_insert_old(root, json_string_old(&value)))
			return 0;
	}
	
	return root;
	
}

int session_actions(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *options)
{
//TODO to get from one place the ACTIONS also to show the actions with the authentication depending on the user
struct string *json_serialized=NULL;
union json *root;
int local_errno=0;

root=session_actions_json(request,response,resources,options,&local_errno);
if(!root)goto error;

	json_serialized=json_serialize(root);
	if(!json_serialized)goto error;

	if(!remote_json_send(request,response, resources, json_serialized))
	{
	//ERROR to return json
	free(json_serialized);json_serialized=0;
	json_free(root);
	return -1;
	}
	
	json_free(root);
	return 0;
	
	error:
	if(root)json_free(root);
	if(!local_errno)local_errno=errno;
	return remote_json_error(request, response, resources, local_errno);
}

int session_login(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *options)
{
//funkciqta trqbva da izprati pri pravilen login
/*

Request:
{
 'protocol': {
  'name': 'remoteJson',
  'function': 'kernel.remotejson.receive',
  'request_id': '123456',
 },
 'actions': [
  ...
 ]
}

Response pri OK:

kernel.remotejson.response("1347971213468",{'session_id': "77091be6ccee82847e2faec4c0fd6b13",});
*/
union json *root, *temp,*json_token;
struct string key, value, passport_key, *json_serialized=NULL;
union json *item;
struct string buffer;
int local_errno=0;
//polu4avam user_id i session i vrushtam session_id ako e success
//predpolagame 4e e lognat
if(json_type(options)!=OBJECT)goto error;

key = string("token");
item=dict_get(options->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;

	uint32_t client_id;
	char id[32];
	char aes_key[16 + 1];

	// Send request.
	struct stream *stream = distribute_request_start(0, CMD_TOKEN_AUTH_OLD, &item->string_node);
	if (!stream) goto error;

	// Get client_id.
	if (stream_read(stream, &buffer, sizeof(uint32_t) + 16))
	{
		distribute_request_finish(false);
		goto error;
	}

	endian_big32(&client_id, buffer.data);
	format_string(aes_key, buffer.data + sizeof(uint32_t), 16);

	stream_read_flush(stream, sizeof(uint32_t) + 16);
	distribute_request_finish(true);

	struct string token = string(id, format_int(id, client_id) - id);


if (!session_create(resources) || !session_edit(resources)) return InternalServerError;

root = json_object_old(false); 

//TODO checkvam passport_id-to i go slagam v sesiqta sled kato e initzializirana

key = string(resources->session_id, CACHE_KEY_SIZE);
temp = json_string_old(&key);
if(!temp)goto error;
key = string("session_id");
json_object_insert_old(root, &key, temp);

#if !defined(DEBUG)
	key = string("encryption");
	json_object_insert_old(root, &key, json_integer(1));
#endif

json_serialized=json_serialize(root);
json_free(root);root=NULL;temp=NULL;
if(!json_serialized)goto error;

#ifdef PUBCLOUD

if(!storage_passport_users_check(resources->storage,&token))
{
struct string path;
	if(!storage_passport_users_add(resources->storage, client_id))goto error;
	key=string("root_path");
	char *root_path=storage_local_settings_get_value(resources->storage, &key);
	if(!root_path)goto error;
	path.data=malloc(256);
	path.length = format(path.data,str(root_path,strlen(root_path)),uint(client_id),str("/",2)) - path.data - 1;
	free(root_path);
	key=string("0");
	if(!storage_blocks_add(resources->storage, 0, client_id,&path ,&key)){free(path.data);goto error;}
	path.data[path.length-1]='\0';
	mkdir(path.data,0755);
	chown(path.data,65534,65534);
}
#endif

	key = string("encrypt_key");
	value = string(aes_key);
	json_object_insert_old(resources->session.rw, &key, json_string_old(&value)); // TODO: this may fail

	// TODO: remove this
	char buf[32 + 1];
	*format_hex(buf, value.data, value.length) = 0;

	//v slu4ai 4e sme lognati dobavqme passport_id
	passport_key = string("passport_id");
	if (json_object_insert_old(resources->session.rw, &passport_key, json_string_old(&token))) goto error;

	passport_key = string("user_id");
	#ifdef DEVICE
	//TODO if more than one user for device to fix below
	if (json_object_insert_old(resources->session.rw, &passport_key, json_integer(1))) goto error;
	#else
	if (json_object_insert_old(resources->session.rw, &passport_key, json_integer(strtol(token.data,0,10)))) goto error;
	#endif

session_save(resources);

//json_free(item);
if(!remote_json_send(request,response, resources, json_serialized))
{
//ERROR to return json
free(json_serialized);json_serialized=0;
return -1;
}

free(json_serialized);

return 0;
//tuka trqbva da vrushta json greshka
error:
buffer = string("{\"error\":\"1\"}");
remote_json_send(request, response, resources, &buffer);
return 0;
}

int session_logout(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : {}
*/
struct string *json_serialized=NULL;
union json *root;

if(!resources->session_access)goto error;
session_destroy(resources);

//if(!storage_auth_delete_value(resources->storage,auth_id))goto error;
//end of func main

root = json_object_old(false);
if(!root)goto error;
json_serialized=json_serialize(root);
if(!json_serialized)goto error;

if(!remote_json_send(request,response, resources, json_serialized))
{
//ERROR to return json
free(json_serialized);json_serialized=0;
return -1;
}

free(json_serialized);
return 0;



error: //ERROR tuka trqbva da vrushta json za greshka
json_serialized=string_alloc("{\"error\":\"1\"}", 13);
remote_json_send(request, response, resources, json_serialized);
free(json_serialized);
return 0;
}

bool auth_id_check(struct resources *restrict resources)
{
	if(resources->auth_id)
	{
		resources->auth=storage_auth_get(resources->storage,resources->auth_id);
		if(resources->auth)
		{
#ifdef PUBCLOUD
			setfsuid(resources->auth->uid);
			setfsgid(resources->auth->gid);
#endif		
		return true;
		}
	}
	return false;
}

bool session_is_logged_in(struct resources *restrict resources)
{
const union json *session = resources->session.ro, *item;
struct string key;

if(!resources->session_access)return false;

key = string("passport_id");
item=dict_get(session->object, &key);
if(!item)return false;
if(json_type(item)!=STRING)return false;
//if(item->string_node.length < 2)return true;

return true;
}

int session_grant(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : { "hash":string }
*/
	struct string *json_serialized=NULL,username;
	union json *temp, *item;
	struct string key,*hash=0;
	int local_errno=0;

	if(!session_is_logged_in(resources)){local_errno=1001;goto error;}
	if(json_type(query)!=OBJECT)goto error;

	key = string("hash");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != STRING)) goto error;
	hash = &item->string_node;
	if ((hash->length % (128 / 8)) || (hash->length > 64)) goto error; // AES-encrypted value's length must be a multiple of AES block size

	if (!resources->session_access || (json_type(resources->session.ro) != OBJECT)) goto error;
	key = string("encrypt_key");
	struct string *value = dict_get(resources->session.ro->object, &key);
	if (!value) goto error;

	char data[64];
	char chunk[16 + 1];
	size_t index;
	struct aes_context context;
    aes_setup(value->data, value->length, &context);
	for(index = 0; index < hash->length / 2; index += 16)
	{
		hex2bin(chunk, hash->data + index * 2, 32);
		aes_decrypt(chunk, data + index, &context);
	}

	username=string("admin");
	struct string password = string(data, strnlen(data,64)); // TODO: data length is not available after AES decrypt. fix this somehow
	if(!storage_local_users_get_id(resources->storage, &username, &password)){local_errno=1015;goto error;}

if(json_type(resources->session.ro)!=OBJECT)goto error;
key = string("grant");
item=dict_get(resources->session.ro->object, &key);
	if(item)
	{
		if(json_type(item)==INTEGER)item->integer=1;
		else goto error;
	}
	else
	{
		if (!session_edit(resources)) goto error;
		json_object_insert_old(resources->session.rw, &key, json_integer(1));
		session_save(resources);
	}
json_serialized=string_alloc("{}", 2);
remote_json_send(request, response, resources, json_serialized);
free(json_serialized);

return 0;

error: //ERROR tuka trqbva da vrushta json za greshka
	return remote_json_error(request, response, resources, local_errno);
}


int session_grant_login(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : { "hash":string }
*/
struct string *json_serialized=NULL,username;
union json *temp, *item;
struct string key,*hash=0;
int local_errno=0;

if(!session_is_logged_in(resources)){local_errno=1001;goto error;}
if(json_type(query)!=OBJECT)goto error;

	key = string("hash");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != STRING)) goto error;
	hash = &item->string_node;
	if ((hash->length % (128 * 2 / 8)) || (hash->length > 64 * 2)) goto error; // AES-encrypted value's length must be a multiple of AES block size

	if (!resources->session_access || (json_type(resources->session.ro) != OBJECT)) goto error;
	key = string("encrypt_key");
	struct string *value = dict_get(resources->session.ro->object, &key);
	if (!value) goto error;

	char data[64];
	char chunk[16 + 1];
	size_t index;
	struct aes_context context;
    aes_setup(value->data, value->length, &context);
	for(index = 0; index < hash->length / 2; index += 16)
	{
		hex2bin(chunk, hash->data + index * 2, 32);
		aes_decrypt(chunk, data + index, &context);
	}

	username=string("admin");
	struct string password = string(data, strnlen(data,64)); // TODO: data length is not available after AES decrypt. fix this somehow
	if(!storage_local_users_get_id(resources->storage, &username, &password)){local_errno=1015;goto error;}

if(json_type(resources->session.ro)!=OBJECT)goto error;
key = string("authenticated");
item=dict_get(resources->session.ro->object, &key);
	if(item)
	{
		if(json_type(item)==INTEGER)item->integer=1;
		else goto error;
	}
	else
	{
		if (!session_edit(resources)) goto error;
		json_object_insert_old(resources->session.rw, &key, json_integer(1));
		session_save(resources);
	}
json_serialized=string_alloc("{}", 2);
remote_json_send(request, response, resources, json_serialized);
free(json_serialized);

return 0;
error: //ERROR tuka trqbva da vrushta json za greshka
return remote_json_error(request, response, resources, local_errno);

}

int auth_add(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : { "auth_id":string, "count" : int ,"rw" : int ,"name" : string, "data" : string, "blocks": ["0": {"path" : string "user_id" : int ,"block_id" : int}, ""...] }
*/
struct string *json_serialized=NULL;
union json *temp, *item;
struct vector *array=0;
unsigned i=0;

int count=0;
int rw=0;
struct string *auth_id=0;
struct string *name=0;
struct string *data=0;
struct blocks_array *blocks_array=0;
struct blocks *block=0;
struct string *location=0;
struct string *path=0;



struct string key;

#ifdef OS_WINDOWS
struct _stati64 attribute;
#else
struct stat attribute;
#endif

//firstly get the block_id from the request
if(!session_is_logged_in(resources))goto error;
//polzvam query za da vzema arguments
if(json_type(query)!=OBJECT)goto error;

key = string("count");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=INTEGER)goto error;
count=item->integer;

key = string("rw");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=INTEGER)goto error;
rw=item->integer;


key = string("auth_id");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;
auth_id=string_alloc(item->string_node.data, item->string_node.length);
if(!auth_id)goto error;


key = string("name");
item=dict_get(query->object, &key);
if(item)
{
if(json_type(item)!=STRING)goto error;
name=string_alloc(item->string_node.data, item->string_node.length);
if(!name)goto error;
}
else name = string_alloc("", 0);

key = string("data");
item=dict_get(query->object, &key);
if(item)
{
if(json_type(item)!=STRING)goto error;
data=string_alloc(item->string_node.data, item->string_node.length);
if(!data)goto error;
}
else data = string_alloc("", 0);

key = string("blocks");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=ARRAY)goto error;
array=&item->array_node;

if(!array)goto error;


	blocks_array=(struct blocks_array *)malloc(sizeof(struct blocks_array)+(sizeof(struct blocks *)*array->length));
	if(!blocks_array)return -1;
	memset(blocks_array,0,sizeof(struct blocks_array)+(sizeof(struct blocks *)*array->length));
    blocks_array->blocks_size=array->length;
	
	

for(i = 0; i < blocks_array->blocks_size; ++i)
	{
		blocks_array->blocks[i]=(struct blocks *)malloc(sizeof(struct blocks));
		memset(blocks_array->blocks[i],0,sizeof(struct blocks));
		
		temp = vector_get(array, i);
		if(!temp)goto error;
		if(json_type(temp)!=OBJECT)goto error;
		
		key = string("block_id");
		item=dict_get(temp->object, &key);
		if(!item)goto error;
		if(json_type(item)!=INTEGER)goto error;
		blocks_array->blocks[i]->block_id=item->integer;
		
		key = string("path");
		item=dict_get(temp->object, &key);
		if(!item)goto error;
		if(json_type(item)!=STRING)goto error;
		blocks_array->blocks[i]->location=&item->string_node;
		if(!blocks_array->blocks[i]->location)goto error;
		
		//check the path and the block_id
		
		//TODO to check is it really necessary
		block=access_get_blocks(resources,blocks_array->blocks[i]->block_id);
		if(!block)goto error;

		path=access_fs_concat_path(block->location,blocks_array->blocks[i]->location,1);
		if(!path)goto error;

		if( stat( path->data, &attribute ) == -1 )
		{
		fprintf(stderr,"can't stat errno %d %s\n",errno,path->data);
		goto error;
		}
		
		free(block);block=0;
		
	}// for(i = 0; i < array->length; ++i)
temp=0;





//function main
	key = string("user_id");
	item=dict_get(resources->session.ro->object, &key);
	if(!item)goto error;
	if(json_type(item)!=INTEGER)goto error;
json_serialized=storage_auth_add(resources->storage,auth_id,blocks_array,rw,count,item->integer,name,data);
if(!json_serialized)goto error;

//end of function main


if(!remote_json_send(request,response, resources, json_serialized))
{
//ERROR to return json
free(json_serialized);json_serialized=0;
goto error;
}

free(json_serialized);
free(auth_id);
blocks_array_free(blocks_array);
return 0;



error: //ERROR tuka trqbva da vrushta json za greshka
blocks_array_free(blocks_array);
free(block);
free(auth_id);
json_serialized=string_alloc("{\"error\":\"1\"}", 13);
remote_json_send(request, response, resources, json_serialized);
free(json_serialized);
return 0;
}

int auth_grant(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : { "auth_id":string, "count" : int ,"rw" : int ,"name" : string, "data" : string, "blocks": ["0": {"path" : string "user_id" : int ,"block_id" : int}, ""...] }
*/
struct string *json_serialized=NULL;
union json *temp, *item;
struct vector *array=0;
unsigned i=0;

int count=0;
int rw=0;
struct string *auth_id=0;
struct string *name=0;
struct string *data=0;
struct blocks_array *blocks_array=0;
struct blocks *block=0;
struct string *location=0;
struct string *path=0;

struct string key;

#ifdef OS_WINDOWS
struct _stati64 attribute;
#else
struct stat attribute;
#endif

//firstly get the block_id from the request
if(!session_is_logged_in(resources)) return Forbidden;
//polzvam query za da vzema arguments
if(json_type(query)!=OBJECT)goto error;

key = string("count");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=INTEGER)goto error;
count=item->integer;

key = string("rw");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=INTEGER)goto error;
rw=item->integer;


key = string("auth_id");
item=dict_get(query->object, &key);
if(item)
{
if(json_type(item)!=STRING)goto error;
auth_id=string_alloc(item->string_node.data, item->string_node.length);
if(!auth_id)goto error;
}

key = string("name");
item=dict_get(query->object, &key);
if(item)
{
if(json_type(item)!=STRING)goto error;
name=string_alloc(item->string_node.data, item->string_node.length);
if(!name)goto error;
}
else name = string_alloc(0, 0);

key = string("data");
item=dict_get(query->object, &key);
if(item)
{
if(json_type(item)!=STRING)goto error;
data=string_alloc(item->string_node.data, item->string_node.length);
if(!data)goto error;
}
else data = string_alloc(0, 0);

key = string("blocks");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=ARRAY)goto error;
array=&item->array_node;

if(!array)goto error;

	blocks_array=(struct blocks_array *)malloc(sizeof(struct blocks_array)+(sizeof(struct blocks *)*array->length));
	if(!blocks_array)return -1;
	memset(blocks_array,0,sizeof(struct blocks_array)+(sizeof(struct blocks *)*array->length));
    blocks_array->blocks_size=array->length;

	for(i = 0; i < array->length; ++i)
	{
		blocks_array->blocks[i]=(struct blocks *)malloc(sizeof(struct blocks));
		memset(blocks_array->blocks[i],0,sizeof(struct blocks));
		
		temp = vector_get(array, i);
		if(!temp)goto error;
		if(json_type(temp)!=OBJECT)goto error;
		
		key = string("block_id");
		item=dict_get(temp->object, &key);
		if(!item)goto error;
		if(json_type(item)!=INTEGER)goto error;
		blocks_array->blocks[i]->block_id=item->integer;
		
		key = string("path");
		item=dict_get(temp->object, &key);
		if(!item)goto error;
		if(json_type(item)!=STRING)goto error;
		blocks_array->blocks[i]->location=&item->string_node;
		if(!blocks_array->blocks[i]->location)goto error;
		
		//check the path and the block_id
		
		//TODO to check is it really necessary
		block=access_get_blocks(resources,blocks_array->blocks[i]->block_id);
		if(!block)goto error;

		path=access_fs_concat_path(block->location,blocks_array->blocks[i]->location,1);
		if(!path)goto error;

		if( stat( path->data, &attribute ) == -1 )
		{
		fprintf(stderr,"can't stat errno %d %s\n",errno,path->data);
		goto error;
		}

		free(path); path = 0;

		free(block);block=0;
		
	}// for(i = 0; i < array->length; ++i)
temp=0;



//function main
if(auth_id)
{
//TODO check the auth_id
if(!storage_auth_delete_value(resources->storage,auth_id))goto error;
}
else
{
	char *restrict id = session_id_alloc();
	if(!id)goto error;
	
	auth_id = string_alloc(id, CACHE_KEY_SIZE);
	free(id);
	if(!auth_id)goto error;
}

//function main
	key = string("user_id");
	item=dict_get(resources->session.ro->object, &key);
	if(!item)goto error;
	if(json_type(item)!=INTEGER)goto error;
json_serialized=storage_auth_add(resources->storage,auth_id,blocks_array,rw,count,item->integer,name,data);
if(!json_serialized)goto error;

//end of function main


if(!remote_json_send(request,response, resources, json_serialized))
{
//ERROR to return json
free(json_serialized);json_serialized=0;
free(auth_id);
blocks_array_free(blocks_array);
return -1;
}

free(json_serialized);
free(auth_id);
blocks_array_free(blocks_array);
return 0;



error: //ERROR tuka trqbva da vrushta json za greshka
blocks_array_free(blocks_array);
free(block);
free(auth_id);
json_serialized=string_alloc("{\"error\":\"1\"}", 13);
remote_json_send(request, response, resources, json_serialized);
free(json_serialized);
return 0;
}

int auth_remove(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : {"auth_id":string "locations":array}
*/
struct string *json_serialized=NULL;
union json *root,*item;
struct vector *array;
struct string key;
struct string *auth_id=0;
int i=0;

if(!session_is_logged_in(resources))goto error;
if(json_type(query)!=OBJECT)goto error;

key = string("auth_id");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;
auth_id=&item->string_node;
if(!auth_id)goto error;

key = string("locations");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=ARRAY)goto error;
array=&item->array_node;
if(!array)goto error;


for(i = 0; i < array->length; ++i)
	{
	item = vector_get(array, i);
	if(!item)goto error;
	if(json_type(item)!=INTEGER)goto error;
	
	if(!storage_auth_delete_location(resources->storage,auth_id,item->integer))goto error;
	}
//end of func main

root = json_object_old(false);
if(!root)goto error;
json_serialized=json_serialize(root);
if(!json_serialized)goto error;

if(!remote_json_send(request,response, resources, json_serialized))
{
//ERROR to return json
free(json_serialized);json_serialized=0;
return -1;
}

free(json_serialized);
return 0;

error: //ERROR tuka trqbva da vrushta json za greshka
json_serialized=string_alloc("{\"error\":\"1\"}", 13);
remote_json_send(request, response, resources, json_serialized);
free(json_serialized);
return 0;
}

int auth_revoke(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : {}
*/
struct string *json_serialized=NULL;
union json *root,*item;
struct string key;
struct string *auth_id=0;

if(!session_is_logged_in(resources))goto error;
if(json_type(query)!=OBJECT)goto error;

key = string("auth_id");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;
auth_id=&item->string_node;
if(!auth_id)goto error;



if(!storage_auth_delete_value(resources->storage,auth_id))goto error;
//end of func main

root = json_object_old(false);
if(!root)goto error;
json_serialized=json_serialize(root);
if(!json_serialized)goto error;

if(!remote_json_send(request,response, resources, json_serialized))
{
//ERROR to return json
free(json_serialized);json_serialized=0;
return -1;
}

free(json_serialized);
return 0;

error: //ERROR tuka trqbva da vrushta json za greshka
json_serialized=string_alloc("{\"error\":\"1\"}", 13);
remote_json_send(request, response, resources, json_serialized);
free(json_serialized);
return 0;
}

int auth_list(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : {}
*/
struct string *json_serialized=NULL;
union json *root=0,*item,*temp;
struct auth_array *auth_array=0;
struct string key;
int count=0;
int i=0;
int user_id=1;// TODO get the user_id from the session

if(!session_is_logged_in(resources))goto error;
//polzvam query za da vzema arguments
if(json_type(query)!=OBJECT)goto error;

/*
key = string("user_id");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=INTEGER)goto error;
user_id=item->integer;
*/



key = string("count");
item=dict_get(query->object, &key);
if(item)
{
if(json_type(item)!=INTEGER)goto error;
count=item->integer;
}

    key = string("user_id");
    item=dict_get(resources->session.ro->object, &key);
    if(!item)goto error;
    if(json_type(item)!=INTEGER)goto error;
	user_id=item->integer;

auth_array=storage_auth_list(resources->storage,user_id,count);
if(!auth_array)goto error;
//end of func main

root = json_array();
if(!root)goto error;

for(i=0;i<auth_array->count;i++)
	{
	if(!auth_array->auth[i])continue;
	//{auth_id string, rw int, count int, block_id int, path string},....
	item = json_object_old(false);
	

	temp = json_string_old(auth_array->auth[i]->auth_id);
	key = string("auth_id");
	json_object_insert_old(item, &key, temp);
	
	temp = json_string_old(auth_array->auth[i]->name);
	key = string("name");
	json_object_insert_old(item, &key, temp);
	
	temp = json_integer(auth_array->auth[i]->rw);
	key = string("rw");
	json_object_insert_old(item, &key, temp);
	
	temp = json_integer(auth_array->auth[i]->count);
	key = string("count");
	json_object_insert_old(item, &key, temp);
	
	temp = json_integer(auth_array->auth[i]->blocks_array->blocks[0]->block_id);
	key = string("block_id");
	json_object_insert_old(item, &key, temp);
	
	if(auth_array->auth[i]->blocks_array->blocks[0]->location)
	{
		temp = json_integer(auth_array->auth[i]->blocks_array->blocks[0]->location_id);
       	key = string("location_id");
		json_object_insert_old(item, &key, temp);

		temp = json_string_old(auth_array->auth[i]->blocks_array->blocks[0]->location);
		key = string("path");
		json_object_insert_old(item, &key, temp);
	}

	json_array_insert_old(root, item);

	}

	json_serialized=json_serialize(root);
	json_free(root);
	if(!json_serialized)goto error;

	if(!remote_json_send(request,response, resources, json_serialized))
	{
		//ERROR to return json

		if(auth_array)
		{
			for(i=0;i<auth_array->count;i++)
			{
			free(auth_array->auth[i]->auth_id);
			free(auth_array->auth[i]->blocks_array->blocks[0]->location);
			free(auth_array->auth[i]->blocks_array->blocks[0]);
			free(auth_array->auth[i]->blocks_array);
			free(auth_array->auth[i]);
			}
			free(auth_array);
		}

		free(json_serialized);json_serialized=0;
		return -1;
	}

	//Everything is fine, now is time to free the memory
	goto free;

error: //ERROR tuka trqbva da vrushta json za greshka
	json_serialized=string_alloc("{\"error\":\"1\"}", 13);
	remote_json_send(request, response, resources, json_serialized);

free:
	if(auth_array)
	{
		for(i=0;i<auth_array->count;i++)
		{
		free(auth_array->auth[i]->auth_id);
		free(auth_array->auth[i]->blocks_array->blocks[0]->location);
		free(auth_array->auth[i]->blocks_array->blocks[0]);
		free(auth_array->auth[i]->blocks_array);
		free(auth_array->auth[i]);
		}
	free(auth_array);
	}

	free(json_serialized);
	return 0;
}

struct host_port
{
	char *host;
	int port;
};

#if !defined(OS_IOS)
struct vector *filement_get_upnp_forwardings(void);
#endif

int socket_connect_non_block(const char *hostname, unsigned port)
{
#ifdef OS_WINDOWS
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0) return -1;
#endif

	int fd;
	int flags;
	struct addrinfo hints, *result = 0, *item;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	char buffer[6]; // TODO: don't hardcode this
	*format_uint(buffer, port) = 0;
	getaddrinfo(hostname, buffer, &hints, &result);

	// Cycle through the results until the socket is connected successfully
	for(item = result; item; item = item->ai_next)
	{
		fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
		if (fd < 0) continue;

#ifdef OS_BSD
		// Make the socket non-blocking for the connect() operation.
		flags = fcntl(fd, F_GETFL, 0);
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);

		// Connect to the server.
		if (!connect(fd, result->ai_addr, result->ai_addrlen)) goto success;
		else if (errno == EINPROGRESS)
		{
			 goto success;
		}
		close(fd);
#else
		u_long iMode = 1;
		ioctlsocket(fd, FIONBIO, &iMode);
		if (!connect(fd, result->ai_addr, result->ai_addrlen)) goto success;
		else
		{
		if(WSAGetLastError()==10035) goto success;
		fprintf(stderr,"The socket error is %d %d\n",WSAGetLastError(),fd);
		}
#endif
	}

	freeaddrinfo(result);
	return -1;

success:
	
#ifdef OS_BSD
	fcntl(fd, F_SETFL, flags);
#endif
	
	freeaddrinfo(result);
	return fd;
}

#if defined(DEVICE)
int session_upnp_nearby(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
#if !defined(OS_IOS)
/*
Request : {}
*/
struct string *json_serialized=NULL;
union json *root,*object,*temp;
struct string key,value;
int i=0;

if(!session_is_logged_in(resources))goto error;
if(json_type(query)!=OBJECT)goto error;



root = json_array();
if(!root)goto error;

struct vector *v=filement_get_upnp_forwardings();
struct host_port *hp=0;
struct pollfd *ufds;
char http_buf[1024];
int bytes_read=0;
int rv;
char *pch=0;
if(v)
{
ufds=malloc(sizeof(struct pollfd) * v->length);
	for(i=0;i<v->length;++i)
	{
		hp=(struct host_port *)vector_get(v,i);
		if(hp)
		{
		//ufds[i].fd
		//printf("connecting to %s:%d\n",hp->host,hp->port);
		ufds[i].fd = socket_connect_non_block(hp->host, hp->port);
		//ufds[i].fd=1;
		ufds[i].events = POLLIN | POLLOUT;
		ufds[i].revents = 0;
			
			
			free(hp);
		}
	}
	while(1)
	{
		rv = poll(ufds, v->length, 2000);
		
		if (rv == -1) {
		
		break;
		} else if (rv == 0) {
			//Timeout
			for(i=0;i<v->length;++i)
			{
			close(ufds[i].fd);
			ufds[i].fd=-1;
			}
			break;
		} else {
			for(i=0;i<v->length;++i)
			{
				if (ufds[i].fd!=-1)
				{
					// check for events on s1:
					if (ufds[i].revents & POLLIN) {
						bytes_read=read(ufds[i].fd, http_buf, 1023);
						//bytes_read=read(ufds[i].fd, http_buf, 1023);
						if(!bytes_read)
						{
						close(ufds[i].fd);
						ufds[i].fd=-1;
						}
						else if(bytes_read>0)
						{
						http_buf[bytes_read]=0;
						//check for UUID
						//printf("Response: %d %s\n",bytes_read ,http_buf);
						pch=strstr (http_buf,"\nUUID: ");
						if(pch)
						{
							
							for(i=0;*(pch+7+i)!='\r' && i<bytes_read;i++);
							
							if(i==UUID_LENGTH)
							{
								*(pch+7+i)='\0';
								//Success
								//printf("UUID in respose: %s\n",pch+7);
								hp=(struct host_port *)vector_get(v,i);
								if(!hp)goto error;
								object = json_object_old(false);
								if(!object)goto error;
								
								temp = json_integer(hp->port);
								if(!temp)goto error;
								key = string("port");
								json_object_insert_old(object, &key, temp);
								
								value = string(hp->host,strlen(hp->host));
								temp = json_string_old(&value);
								if(!temp)goto error;
								key = string("host");
								json_object_insert_old(object, &key, temp);
								
								value = string(pch+7,UUID_LENGTH);
								temp = json_string_old(&value);
								if(!temp)goto error;
								key = string("UUID");
								json_object_insert_old(object, &key, temp);
								
								
								json_array_insert_old(root,object);
							}
							
							
						}
						
						pch=0;
						}
					}
					else if (ufds[i].revents & POLLOUT)
					{
						write(ufds[i].fd,"GET / HTTP/1.1\r\nHost: Test\r\n\r\n",sizeof("GET / HTTP/1.1\r\nHost: Test\r\n\r\n")-1);
					}
					else if (ufds[i].revents & (POLLHUP | POLLERR)) {
						close(ufds[i].fd);
						ufds[i].fd=-1;
					}
				}
			}
		}
	
	}
	vector_term(v);
	free(v);
	free(ufds);
}

json_serialized=json_serialize(root);
if(!json_serialized)goto error;
json_free(root);
if(!remote_json_send(request,response, resources, json_serialized))
{
//ERROR to return json
free(json_serialized);json_serialized=0;
return -1;
}

free(json_serialized);
return 0;

error: //ERROR tuka trqbva da vrushta json za greshka
value=string("{\"error\":\"1\"}");
remote_json_send(request, response, resources, &value);
if(root)json_free(root);
return 0;
#else
	return ERROR_MISSING;
#endif
}
#endif /* DEVICE */
