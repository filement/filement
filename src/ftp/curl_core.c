#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <curl/curl.h>

#include "types.h"
#include "json.h"
#include "curl_core.h"

struct buffer * buf_alloc()
{
struct buffer * buf=(struct buffer *)malloc(sizeof(struct buffer ));
    buf->p = NULL;
    buf->begin_offset = 0;
    buf->len = 0;
    buf->size = 0;

return buf;
}

void buf_free(struct buffer* buf)
{
	if(buf)
	{
    free(buf->p);
	free(buf);
	}
}

int buf_resize(struct buffer *buf, size_t len)
{
    buf->size = (buf->len + len + 63) & ~31;
    buf->p = (uint8_t *) realloc(buf->p, buf->size);
    if (!buf->p) return -1;
    
    return 0;
}

int buf_add_mem(struct buffer *buf, const void *data, size_t len)
{
    if (buf->len + len > buf->size && buf_resize(buf, len) == -1)
        return -1;

    memcpy(buf->p + buf->len, data, len);
    buf->len += len;
    return 0;
}

void buf_null_terminate(struct buffer *buf)
{
    if (buf_add_mem(buf, "\0", 1) == -1)
        exit(1);
}
 
 size_t null_write(void *buffer, size_t size, size_t nmemb, void *arg) {
 return 0;
 }
 
size_t read_data(void *ptr, size_t size, size_t nmemb, void *data) {
  struct buffer* buf = (struct buffer*)data;
  if (buf == NULL) return size * nmemb;
  if (buf_add_mem(buf, ptr, size * nmemb) == -1)
    return 0;

  return size * nmemb;
}

int curl_set_login(CURL * curl,const union json *session)
{
union json *item;
struct string key;

if(session && json_type(session)!=OBJECT)return false;

key = string("username");
item=dict_get(session->object, &key);
if(!item)return false;
if(json_type(item)!=STRING)return false;
curl_easy_setopt(curl, CURLOPT_USERNAME, item->string_node.data);

key = string("password");
item=dict_get(session->object, &key);
if(!item)return false;
if(json_type(item)!=STRING)return false;
curl_easy_setopt(curl, CURLOPT_PASSWORD, item->string_node.data);

key = string("port");
item=dict_get(session->object, &key);
if(!item)return false;
if(json_type(item)!=INTEGER)return false;
curl_easy_setopt(curl, CURLOPT_PORT, item->integer);

curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
	
curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20);

return true;
}

void replace_2F(char *data,int length)
{
int i=0;
int c=0;

for(i=0,c=0;i<length;i++,c++)
{
	if(i+2<length && data[i]=='%' && data[i+1]=='2' && ( data[i+2]=='F' || data[i+2]=='f'))
	{
		i+=3;
		data[c++] = '/';
	}
	data[c]=data[i];
}
data[c] = 0;

}
