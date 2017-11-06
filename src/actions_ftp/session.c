#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <curl/curl.h>

#ifdef OS_BSD
#include <sys/mman.h>
#include <arpa/inet.h>
#endif

#include "types.h"
#include "../storage.h"
#include "actions.h"
#include "../io.h"
#include "../ftp/curl_core.h"
#include "access.h"
#include "session.h"
#include "protocol.h"

#define json_free(json) do if (json) (json_free)(json); while (false)


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

int session_check(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *options)
{
//Request : {"username" = string ,"password" = string ,"host" = string ,"port" = int }

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

kernel.remotejson.response("1347971213468",{'status': "success",});
*/
union json *root, *temp;
struct string key, *json_serialized=NULL;
union json *item;
struct string buffer;
int local_errno=0;
//polu4avam user_id i session i vrushtam session_id ako e success
//predpolagame 4e e lognat
if(json_type(options)!=OBJECT)goto error;

key = string("username");
item=dict_get(options->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;
struct string *ftp_username=&item->string_node;
if(!ftp_username->length)goto error;

key = string("password");
item=dict_get(options->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;
struct string *ftp_password=&item->string_node;
if(!ftp_password->length)goto error;

key = string("host");
item=dict_get(options->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;
struct string *ftp_host=&item->string_node;
if(!ftp_host->length)goto error;
if(!access_check_host(ftp_host))goto error;

key = string("port");
item=dict_get(options->object, &key);
if(!item)goto error;
if(json_type(item)!=INTEGER)goto error;
int ftp_port=item->integer;
if(!ftp_port)goto error;

//main login part
CURL *curl=curl_easy_init();
if(!curl)goto error;
CURLcode res;
char *url=0;
struct buffer *header=buf_alloc();

url=(char *)malloc(sizeof(char)*(ftp_host->length+8));
sprintf(url,"ftp://%s/",ftp_host->data);


  if(curl) {
    /* Get a file listing from sunet */ 
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_PORT, ftp_port);
	curl_easy_setopt(curl, CURLOPT_USERNAME, ftp_username->data);
	curl_easy_setopt(curl, CURLOPT_PASSWORD, ftp_password->data);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20);

    
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, read_data);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, read_data);
    /* If you intend to use this on windows with a libcurl DLL, you must use
       CURLOPT_WRITEFUNCTION as well */ 
	   
    
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEHEADER, header);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    //curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "STAT");
	res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	curl=0;
    /* Check for errors */ 
	
    if(res != CURLE_OK)
		{
		fprintf(stderr, "curl_easy_perform() host:%s,port:%d failed: %s\n",ftp_host->data,ftp_port,curl_easy_strerror(res));
		buf_free(header);
		local_errno=2000+res;
		goto error;
		}
	
	buf_null_terminate(header);
	if(strstr((char *)header->p,"530"))
	{
		buf_free(header);
		local_errno=2000+res;
		goto error;
	}
	else if(!strstr((char *)header->p,"230"))
	{
		buf_free(header);
		local_errno=2000+res;
		goto error;
	}
	
	}
	else
	{ 
		buf_free(header);
		goto error;
	}
	
	
	curl_free(url);
	buf_free(header);
	
resources->handle=curl;


root = json_object_old(false);

temp = json_integer(1);
key = string("success");
json_object_insert_old(root, &key, temp);
json_serialized=json_serialize(root);
//json_free(root);root=NULL;temp=NULL;
if(!json_serialized)goto error;


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

return remote_json_error(request, response, resources, local_errno);
}

int session_login(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *options)
{
//Request : {"username" = string ,"password" = string ,"host" = string ,"port" = int }

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
union json *root, *temp;
struct string key, *json_serialized=NULL;
union json *item;
struct string buffer;
int local_errno=0;
//polu4avam user_id i session i vrushtam session_id ako e success
//predpolagame 4e e lognat
if(json_type(options)!=OBJECT)goto error;

key = string("username");
item=dict_get(options->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;
struct string *ftp_username=&item->string_node;
if(!ftp_username->length)goto error;

key = string("password");
item=dict_get(options->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;
struct string *ftp_password=&item->string_node;
if(!ftp_password->length)goto error;

key = string("host");
item=dict_get(options->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;
struct string *ftp_host=&item->string_node;
if(!ftp_host->length)goto error;
if(!access_check_host(ftp_host))goto error;

key = string("port");
item=dict_get(options->object, &key);
if(!item)goto error;
if(json_type(item)!=INTEGER)goto error;
int ftp_port=item->integer;
if(!ftp_port)goto error;

//main login part
CURL *curl=curl_easy_init();
if(!curl)goto error;
CURLcode res;
char *url=0;
struct buffer *header=buf_alloc();

url=(char *)malloc(sizeof(char)*(ftp_host->length+8));
sprintf(url,"ftp://%s/",ftp_host->data);

  if(curl) {
    /* Get a file listing from sunet */ 
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_PORT, ftp_port);
	curl_easy_setopt(curl, CURLOPT_USERNAME, ftp_username->data);
	curl_easy_setopt(curl, CURLOPT_PASSWORD, ftp_password->data);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20);
    
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, read_data);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, read_data);
    /* If you intend to use this on windows with a libcurl DLL, you must use
       CURLOPT_WRITEFUNCTION as well */ 

	curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEHEADER, header);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    //curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "STAT");
	res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
    /* Check for errors */ 

	if(res != CURLE_OK)
		{
		fprintf(stderr, "curl_easy_perform() host:%s,port:%d failed: %s\n",ftp_host->data,ftp_port,curl_easy_strerror(res));
		buf_free(header);
		local_errno=2000+res;
		goto error;
		}

	buf_null_terminate(header);
	if(strstr((char *)header->p,"530"))
	{
		buf_free(header);
		local_errno=2000+res;
		goto error;
	}
	else if(!strstr((char *)header->p,"230"))
	{
		buf_free(header);
		local_errno=2000+res;
		goto error;
	}
	
	}
	else
	{
		buf_free(header);
		goto error;
	}
	
	
	curl_free(url);
	buf_free(header);
	resources->handle=0;

	if (!session_create(resources)) return InternalServerError;

//end of main

root = json_object_old(false);

struct string session_id = string(resources->session_id);
temp = json_string_old(&session_id);
key = string("session_id");
json_object_insert_old(root, &key, temp);
json_serialized=json_serialize(root);
//json_free(root);root=NULL;temp=NULL;
if(!json_serialized)goto error;

if (json_type(resources->session.ro) == OBJECT)
{
	session_edit(resources);

	key = string("username");
	struct string session_username;
	string_init(&session_username, ftp_username->data, ftp_username->length);
	temp=json_string_old(&session_username);
	if(!temp)goto error;
	if (json_object_insert_old(resources->session.rw, &key, temp)) goto error;

	key = string("password");
	struct string session_password;
	string_init(&session_password, ftp_password->data, ftp_password->length);
	temp=json_string_old(&session_password);
	if(!temp)goto error;
	if (json_object_insert_old(resources->session.rw, &key, temp)) goto error;
	
	key = string("host");
	struct string session_host;
	string_init(&session_host, ftp_host->data, ftp_host->length);
	temp=json_string_old(&session_host);
	if(!temp)goto error;
	if (json_object_insert_old(resources->session.rw, &key, temp)) goto error;
	
	key = string("port");
	temp=json_integer(ftp_port);
	if(!temp)goto error;
	if (json_object_insert_old(resources->session.rw, &key, temp)) goto error;

	session_save(resources);
}
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

return remote_json_error(request, response, resources, local_errno);
}

int session_logout(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : {}
*/
struct string *json_serialized=NULL;
union json *root;

if(!resources->session_access)goto error;

//TODO MARTO da mi dade funkciq  za session_logout
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
return -1;
}

bool session_is_logged_in(struct resources *restrict resources)
{
const union json *session = resources->session.ro, *item;
struct string key;

if(!resources->session_access)return false;

if(json_type(resources->session.ro) != OBJECT)return false;

key = string("username");
item=dict_get(session->object, &key);
if(!item)return false;
if(json_type(item)!=STRING)return false;

key = string("host");
item=dict_get(session->object, &key);
if(!item)return false;
if(json_type(item)!=STRING)return false;

return true;
}



struct auth *remote_get_auth(const char *hostname,struct string *auth_key)
{
//currently the JSON response must be on 1 line, otherwise it will fail
//"error":1 on ERROR

//Example response:
//{"count":-1,"rw":1,"login":{"username":"test","password":"test","host":"ftp.example.com","port":21},"blocks":[{"block_id":1,"user_id":0,"location":"/test/","location_id":2214,"size":0},{},{}...] }

    int sock,n,len,i=0;
	struct string string_json;
	union json *json=0,*item,*temp=0;
	struct auth *auth=0;
	struct string key; 

	//TODO to not restrict the buffer to 4kb
    char buffer[4096];
	


	sock = socket_connect(hostname, 80);

	if(sock<0)return 0;

	n=sprintf(buffer,"GET /private/requests/sessions/getAuth.php?token=%s HTTP/1.1\r\nConnection: close\r\nHost: %s\r\n\r\n",auth_key->data,hostname);
		
	#ifdef OS_WINDOWS
	n = WRITEALL(sock,buffer,n);// TODO to check and loop if is not send in 1 portion
	#else
	n = writeall(sock,buffer,n);// TODO to check and loop if is not send in 1 portion
	#endif

    if (n < 0)return 0;
	
	//TODO to read more than one time if necessary
	n = socket_read(sock,buffer,4095);
	//perror(0);


    if (n < 0)return 0;
	#ifdef OS_WINDOWS
	CLOSE(sock);
	#else
	close(sock);
	#endif
    buffer[n]='\0';
	len=n;
	//TODO this limits the response content to not have \n
	for(;buffer[n]!='\n';n--)
		if(!n)return 0;
	
	string_json.data=buffer+n+1;
	string_json.length=len-n-1;
	
	json=json_parse(&string_json);
	if(!json)return 0;
	if(json_type(json)!=OBJECT)return 0;
	
	auth=(struct auth *)malloc(sizeof(struct auth));
	if(!auth)return 0;
	memset(auth,0,sizeof(struct auth));

	auth->auth_id=auth_key;

	key = string("count");
	item=dict_get(json->object, &key);
	if(!item)goto error;
	if(json_type(item)!=INTEGER)goto error;
	auth->count=item->integer;

	key = string("rw");
	item=dict_get(json->object, &key);
	if(!item)goto error;
	if(json_type(item)!=INTEGER)goto error;
	auth->rw=item->integer;	
	
	key = string("blocks");
	item=dict_get(json->object, &key);
	if(!item)goto error;
	if(json_type(item)!=ARRAY)goto error;
	struct vector *array=&item->array_node;
	if(!array)goto error;
	auth->blocks_array=(struct blocks_array *)malloc(sizeof(struct blocks_array)+(sizeof(struct blocks *)*array->length));
	if(!auth->blocks_array)return 0;
	int vlen=array->length;
	memset(auth->blocks_array,0,sizeof(struct blocks_array)+(sizeof(struct blocks *)* array->length));
	
	for(i = 0; i < array->length; ++i)
		{
			auth->blocks_array->blocks[i]=(struct blocks *)malloc(sizeof(struct blocks));
			memset(auth->blocks_array->blocks[i],0,sizeof(struct blocks));
			
			temp = vector_get(array, i);
			if(!temp)goto error;
			if(json_type(temp)!=OBJECT)goto error;
			
			key = string("block_id");
			item=dict_get(temp->object, &key);
			if(!item)goto error;
			if(json_type(item)!=INTEGER)goto error;
			auth->blocks_array->blocks[i]->block_id=item->integer;
			
			key = string("size");
			item=dict_get(temp->object, &key);
			if(!item)goto error;
			if(json_type(item)!=INTEGER)goto error;
			auth->blocks_array->blocks[i]->size=item->integer;
			
			key = string("location");
			item=dict_get(temp->object, &key);
			if(!item)goto error;
			if(json_type(item)!=STRING)goto error;
			auth->blocks_array->blocks[i]->location=string_alloc(item->string_node.data,item->string_node.length);
			if(!auth->blocks_array->blocks[i]->location)goto error;
			
			key = string("location_id");
			item=dict_get(temp->object, &key);
			if(!item)goto error;
			if(json_type(item)!=INTEGER)goto error;
			auth->blocks_array->blocks[i]->location_id=item->integer;
			
			key = string("user_id");
			item=dict_get(temp->object, &key);
			if(!item)goto error;
			if(json_type(item)!=INTEGER)goto error;
			auth->blocks_array->blocks[i]->user_id=item->integer;
			
			auth->blocks_array->blocks_size++;
		}// for(i = 0; i < array->length; ++i)
	temp=0;
	 
	key = string("login");
	item=dict_get(json->object, &key);
	if(!item)goto error;
	if(json_type(item)!=OBJECT)goto error;
	auth->login=json_object_old(false);
	//TODO check this code below
	struct string *tmp_string=0;
	union json *tmp_json=0;
		key = string("username");
		temp=dict_get(item->object, &key);
		if(!temp)goto error;
		if(json_type(temp)!=STRING)goto error;
		tmp_string=string_alloc(temp->string_node.data, temp->string_node.length);
		if(!tmp_string)goto error;
		tmp_json=json_string_old(tmp_string);
		if(!tmp_json)goto error;
		json_object_insert_old(auth->login, &key, tmp_json);
	
		key = string("password");
		temp=dict_get(item->object, &key);
		if(!temp)goto error;
		if(json_type(temp)!=STRING)goto error;
		tmp_string=string_alloc(temp->string_node.data, temp->string_node.length);
		if(!tmp_string)goto error;
		tmp_json=json_string_old(tmp_string);
		if(!tmp_json)goto error;
		json_object_insert_old(auth->login, &key, tmp_json);
	
		key = string("host");
		temp=dict_get(item->object, &key);
		if(!temp)goto error;
		if(json_type(temp)!=STRING)goto error;
		tmp_string=string_alloc(temp->string_node.data, temp->string_node.length);
		if(!tmp_string)goto error;
		if(!access_check_host(tmp_string))goto error;
		tmp_json=json_string_old(tmp_string);
		if(!tmp_json)goto error;
		json_object_insert_old(auth->login, &key, tmp_json);
		
		key = string("port");
		temp=dict_get(item->object, &key);
		if(!temp)goto error;
		if(json_type(temp)!=INTEGER)goto error;
		tmp_json=json_integer(temp->integer);
		if(!tmp_json)goto error;
		json_object_insert_old(auth->login, &key, tmp_json);
	 
	
	json_free(json);
	return auth;
	
	error:
	//TODO possible memory leak
	//TODO to free the blocks before auth
	json_free(auth->login);
	free(auth); 
	json_free(json);
	return 0;
}

bool auth_id_check(struct resources *restrict resources)
{
union json *item;
struct string key;
struct auth *auth;

if(resources->auth_id)
	{
	resources->auth=remote_get_auth(HOST_DISTRIBUTE_REMOTE,resources->auth_id);
	if(auth)return true;
	}
	
return false;
}

union json *config_info_json(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query, int *local_errno)
{
union json *root,*temp;
struct string key;

root = json_object_old(false);
if(!root)return 0;


temp = fs_get_blocks_json(request,response,resources,query,local_errno);
if(!temp){json_free(root);return 0;}
key = string("fs.get_blocks");
json_object_insert_old(root, &key, temp);

temp = session_actions_json(request,response,resources,query,local_errno);
if(!temp){json_free(root);return 0;}
key = string("session.actions");
json_object_insert_old(root, &key, temp);

return root;
}

int config_info(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
//TODO possible memory leak with json on error
/*
Response : {" "port":222,"ips":[]}
*/
struct string *json_serialized=NULL;
union json *root;
int local_errno=1;

if(resources->auth_id)auth_id_check(resources);
if(!resources->auth && !session_is_logged_in(resources)) return Forbidden;

root=config_info_json(request,response,resources,query,&local_errno);
if(!root)goto error;
json_serialized=json_serialize(root);
json_free(root);
if(!json_serialized){goto error;}

if(!remote_json_send(request,response, resources, json_serialized))
{
//ERROR to return json
free(json_serialized);json_serialized=0;
return -1;
}

free(json_serialized);json_serialized=0;
return 0;

error:
return remote_json_error(request, response, resources, local_errno);
}

/*

int auth_grant(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{

//Request : {"user_id" = int ,"block_id" = int ,"count" = int ,"rw" = int  , "path" = string}

struct string *json_serialized=NULL;
union json *root, *temp, *item;

int user_id=0;
int block_id=0;
int count=0;
int rw=0;
struct string *location=0;
struct string *auth_id=0;

struct blocks *block=0;
struct string *path=0;
struct string key;

struct stat attribute;

//firstly get the block_id from the request
if(!session_is_logged_in(resources))goto error;
//polzvam query za da vzema arguments
if(json_type(query)!=OBJECT)goto error;

key = string("user_id");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=INTEGER)goto error;
user_id=item->integer;


key = string("block_id");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=INTEGER)goto error;
block_id=item->integer;

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

key = string("path");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;
location=&item->string_node;
if(!location)goto error;
//function main

auth_id=id_alloc(0);
if(!auth_id)goto error;

//path security check
//TODO to check is it really necessary
block=storage_blocks_get_by_block_id(resources->storage,block_id);
if(!block)goto error;

path=fs_get_path(block,location);
if(!path)goto error;

if( stat( path->data, &attribute ) == -1 )
{
fprintf(stderr,"can't stat errno %d %s\n",errno,path->data);
goto error;
}
//function main


if(!storage_auth_add(resources->storage,auth_id,user_id,block_id,location,rw,count ))goto error;

//end of func main

root = json_object_old(false);
if(!root)goto error;

temp = json_string_old(auth_id);
key = string("auth_id");
json_object_insert_old(root, &key, temp);
json_serialized=json_serialize(root);
free(auth_id);

if(!json_serialized)goto error;

if(!remote_json_send(request,response, resources, json_serialized))
{
//ERROR to return json
free(json_serialized);json_serialized=0;
goto error;
}

free(json_serialized);
return OK;



error: //ERROR tuka trqbva da vrushta json za greshka
free(auth_id);
json_serialized=string_alloc("{\"error\":\"1\"}", 13);
remote_json_send(request, response, resources, json_serialized);
free(json_serialized);
return OK;
}

*/
