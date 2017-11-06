#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef OS_BSD
# include <arpa/inet.h>
# include <ifaddrs.h>
# include <netdb.h>
# include <sys/mman.h>
# include <sys/socket.h>
#else
#define WINVER 0x0501
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "mingw.h"
#endif

#include "types.h"

#ifdef OS_BSD
# include "protocol.h"
# include "actions.h"
# include "access.h"
# include "storage.h"
# include "io.h"
#else
# include "../actions.h"
# include "../access.h"
# include "../storage.h"
# include "../io.h"
#endif

#include "config.h"
#include "session.h"

extern int fs_get_blocks(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query);

int config_blocks(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Response : [{"location"="/"},{},{}]
*/
union json *block_object, *location;
struct string buffer;
struct string key, value;
char *tmp=0;
int local_errno=0;

//firstly get the block_id from the request
if(!session_is_logged_in(resources)){local_errno=1001;goto error;}
//polzvam query za da vzema arguments
if(json_type(query)!=OBJECT)goto error;

key = string("blocks_initialized");
tmp=storage_local_settings_get_value(resources->storage,&key);
if((!tmp || strcmp(tmp,"0")) && !access_is_session_granted(resources) ){local_errno=1026;goto error;}



key = string("location");

storage_blocks_truncate(resources->storage);

struct dict_iterator it;
const struct dict_item *item;
for(item = dict_first(&it, query->object); item; item = dict_next(&it, query->object))
{
	block_object = item->value;
	if (json_type(block_object) != OBJECT)continue;

	location=dict_get(block_object->object, &key);
	if (!location || (json_type(location) != STRING))continue;

	storage_blocks_add(resources->storage,0,1,&location->string_node,0);
}

//TODO if blocks are initialized, I don't need to se them to "1" everytime
key = string("blocks_initialized");
value = string("1");
if (!storage_local_settings_set_value(resources->storage, &key, &value)) goto error;
//TODO if this fails, it won't ask for grant next time

return fs_get_blocks(request,response,resources,query);
//tuka trqbva da vrushta json greshka
error:
return remote_json_error(request, response, resources, local_errno);
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

temp = config_get_interfaces_json(request,response,resources,query,local_errno);
if(!temp){json_free(root);return 0;}
key = string("config.get_interfaces");
json_object_insert_old(root, &key, temp);

extern struct string SERVER;
key = string("version");
json_object_insert_old(root, &key, json_string_old(&SERVER));

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
int local_errno=0;

if(resources->auth_id)auth_id_check(resources);
if(!resources->auth && !session_is_logged_in(resources)){local_errno=1001;return Forbidden;}

root=config_info_json(request,response,resources,query,&local_errno);
if(!root)goto error;
json_serialized=json_serialize(root);
if(!json_serialized){if(root)json_free(root);goto error;}
json_free(root);root=0;

if(!remote_json_send(request,response, resources, json_serialized))
{
//ERROR to return json
free(json_serialized);json_serialized=0;
goto error;
}

free(json_serialized);json_serialized=0;
return 0;

error:
return remote_json_error(request, response, resources, local_errno);
}

#ifdef OS_BSD
union json *config_get_interfaces_json(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query, int *local_errno)
{
union json *root,*temp,*array;
struct string key,*str_ptr=0;
struct ifaddrs *ifaddr=0, *ifa;
int family, s;
char host[NI_MAXHOST];


if(resources->auth_id)auth_id_check(resources);
if(!resources->auth && !session_is_logged_in(resources)){*local_errno=1001;return 0;}


           if (getifaddrs(&ifaddr) == -1) {
               return 0;
           }

root = json_object_old(false);
if(!root)return 0;
temp = json_integer(PORT_HTTP_MIN);
if(!temp){json_free(root);return 0;}
key = string("port");
json_object_insert_old(root, &key, temp);

array = json_array();
if(!array){json_free(root);return 0;}

           for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
               if (ifa->ifa_addr == NULL)
                   continue;

               family = ifa->ifa_addr->sa_family;

               if (family == AF_INET || family == AF_INET6) {
                   s = getnameinfo(ifa->ifa_addr,
                           (family == AF_INET) ? sizeof(struct sockaddr_in) :
                                                 sizeof(struct sockaddr_in6),
                           host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
                   if (s != 0) {
                       {freeifaddrs(ifaddr);return 0;}
                   }
				   str_ptr=string_alloc(host,strlen(host));
				   if(!str_ptr){freeifaddrs(ifaddr);return 0;}
				   temp = json_string_old(str_ptr);
				   if(!temp){freeifaddrs(ifaddr);return 0;}
				   json_array_insert_old(array, temp);
               }
           }

key = string("ips");
json_object_insert_old(root, &key, array);
if(ifaddr)freeifaddrs(ifaddr);
return root;
}

int config_get_interfaces(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
//TODO possible memory leak with json on error
/*
Response : {" "port":222,"ips":[]}
*/
struct string *json_serialized=NULL;
union json *root;
int local_errno=0;

root=config_get_interfaces_json(request,response,resources,query,&local_errno);
if(!root)goto error;
json_serialized=json_serialize(root);
if(!json_serialized){if(root)json_free(root);goto error;}
json_free(root);root=0;

if(!remote_json_send(request,response, resources, json_serialized))
{
//ERROR to return json
free(json_serialized);json_serialized=0;
goto error;
}

free(json_serialized);json_serialized=0;
return 0;

error:
return remote_json_error(request, response, resources, local_errno);
}
#else
int config_get_interfaces(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
struct string *json_serialized=NULL;
union json *root;
	
	root = json_object_old(true);
	if(!root)goto error;
	json_serialized=json_serialize(root);
	if(!json_serialized)goto error;

	if(!remote_json_send(request,response, resources, json_serialized))
	{
	free(json_serialized);json_serialized=0;
	}
	
	return -1;
}
#endif

