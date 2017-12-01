#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if !defined(OS_WINDOWS)
# include <sys/mman.h>
# include <sys/socket.h>
#else
#define WINVER 0x0501
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include "mingw.h"
#endif

#include "types.h"
#include "format.h"
#include "aes.h"
#include "filement.h"

#if !defined(OS_WINDOWS)
# include <arpa/inet.h>
# include <netdb.h>
# ifdef OS_ANDROID
#  include "ifaddrs.h"
# else
#  include <ifaddrs.h>
# endif

# include "actions.h"
# include "protocol.h"
# include "access.h"
# include "storage.h"
# include "io.h"
# include "dlna/ssdp.h"

# if defined(FILEMENT_UPNP)
#  include "miniupnpc_filement.h"
# endif
#elif defined(OS_WINDOWS)
# include "../miniupnpc_filement.h"
# include "../actions.h"
# include "../protocol.h"
# include "../access.h"
# include "../storage.h"
# include "../io.h"
# include "../dlna/ssdp.h"
#endif

#include "config.h"
#include "session.h"

extern int fs_get_blocks(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query);

int dlna_change_availability(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : { INTEGER }
*/
struct string *json_serialized=NULL,username;
union json *temp, *item;
struct string key,*hash=0,*newhash=0;
int local_errno=0;

if(!session_is_logged_in(resources)){local_errno=1001;goto error;}
if(json_type(query)!=INTEGER)goto error;

if(query->integer)DLNAEnable(resources->storage);
else DLNADisable(resources->storage);

json_serialized=string_alloc("{}", 2);
remote_json_send(request, response, resources, json_serialized);
free(json_serialized);

return 0;
error: //ERROR tuka trqbva da vrushta json za greshka
return remote_json_error(request, response, resources, local_errno);

}

int config_change_password(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : { "hash":string, "newhash":string }
*/
struct string *json_serialized=NULL,username;
union json *temp, *item;
struct string key,*hash=0,*newhash=0;
int local_errno=0;

if(!session_is_logged_in(resources)){local_errno=1001;goto error;}
if(json_type(query)!=OBJECT)goto error;

	key = string("hash");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != STRING)) goto error;
	hash = &item->string_node;
	if ((hash->length % (128 * 2 / 8)) || (hash->length > 64 * 2)) goto error; // AES-encrypted value's length must be a multiple of AES block size
	
	key = string("newhash");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != STRING)) goto error;
	newhash = &item->string_node;
	if ((newhash->length % (128 * 2 / 8)) || (newhash->length > 64 * 2)) goto error; // AES-encrypted value's length must be a multiple of AES block size

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
	struct string password = string(data, strlen(data)); // TODO: data length is not available after AES decrypt. fix this somehow
	if(!storage_local_users_get_id(resources->storage, &username, &password)){local_errno=1015;goto error;}

	
	for(index = 0; index < newhash->length / 2; index += 16)
	{
		hex2bin(chunk, newhash->data + index * 2, 32);
		aes_decrypt(chunk, data + index, &context);
	}
	 password = string(data, strlen(data));
	if(!storage_local_users_set(resources->storage, &username, &password)){local_errno=1085;goto error;}
	
json_serialized=string_alloc("{}", 2);
remote_json_send(request, response, resources, json_serialized);
free(json_serialized);

return 0;
error: //ERROR tuka trqbva da vrushta json za greshka
return remote_json_error(request, response, resources, local_errno);

}

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
	bool initialized;

	//firstly get the block_id from the request
	if(!session_is_logged_in(resources)){local_errno=1001;goto error;}
	//polzvam query za da vzema arguments
	if(json_type(query)!=OBJECT)goto error;

	key = string("blocks_initialized");
	tmp = storage_local_settings_get_value(resources->storage, &key);
	initialized = (tmp != 0);
	free(tmp);
	if (initialized && !access_is_session_granted(resources)) {local_errno=1026;goto error;}

	storage_blocks_truncate(resources->storage);

	key = string("location");

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

	if (!initialized)
	{
		key = string("blocks_initialized");
		value = string("1");
		if (!storage_local_settings_set_value(resources->storage, &key, &value)) goto error;
		//TODO if this fails, it won't ask for grant next time
	}

	return fs_get_blocks(request,response,resources,query);
	//tuka trqbva da vrushta json greshka
error:
	return remote_json_error(request, response, resources, local_errno);
}

int config_blocks_add(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	/*
	Response : [{"location"="/"},{},{}]
	*/
	union json *block_object, *location;
	struct string buffer;
	struct string key, value;
	int local_errno=0;

	//firstly get the block_id from the request
	if(!session_is_logged_in(resources)){local_errno=1001;goto error;}
	//polzvam query za da vzema arguments
	if(json_type(query)!=OBJECT)goto error;

	if (!access_is_session_granted(resources)) {local_errno=1026;goto error;}

	key = string("location");

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

	return fs_get_blocks(request,response,resources,query);
	//tuka trqbva da vrushta json greshka
error:
	return remote_json_error(request, response, resources, local_errno);
}

int config_blocks_del(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	/*
	Response : [23,41,65,22,11]
	*/
	int local_errno=0;
	const struct vector *list=0;

	//firstly get the block_id from the request
	if(!session_is_logged_in(resources)){local_errno=1001;goto error;}
	//polzvam query za da vzema arguments
	
	if(json_type(query)!=ARRAY)goto error;
	if (!access_is_session_granted(resources)) {local_errno=1026;goto error;}
	list=&query->array_node;
	
	int i=0;
	for(i = 0; i < list->length; ++i)
		{
		if (json_type((union json *)list->data[i]) != INTEGER)continue;
		
		storage_blocks_del(resources->storage,1,((union json *)list->data[i])->integer);
		}
	

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

key = string("authenticated");
if(resources->session_access && json_type(resources->session.ro)==OBJECT)
{
temp=dict_get(resources->session.ro->object, &key);
	if(temp)
	{
		if(json_type(temp)==INTEGER && temp->integer)json_object_insert_old(root, &key, json_integer(1));
		else json_object_insert_old(root, &key, json_integer(0));
	}
	else
	{
	json_object_insert_old(root, &key, json_integer(0));
	}
}

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

key = string("dlna_availability");
json_object_insert_old(root, &key, json_integer(DLNAisEnabled()));

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

#ifdef DEVICE
#ifdef OS_WINDOWS
extern struct string UUID_WINDOWS;
#else
extern struct string UUID;
#endif

int config_register(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Response : {"token":"...", "email":"..."}
*/
union json *password, *email, *devname;
struct string *json_serialized=0;
struct string buffer;
struct string key;
int local_errno=1;
uint32_t passport_id=0;
char sprintf_buf[256];
char *newuuid=0; 
struct string value;
struct string default_location=string("/tmp/mnt/shared/");

struct string UUID_TMP=string("00000000000000000000000000000000");
#ifdef OS_WINDOWS
if(!string_equal(&UUID_WINDOWS,&UUID_TMP))
#else
if(!string_equal(&UUID,&UUID_TMP))
#endif
{
local_errno=598;goto error; 
}

if(json_type(query)!=OBJECT)goto error;
	key = string("password");
	password=dict_get(query->object, &key);
	if(!password)goto error;
	if(json_type(password)!=STRING)goto error;

	key = string("email");
	email=dict_get(query->object, &key);
	if(!email)goto error;
	if(json_type(email)!=STRING)goto error;
	
	key = string("devname");
	devname=dict_get(query->object, &key);
	if(!devname)goto error;
	if(json_type(devname)!=STRING)goto error;
	

if(!filement_register(&email->string_node, &devname->string_node, &password->string_node))
{
	local_errno=599;goto error;
}
else
{
	filement_start();
}

#ifdef OS_WINDOWS
	json_serialized=json_serialize(json_string_old(&UUID_WINDOWS)); //TODO memory leak from json_string
#else
	json_serialized=json_serialize(json_string_old(&UUID)); //TODO memory leak from json_string
#endif
if(!json_serialized){goto error;}
if(!remote_json_send(request,response, resources, json_serialized))
{
//ERROR to return json
free(json_serialized);json_serialized=0;
goto error;
}

free(json_serialized);json_serialized=0;
return 0;
//tuka trqbva da vrushta json greshka
error:
return remote_json_error(request, response, resources, local_errno);
}

#endif

#if !defined(OS_WINDOWS)
union json *config_get_interfaces_json(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query, int *local_errno)
{
union json *root,*temp,*temp2,*array;
struct string key,*str_ptr=0;
struct ifaddrs *ifaddr=0, *ifa;
int family, s;
char host[NI_MAXHOST];
int i=0;

if(resources->auth_id)auth_id_check(resources);
if(!resources->auth && !session_is_logged_in(resources)){*local_errno=1001;return 0;}


           if (getifaddrs(&ifaddr) == -1) {
               return 0;
           }

char *f_ip=0;
int f_port[4];
int f_count=4;
# if defined(FILEMENT_UPNP)
filement_get_upnp_forwarding(&f_ip,f_port,&f_count);
# endif

root = json_object_old(false);
if(!root)return 0;

if(f_ip && ( f_port[0] || f_port[1] || f_port[2] || f_port[3] ) )//TEST
{

str_ptr=string_alloc(f_ip,strlen(f_ip));
if(!str_ptr){json_free(root);return 0;}

array = json_array();
if(!array){return 0;}

	for(i=0;i<4;i++)
	{
		if(f_port[i])
		{
		temp2 = json_integer(f_port[i]);
		json_array_insert_old(array, temp2);
		}

	}
json_object_insert_old(root, str_ptr, array);

free(str_ptr);
//json_free(array);
free(f_ip);
}

/*
temp = json_integer(g_listening_port);
if(!temp){json_free(root);return 0;}
key = string("port");
json_object_insert_old(root, &key, temp);

array = json_array();
if(!array){json_free(root);return 0;}
*/
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
			   {freeifaddrs(ifaddr);json_free(root);return 0;}
		   }
		   str_ptr=string_alloc(host,strlen(host));
		   if(!str_ptr){freeifaddrs(ifaddr);json_free(root);return 0;}

		   array = json_array();
			if(!array){freeifaddrs(ifaddr);json_free(root);return 0;}
			
			for(i=0;i<PORT_DIFF;i++)
			{
				temp2 = json_integer(PORT_HTTP_MIN + i);
				json_array_insert_old(array, temp2); //TODO free temp2?
			}
			
		   json_object_insert_old(root, str_ptr, array);
		   free(str_ptr);
		   //json_free(array);
	   }
   }

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
return -1;
}

free(json_serialized);json_serialized=0;
return 0;

error:
return remote_json_error(request, response, resources, local_errno);
}
#else




union json *config_get_interfaces_json(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query, int *local_errno)
{
union json *root=0,*temp=0,*temp2=0,*array=0;
struct string key,*str_ptr=0;
struct ifaddrs *ifaddr=0, *ifa;



if(resources->auth_id)auth_id_check(resources);
if(!resources->auth && !session_is_logged_in(resources)){*local_errno=1001;return 0;}

	char str_buffer[128];
    /* Declare and initialize variables */
	  WSADATA wsaData = {0};
    int iResult = 0;
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        return 0;
    }
	
    DWORD dwSize = 0;
    DWORD dwRetVal = 0;

    unsigned int i = 0;


    // default to unspecified address family (both)
    ULONG family = AF_UNSPEC;

    PIP_ADAPTER_ADDRESSES pAddresses = NULL;
    ULONG outBufLen = 0;
    ULONG Iterations = 0;

    PIP_ADAPTER_ADDRESSES pCurrAddresses = NULL;
    PIP_ADAPTER_UNICAST_ADDRESS pUnicast = NULL;
  

    // Allocate a 15 KB buffer to start with.
    outBufLen = 15000;
	
/*
temp = json_integer(g_listening_port);
if(!temp){json_free(root);return 0;}
key = string("port");
json_object_insert_old(root, &key, temp);
*/


char *f_ip=0;
int f_port[4];
int f_count=4;
filement_get_upnp_forwarding(&f_ip,f_port,&f_count);

root = json_object_old(false);
if(!root)return 0;

if(f_ip && ( f_port[0] || f_port[1] || f_port[2] || f_port[3] ) )//TEST
{

str_ptr=string_alloc(f_ip,strlen(f_ip));
if(!str_ptr){json_free(root);return 0;}

array = json_array();
if(!array){return 0;}

	for(i=0;i<4;i++)
	{
		if(f_port[i])
		{
		temp2 = json_integer(f_port[i]);
		json_array_insert_old(array, temp2);
		}

	}
json_object_insert_old(root, str_ptr, array);

free(str_ptr);
//json_free(array);
free(f_ip);
}

////////////

   do {

        pAddresses = (IP_ADAPTER_ADDRESSES *) malloc(outBufLen);
        if (pAddresses == NULL) {
           return 0;
        }

        dwRetVal =
            GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | 
        GAA_FLAG_SKIP_MULTICAST | 
        GAA_FLAG_SKIP_DNS_SERVER |
        GAA_FLAG_SKIP_FRIENDLY_NAME, NULL, pAddresses, &outBufLen);

        if (dwRetVal == ERROR_BUFFER_OVERFLOW) {
            free(pAddresses);
            pAddresses = NULL;
        } else {
            break;
        }

        Iterations++;

    } while ((dwRetVal == ERROR_BUFFER_OVERFLOW) && (Iterations < 4));
	
	    if (dwRetVal == NO_ERROR) {
        // If successful, output some information from the data we received
        pCurrAddresses = pAddresses;
        while (pCurrAddresses) {

            pUnicast = pCurrAddresses->FirstUnicastAddress;
            if (pUnicast != NULL) {
                for (i = 0; pUnicast != NULL; i++)
				{
					family = pUnicast->Address.lpSockaddr->sa_family;
					memset(str_buffer,0,128);
					inet_ntop(family, &((struct sockaddr_in *)pUnicast->Address.lpSockaddr)->sin_addr, str_buffer, 128);
					
	
					str_ptr=string_alloc(str_buffer,strlen(str_buffer));
				   if(!str_ptr)
				   {
				   if(root)json_free(root);

				   if (pAddresses) {
					free(pAddresses);
					}
				   return 0;
				   }
				  
				  
					array = json_array();
					if(!array){return 0;}
					
					for(i=0;i<PORT_DIFF;i++)
					{
						temp2 = json_integer(PORT_HTTP_MIN + i);
						json_array_insert_old(array, temp2);
					}
					
				   json_object_insert_old(root, str_ptr, array);
				   //json_free(array);
				   
				   
                    pUnicast = pUnicast->Next;
				}
            }

				

            pCurrAddresses = pCurrAddresses->Next;
        }
    } else {
		if(root)json_free(root);

		if (pAddresses) {
					free(pAddresses);
					}
		return 0;
                 
    }

	if (pAddresses) {
					free(pAddresses);
					}
/////////////


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
return -1;
}

free(json_serialized);json_serialized=0;
return 0;

error:
return remote_json_error(request, response, resources, local_errno);
}
#endif

