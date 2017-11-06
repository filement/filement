#define _XOPEN_SOURCE

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <curl/curl.h>
#include <time.h>

#ifdef OS_BSD
#include <sys/mman.h>
#include <arpa/inet.h>
#endif

#include "types.h"
#include "../storage.h"
#include "actions.h"
#include "../io.h"
#include "../ftp/curl_core.h"
#include "../ftp/upload.h"
#include "session.h"
#include "status.h"
#include "access.h"
 
#define MAGIC_DB "/home/nikov/magic/magic.mgc"

// TODO: distinguish between invalid path and memory error
static struct string *content_disposition(const struct string *path)
{
	ssize_t index;
	size_t length;
	struct string *result;

	for(index = path->length - 1; index >= 0; --index)
#ifdef OS_WINDOWS
		if (path->data[index] == '\\')
#else
		if (path->data[index] == '/')
#endif
			goto finally;

	return 0;

finally:

	// TODO: support quotes in filename
	#define DISPOSITION "attachment; filename="

	length = sizeof(DISPOSITION) - 1 + 1 + path->length - index - 1 + 1;

	result = malloc(sizeof(struct string) + sizeof(char) * (length + 1));
	if (!result) return 0;
	result->data = (char *)(result + 1);
	result->length = length;

	sprintf(result->data, DISPOSITION "\"%s\"", path->data + index + 1);
	result->data[length] = 0;

	#undef DISPOSITION

	return result;
}

union json *fs_get_blocks_json(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query,int *local_errno)
{
union json *root, *temp, *array,*tmp_object;
struct blocks_array *blocks_array = 0;
struct string key;
int i;

if(resources->auth_id)auth_id_check(resources);
if(!resources->auth && !session_is_logged_in(resources)){*local_errno=1001;goto error;}

blocks_array = access_get_blocks_array(resources);
if(!blocks_array)goto error;

root = json_object_old(false);
array = json_array();
if(!root || !array)goto error;

for(i=0;i<blocks_array->blocks_size;i++)
	{
	tmp_object=json_object_old(false);
	
	temp=json_integer(blocks_array->blocks[i]->block_id);
	key = string("block_id");
	if(json_object_insert_old(tmp_object, &key, temp))goto error;
	
	temp=json_integer(blocks_array->blocks[i]->user_id);
	key = string("user_id");
	if(json_object_insert_old(tmp_object, &key, temp))goto error;

	temp=json_integer(blocks_array->blocks[i]->size);
	key = string("size");
	if(json_object_insert_old(tmp_object, &key, temp))goto error;
	
	temp=json_integer(blocks_array->blocks[i]->size);
	key = string("bsize");
	if(json_object_insert_old(tmp_object, &key, temp))goto error;
	
	temp=json_string_old(blocks_array->blocks[i]->location);
	key = string("location");
	if(json_object_insert_old(tmp_object, &key, temp))goto error;
	
	json_array_insert_old(array, tmp_object);
	}
	
key = string("blocks");
if(json_object_insert_old(root, &key, array))goto error;
free_blocks_array(blocks_array);blocks_array=NULL;

return root;

error: //ERROR tuka trqbva da vrushta json za greshka
if(blocks_array)free_blocks_array(blocks_array);
return 0;
}

int fs_get_blocks(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
//funkciqta trqbva da izprati pri pravilen login
/*
Request : {}
Response pri OK:

kernel.remotejson.response("1347971213468",{'blocks': [
{},{...}...
]});
*/
union json *root;
struct string *json_serialized=NULL;
int local_errno = 1035;

root = fs_get_blocks_json(request,response,resources,query,&local_errno);
if (!root) return remote_json_error(request, response, resources, local_errno);

json_serialized=json_serialize(root);

if(!remote_json_send(request, response, resources, json_serialized))return -1;
return 0;
}

struct string *fs_stat_to_string_fake(char *d_name,int level, bool type)
{
	struct string *buffer=0,*serialized_name=0;
	size_t len=0;
	time_t current_time;
	current_time = time (NULL);

	len+=24; // size + current_time *2	
	len+=integer_digits(level, 10);
	len+=strlen(d_name);
	len+=12;//spaces,type,\t

	buffer=string_alloc(0, len);
	if(!buffer)return 0;

	if( type )
	{
		if(sprintf(buffer->data,"%d d 4096 %u %u %s\\u0000",level,(unsigned)current_time,(unsigned)current_time, d_name)<0)
			return 0;
	}
	else
	{
		if(sprintf(buffer->data,"%d f 0 %u %u %s\\u0000",level,(unsigned)current_time,(unsigned)current_time, d_name)<0)
			return 0;
	}

	return buffer;
}

struct string *fs_stat_to_string_json_fake(char *d_name,int level, bool type)
{
struct string *buffer=0,*serialized_name=0;
size_t len=0;
 time_t current_time;
 current_time = time (NULL);

len+=24; // size + current_time *2	
len+=integer_digits(level, 10);
serialized_name = string_serialize(d_name, strlen(d_name));
len+=serialized_name->length;
len+=7;//spaces,type,\t


buffer=string_alloc(0, len);
if(!buffer)return 0;
 
	if( type )
			{
			
			if(sprintf(buffer->data,"%d d %jd %u %u %s%c",level,(intmax_t)4096,(unsigned)current_time,(unsigned)current_time,serialized_name->data,'\0')<0)
				return 0;
			}
	else
			{
			
			if(sprintf(buffer->data,"%d f %jd %u %u %s%c",level,(intmax_t)0,(unsigned)current_time,(unsigned)current_time, serialized_name->data,'\0')<0)
				return 0;
			}
			
free(serialized_name);
return buffer;
}

struct string *fs_stat_to_string_json(struct stat *restrict attribute, char *d_name,int level, bool *type)
{

struct string *buffer=0,*serialized_name=0;
size_t len=0;



#ifdef OS_WINDOWS
int bufsize=__int64_length(attribute->st_size, 10);
len+=bufsize;
#else
len+=integer_digits(attribute->st_size, 10);
#endif
len+=integer_digits(attribute->st_mtime, 10);
len+=integer_digits(attribute->st_ctime, 10);		
len+=integer_digits(level, 10);
serialized_name = string_serialize(d_name, strlen(d_name));
len+=serialized_name->length;
len+=7;//spaces,type,\0

buffer=string_alloc(0, len);
if(!buffer)return 0;


if( attribute->st_mode & S_IFDIR )
			{
			#ifdef OS_WINDOWS
			// :@ :@ :@ :@ :@ :@ :@ :@ :@ :@ :@ :@ :@ :@
			char *tmpsizebuf=(char *)malloc(sizeof(char)*(bufsize+1));
			_i64toa(attribute.st_size,tmpsizebuf,10);
			tmpsizebuf[bufsize]=0;
			if(sprintf(buffer->data,"%d d %s %u %u %s%c",level,tmpsizebuf,(unsigned)attribute->st_mtime,(unsigned)attribute->st_ctime, serialized_name->data,'\0')<0)return 0;
			if(type)*type=1;
			free(tmpsizebuf);
			}
			#else
			if(sprintf(buffer->data,"%d d %jd %u %u %s%c",level,(intmax_t)attribute->st_size,(unsigned)attribute->st_mtime,(unsigned)attribute->st_ctime,serialized_name->data,'\0')<0)return 0;
			if(type)*type=1;
			}
			#endif
else if( attribute->st_mode & S_IFREG )
			{
			#ifdef OS_WINDOWS
			char *tmpsizebuf=(char *)malloc(sizeof(char)*(bufsize+1));
			_i64toa(attribute.st_size,tmpsizebuf,10);
			tmpsizebuf[bufsize]=0;
			if(sprintf(buffer->data,"%d f %s %u %u %s%c",level,tmpsizebuf,(unsigned)attribute->st_mtime,(unsigned)attribute->st_ctime, serialized_name->data,'\0')<0)return 0;
			if(type)*type=0;
			free(tmpsizebuf);
			#else
			if(sprintf(buffer->data,"%d f %jd %u %u %s%c",level,(intmax_t)attribute->st_size,(unsigned)attribute->st_mtime,(unsigned)attribute->st_ctime, serialized_name->data,'\0')<0)
				return 0;
			#endif
			}
else 
	{
	free(serialized_name);
	return 0;
	}
			
free(serialized_name);
return buffer;
}

struct string *fs_stat_to_string(struct stat *restrict attribute, char *d_name,int level, bool *type)
{
struct string *buffer=0;
size_t len=0;


#ifdef OS_WINDOWS
int bufsize=__int64_length(attribute->st_size, 10);
len+=bufsize;
#else
len+=integer_digits(attribute->st_size, 10);
#endif
len+=integer_digits(attribute->st_mtime, 10);
len+=integer_digits(attribute->st_ctime, 10);		
len+=integer_digits(level, 10);
//serialized_name = string_alloc(d_name, strlen(d_name));
len+=strlen(d_name);
len+=12;//spaces,type,\t

buffer=string_alloc(0, len);
if(!buffer)return 0;


//TODO: to stop using fucken sprintf
if( attribute->st_mode & S_IFDIR )
			{
			#ifdef OS_WINDOWS
			// :@ :@ :@ :@ :@ :@ :@ :@ :@ :@ :@ :@ :@ :@
			char *tmpsizebuf=(char *)malloc(sizeof(char)*(bufsize+1));
			_i64toa(attribute.st_size,tmpsizebuf,10);
			tmpsizebuf[bufsize]=0;
			if(sprintf(buffer->data,"%d d %s %u %u %s\\u0000",level,tmpsizebuf,(unsigned)attribute->st_mtime,(unsigned)attribute->st_ctime, d_name)<0)return 0;
			if(type)*type=1;
			free(tmpsizebuf);
			}
			#else
			if(sprintf(buffer->data,"%d d %jd %u %u %s\\u0000",level,(intmax_t)attribute->st_size,(unsigned)attribute->st_mtime,(unsigned)attribute->st_ctime, d_name)<0)return 0;
			if(type)*type=1;									
			}
			#endif
else if( attribute->st_mode & S_IFREG )
			{
			#ifdef OS_WINDOWS
			char *tmpsizebuf=(char *)malloc(sizeof(char)*(bufsize+1));
			_i64toa(attribute.st_size,tmpsizebuf,10);
			tmpsizebuf[bufsize]=0;
			if(sprintf(buffer->data,"%d f %s %u %u %s\\u0000",level,tmpsizebuf,(unsigned)attribute->st_mtime,(unsigned)attribute->st_ctime, d_name)<0)return 0;
			if(type)*type=0;
			free(tmpsizebuf);
			#else
			if(sprintf(buffer->data,"%d f %jd %u %u %s\\u0000",level,(intmax_t)attribute->st_size,(unsigned)attribute->st_mtime,(unsigned)attribute->st_ctime, d_name)<0)
				return 0;
			#endif
			}
else
	return 0;
			
return buffer;
}

struct string *fs_error_to_string(char *d_name,int level,int err_num)
{
struct string *buffer=0;
struct string serialized_name;
size_t len=0;

len+=integer_digits(level, 10);
len+=integer_digits(err_num, 10);		
serialized_name = string(d_name, strlen(d_name));
len+=serialized_name.length;
len+=14;//spaces,type,\0

buffer=string_alloc(0, len);
if(!buffer)return 0;

if(sprintf(buffer->data,"%d e %d 0 0 %s\\u0000",level,err_num,serialized_name.data)<0)
return 0;					

return buffer;

}

static int parse_dir_unix(const char *line,
                          struct stat *sbuf,
                          char *file) {
  char mode[12];
  long nlink = 1;
  char user[33];
  char group[33];
  unsigned long long size;
  char month[4];
  char day[3];
  char year[6];
  char date[20];
  struct tm tm;
  time_t tt;
  int res;

  memset(file, 0, sizeof(char)*1024);
  memset(&tm, 0, sizeof(tm));
  memset(&tt, 0, sizeof(tt));

#define SPACES "%*[ \t]"
  res = sscanf(line,
               "%11s"
               "%lu"  SPACES
               "%32s" SPACES
               "%32s" SPACES
               "%llu" SPACES
               "%3s"  SPACES
               "%2s"  SPACES
               "%5s"  "%*c"
               "%1023c",
               mode, &nlink, user, group, &size, month, day, year, file);
  if (res < 9) {
    res = sscanf(line,
                 "%11s"
                 "%32s" SPACES
                 "%32s" SPACES
                 "%llu" SPACES
                 "%3s"  SPACES
                 "%2s"  SPACES
                 "%5s"  "%*c"
                 "%1023c",
                 mode, user, group, &size, month, day, year, file);
    if (res < 8) {
      return 0;
    }
  }
#undef SPACES

  char *link_marker = strstr(file, " -> ");
  if (link_marker) {
   *link_marker='\0';
  }
  
  int i = 0;
  if (mode[i] == 'd') {
    sbuf->st_mode |= S_IFDIR;
  } else if (mode[i] == 'l') {
    sbuf->st_mode |= S_IFLNK;
  } else {
    sbuf->st_mode |= S_IFREG;
  }
  for (i = 1; i < 10; ++i) {
    if (mode[i] != '-') {
      sbuf->st_mode |= 1 << (9 - i);
    }
  }

  sbuf->st_nlink = nlink;

  sbuf->st_size = size;
  sbuf->st_blksize = 0;
  sbuf->st_blocks = 0;

  sprintf(date,"%s,%s,%s", year, month, day);
  tt = time(NULL);
  gmtime_r(&tt, &tm);
  tm.tm_sec = tm.tm_min = tm.tm_hour = 0;
  if(strchr(year, ':')) {
    int cur_mon = tm.tm_mon;  // save current month
    strptime(date, "%H:%M,%b,%d", &tm);
    // Unix systems omit the year for the last six months
    if (cur_mon + 5 < tm.tm_mon) {  // month from last year
      tm.tm_year--;  // correct the year
    }
  } else {
    strptime(date, "%Y,%b,%d", &tm);
  }

  sbuf->st_atime = sbuf->st_ctime = sbuf->st_mtime = mktime(&tm);

  if(sbuf->st_atim.tv_sec<0)sbuf->st_atim.tv_sec=0;
  if(sbuf->st_mtim.tv_sec<0)sbuf->st_mtim.tv_sec=0;
  if(sbuf->st_ctim.tv_sec<0)sbuf->st_ctim.tv_sec=0;
  return 1;
}

static int parse_dir_win(const char *line,
                         struct stat *sbuf,
                         char *file) {
  char date[9];
  char hour[8];
  char size[33];
  struct tm tm;
  time_t tt;
  int res;
 
	printf("\n\nDOSTA GOLQM PRINTF!!!\n\n\n");

  memset(file, 0, sizeof(char)*1024);
  memset(&tm, 0, sizeof(tm));
  memset(&tt, 0, sizeof(tt));

  res = sscanf(line, "%8s%*[ \t]%7s%*[ \t]%32s%*[ \t]%1023c",
               date, hour, size, file);
  if (res < 4) {
    return 0;
  }

  tt = time(NULL);
  gmtime_r(&tt, &tm);
  tm.tm_sec = tm.tm_min = tm.tm_hour = 0;
  strptime(date, "%m-%d-%y", &tm);
  strptime(hour, "%I:%M%p", &tm);

  sbuf->st_atime = sbuf->st_ctime = sbuf->st_mtime = mktime(&tm);

  sbuf->st_nlink = 1;

  if (!strcmp(size, "<DIR>")) {
    sbuf->st_mode |= S_IFDIR;
  } else {
    unsigned long long nsize = strtoull(size, NULL, 0);
    sbuf->st_mode |= S_IFREG;
    sbuf->st_size = nsize;
    sbuf->st_blksize = 0;
    sbuf->st_blocks = 0;
  }

  if(sbuf->st_atim.tv_sec<0)sbuf->st_atim.tv_sec=0;
  if(sbuf->st_mtim.tv_sec<0)sbuf->st_mtim.tv_sec=0;
  if(sbuf->st_ctim.tv_sec<0)sbuf->st_ctim.tv_sec=0;
  
  return 1;
}

int fs_delete_ftp(struct string *restrict path,struct resources *restrict resources,bool is_dir,CURL *curl)
{
	if(!path || !resources)return 0;
	
	if(path->data[path->length-1]=='/')path->data[--path->length]='\0';
		

const union json *login=0;
if(resources->session_access && json_type(resources->session.ro)==OBJECT)login=resources->session.ro;
else if(resources->auth && resources->auth->login && json_type(resources->auth->login)==OBJECT)login=resources->auth->login;
else return 0;

if(!curl)return 0;
CURLcode res;
struct curl_slist *headerlist=NULL;
char *url=0;
char *cmd=0;
union json *item;
struct string key;

key = string("host");
item=dict_get(login->object, &key);
if(!item)return 0;
if(json_type(item)!=STRING)return 0;


url=(char *)malloc(sizeof(char)*(item->string_node.length+8));
sprintf(url,"ftp://%s/",item->string_node.data);

//headerlist = curl_slist_append(headerlist, "OPTS UTF8 ON");

if(is_dir)
{
cmd=(char *)malloc(sizeof(char)*(path->length+5));
sprintf(cmd,"RMD %s",path->data);
}
else
{
cmd=(char *)malloc(sizeof(char)*(path->length+6));
sprintf(cmd,"DELE %s",path->data);
}
headerlist = curl_slist_append(headerlist, cmd);


  if(curl && curl_set_login(curl,login)) {
    /* Get a file listing from sunet */ 

    
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, read_data);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, read_data);
	   
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEHEADER, NULL);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1); 
    curl_easy_setopt(curl, CURLOPT_POSTQUOTE, headerlist);
	res = curl_easy_perform(curl);
	curl_slist_free_all (headerlist);
	curl_easy_reset(curl);
    /* Check for errors */ 
	
    if(res != CURLE_OK)
		{
		fprintf(stderr, "curl_easy_perform() host:%s failed: %s\n",url,curl_easy_strerror(res));
		free(cmd);cmd=0;
		free(url);
		return 0;
		}
	
	}
	else
	{
		free(url);
		free(cmd);cmd=0;
		return 0;
	}
	
free(url);
free(cmd);cmd=0;
	
	
return 1;
}

int fs_read_dir(struct string *restrict path, int level,int max_level, struct resources *restrict resources, int do_delete, CURL *curl_arg, struct http_response *restrict response) { 
	struct string *apath=NULL;
	struct string *stat=NULL,*chunk;
	struct stat stat_buf;
	
	char file[1024];

	
	bool type;
	int status=0;
	int d_name_len=0;

	char *start;
	char *end;
	
    char* line;

	//CURL connection part

const union json *login=0;
if(resources->session_access && json_type(resources->session.ro)==OBJECT)login=resources->session.ro;
else if(resources->auth && resources->auth->login && json_type(resources->auth->login)==OBJECT)login=resources->auth->login;
else goto error;

CURL *curl=curl_arg; 

CURLcode res;
char *url=0,*tmp=0;
struct buffer *header=buf_alloc(),*body=buf_alloc();
union json *item;
struct string key;

key = string("host");
item=dict_get(login->object, &key);
if(!item)return false;
if(json_type(item)!=STRING)return false;

if(path->length>1)
{
tmp=curl_escape(path->data+1,path->length-1);
}
else tmp=path->data;
int tmp_len=strlen(tmp);
url=(char *)malloc(sizeof(char)*(item->string_node.length+8+tmp_len));
replace_2F(tmp,tmp_len);
sprintf(url,"ftp://%s/%s",item->string_node.data,tmp);
if(path->length>1)curl_free(tmp);

  if(curl && curl_set_login(curl,login)) {
    /* Get a file listing from sunet */ 

    
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, read_data);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, read_data);
    /* If you intend to use this on windows with a libcurl DLL, you must use
       CURLOPT_WRITEFUNCTION as well */ 
	   
    
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, body);
    curl_easy_setopt(curl, CURLOPT_WRITEHEADER, header);
    
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "LIST");
	
	/*
	struct curl_slist *headerlist=NULL;
	headerlist = curl_slist_append(headerlist, "OPTS UTF8 ON");
    curl_easy_setopt(curl, CURLOPT_POSTQUOTE, headerlist);
	*/
	
	res = curl_easy_perform(curl);
	if(curl)curl_easy_reset(curl);
    /* Check for errors */ 
	//curl_slist_free_all (headerlist);
	
    if(res != CURLE_OK)
	{
		
		fprintf(stderr, "curl_easy_perform() host:%s failed: %s\n",url,curl_easy_strerror(res));
		buf_free(header);header=0;
		buf_free(body);body=0;
		
		if(do_delete)
		{
			if(!fs_delete_ftp(path,resources,0,curl))return 0;
		}
		else
		{
			stat=fs_error_to_string("/",level,2);
			if (!response_content_send(&resources->stream, response, stat->data, stat->length)) goto error;
			free(stat);stat=0;
			if (!status) goto error; // TODO: ERROR
		}
		return OK;
	}
	
	buf_null_terminate(header);
	buf_null_terminate(body);
	
	
	}
	else
	{
		buf_free(header);header=0;
		buf_free(body);body=0;
		goto error;
	}
	
	free(url);
	buf_free(header);header=0;

	start = (char *)(body->p+body->begin_offset);
	end = (char *)(body->p+body->begin_offset);
	
  while ((end = strchr(start, '\n')) != NULL) 
   {
    memset(&stat_buf, 0, sizeof(stat_buf));

    if (end > start && *(end-1) == '\r') end--;
	
	line = (char*)malloc(end - start + 1);
    strncpy(line, start, end - start);
    line[end - start] = '\0';
    start = end + 1 + (*end == '\r');


    file[0] = '\0';
    int result =  parse_dir_unix(line, &stat_buf, file) || parse_dir_win(line, &stat_buf, file);		  
    free(line);
	if(result)
		{
		if(!strcmp(file,".") && !level && !do_delete) // TODO: WINDOWS FTP server may not work as expected
			{
				stat=fs_stat_to_string(&stat_buf,"/",0,0);
				if(!stat)goto error;
				if (!response_content_send(&resources->stream, response, stat->data, stat->length)) goto error;
				free(stat);stat=0;
				level++;
			}
		else if(!level && !do_delete)
			{
			stat=fs_stat_to_string_fake("/",0,1);
			if(!stat)goto error;
			if (!response_content_send(&resources->stream, response, stat->data, stat->length)) goto error;
			free(stat);stat=0;
			level++;
			
			}
		
				if( !strcmp( file, "." ))continue;
			if( !strcmp( file, ".." ))continue; 
			d_name_len=strlen(file);
			
			apath=string_alloc(0, (path->length+d_name_len+2)); //+2 for the / in the mid and the end
			apath->data[path->length+d_name_len]='\0';
			apath->data[path->length+d_name_len+1]='\0';
			apath->length--;
			memcpy(apath->data, path->data, path->length);
			if(path->data[path->length-1]!='/' && *file!='/')
			{
			memcpy(apath->data+path->length, "/", 1);
			memcpy(apath->data+path->length+1, file, d_name_len);
			apath->data[path->length+d_name_len+1]='/';
			apath->length++;
			} 
			else
			{ 
			memcpy(apath->data+path->length, file, d_name_len);
			apath->data[path->length+d_name_len]='/';
			}

			struct string *s = string_serialize(file, d_name_len);

			stat=fs_stat_to_string(&stat_buf,s->data,level,&type);
			free(s);
			if(!stat)continue;
			
			if(do_delete)free(stat);

			if (!do_delete)
			{
				if (!response_content_send(&resources->stream, response, stat->data, stat->length)) goto error;
				free(stat);stat=0;
			}		

			if( type )
			{
				if(level<max_level || do_delete)
				{
					if(!fs_read_dir( apath,level+1,max_level,resources,do_delete,curl, response))
					{
						fprintf(stderr,"ERROR read_dir_new:error in recursion. %s\n",apath->data);
					}
				}
			}	
			else if (do_delete) fs_delete_ftp(apath,resources,0,curl);

			free(apath);apath=0;
			
		}//if(res)
		else if(!do_delete)
		{
			stat=fs_error_to_string("/",level,2);
			if (!response_content_send(&resources->stream, response, stat->data, stat->length)) goto error;
			free(stat);stat=0;
		}
	}
	
	if(body && body->len==1 && !do_delete)
	{
		stat=fs_stat_to_string_fake("/",0,1);
		if(!stat)goto error;
		if (!response_content_send(&resources->stream, response, stat->data, stat->length)) goto error;
		free(stat);stat=0;
	}

	buf_free(body);body=0;
	
	if(do_delete)
	{
		if (!fs_delete_ftp(path,resources,1,curl))
		{
			return 0;
		}
	}
	
	return OK;
error://TODO
	buf_free(body);
	return 0;
}

int fs_list(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : {"block_id" = int , "path" = string, "max_level" = int}
block_id -> block_id from fs.get_block
path -> path of the stat
max_level -> max level of listing
*/
struct string *json_serialized=NULL;
union json *item,*root,*temp;
struct blocks *block=0;
struct string *path=0,key;
int max_level=0;
int local_errno=0;

//firstly get the block_id from the request
if(resources->auth_id)auth_id_check(resources);
if(!resources->auth && !session_is_logged_in(resources)){local_errno=1001;goto error;}

//polzvam query za da vzema arguments
if(json_type(query)!=OBJECT)goto error;

int block_id=0;
key = string("block_id");
item=dict_get(query->object, &key);
if(item)
{
if(json_type(item)!=INTEGER)goto error;
block_id=item->integer;
}
block=access_get_blocks(resources,block_id);
if(!block){local_errno=1001;goto error;}


key = string("path");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;
if(!access_auth_check_location(resources,&item->string_node,block->block_id)){local_errno=1101;goto error;} // Auth check
path=access_fs_concat_path(block->location,&item->string_node,1);

if(!path)goto error;

key = string("max_level");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=INTEGER)goto error;
max_level=item->integer;
if(!max_level)goto error;
 
//function main

remote_json_chunked_init(request,response,resources);
CURL *curl=curl_easy_init();
fs_read_dir(path,0,max_level,resources,0,curl, response);//TODO error handling
curl_easy_cleanup(curl);
remote_json_chunked_close(response, resources);
free(path);
return 0;

error:
free(path);
return remote_json_error(request, response, resources, local_errno);
}

int fs_mkdir(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : {"block_id" = int , "path" = string}
block_id -> block_id from fs.get_block
path -> 
*/
struct string *json_serialized=NULL;
union json *root, *temp,*item;
struct blocks *block=0;
struct string *path=0,*stat=0,key;
int pfd;
int local_errno=0;

if(resources->auth_id)auth_id_check(resources);
if(!resources->auth && !session_is_logged_in(resources)){local_errno=1001;goto error;}

if(!access_check_write_access(resources)){local_errno=1102;goto error;} //Auth check read only
//polzvam query za da vzema arguments
if(json_type(query)!=OBJECT)goto error;

int block_id=0;
key = string("block_id");
item=dict_get(query->object, &key);
if(item)
{
if(json_type(item)!=INTEGER)goto error;
block_id=item->integer;
}
block=access_get_blocks(resources,block_id);

key = string("path");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;
if(!access_auth_check_location(resources,&item->string_node,block->block_id)){local_errno=1101;goto error;} // Auth check
path=access_fs_concat_path(block->location,&item->string_node,1);

if(!path)goto error;

//function main

const union json *login=0;
if(resources->session_access && json_type(resources->session.ro)==OBJECT)login=resources->session.ro;
else if(resources->auth && resources->auth->login && json_type(resources->auth->login)==OBJECT)login=resources->auth->login;
else goto error;


	//CURL connection part
CURL *curl=curl_easy_init();
if(!curl)goto error;
CURLcode res;
char *url=0;
char *cmd=0;

struct curl_slist *headerlist=NULL;
struct buffer *header=buf_alloc();

key = string("host");
item=dict_get(login->object, &key);
if (!item || (json_type(item) != STRING)) return BadRequest;

url=(char *)malloc(sizeof(char)*(item->string_node.length+8));
sprintf(url,"ftp://%s/",item->string_node.data);

//headerlist = curl_slist_append(headerlist, "OPTS UTF8 ON");

cmd=(char *)malloc(sizeof(char)*(path->length+5));
sprintf(cmd,"MKD %s",path->data);
headerlist = curl_slist_append(headerlist, cmd);

  if(curl && curl_set_login(curl,login)) {
    /* Get a file listing from sunet */ 

    
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, read_data);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, read_data);
	   
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEHEADER, header);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1);

    curl_easy_setopt(curl, CURLOPT_POSTQUOTE, headerlist);
	
	res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
curl_slist_free_all (headerlist);
    /* Check for errors */ 
    if(res != CURLE_OK)
		{
		fprintf(stderr, "curl_easy_perform() host:%s failed: %s\n",url,curl_easy_strerror(res));
		buf_free(header);
		free(cmd);cmd=0;
		local_errno=2000+res;
		goto error;
		}
	buf_null_terminate(header);
	
	
	}
	else
	{
		buf_free(header);
		free(cmd);cmd=0;
		goto error;
	}
	
free(url);
free(cmd);cmd=0;
	buf_free(header);

//end of func main


stat=fs_stat_to_string_json_fake("/",0,1);
if(!stat)goto error;	

root = json_object_old(false);
if(!root)goto error;
temp = json_string_old(stat);
if(!temp)goto error;


key = string("data");
json_object_insert_old(root, &key, temp);
json_serialized=json_serialize(root);
if(!json_serialized)goto error;

if(!remote_json_send(request,response, resources, json_serialized))
{
//ERROR to return json
free(json_serialized);json_serialized=0;
free(block);block=0;
return -1;
}

free(json_serialized);json_serialized=0;
free(block);block=0;
return 0;

error: 
free(block);
free(stat);

return remote_json_error(request, response, resources, local_errno);
}


int fs_mkfile(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : {"block_id" = int , "path" = string}
block_id -> block_id from fs.get_block
path -> 
*/
struct string *json_serialized=NULL;
union json *root, *temp,*item;
struct blocks *block=0;
struct string *path=0,*stat=0,key;
int pfd;
int local_errno=0;

if(resources->auth_id)auth_id_check(resources);
if(!resources->auth && !session_is_logged_in(resources)){local_errno=1001;goto error;}

if(!access_check_write_access(resources)){local_errno=1102;goto error;} //Auth check read only
//polzvam query za da vzema arguments
if(json_type(query)!=OBJECT)goto error;

int block_id=0;
key = string("block_id");
item=dict_get(query->object, &key);
if(item)
{
if(json_type(item)!=INTEGER)goto error;
block_id=item->integer;
}
block=access_get_blocks(resources,block_id);

key = string("path");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;
if(!access_auth_check_location(resources,&item->string_node,block->block_id)){local_errno=1101;goto error;} // Auth check
path=access_fs_concat_path(block->location,&item->string_node,0);

if(!path)goto error;

//function main

const union json *login=0;
if(resources->session_access && json_type(resources->session.ro)==OBJECT)login=resources->session.ro;
else if(resources->auth && resources->auth->login && json_type(resources->auth->login)==OBJECT)login=resources->auth->login;
else goto error;


	//CURL connection part
CURL *curl=curl_easy_init();
if(!curl)goto error;
CURLcode res;
char *url=0,*tmp=0;
char *cmd=0;

//struct curl_slist *headerlist=NULL;

key = string("host");
item=dict_get(login->object, &key);
if (!item || (json_type(item) != STRING)) return BadRequest;

if(*path->data!='/')goto error;

if(path->length>1)
{
tmp=curl_escape(path->data+1,path->length-1);
}
else tmp=path->data;
int tmp_len=strlen(tmp);
url=(char *)malloc(sizeof(char)*(item->string_node.length+8+tmp_len));
replace_2F(tmp,tmp_len);
sprintf(url,"ftp://%s/%s",item->string_node.data,tmp);
if(path->length>1)curl_free(tmp);
free(path);path=0;


//headerlist = curl_slist_append(headerlist, "OPTS UTF8 ON");

  if(curl && curl_set_login(curl,login)) {
    /* Get a file listing from sunet */ 

    
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_READFUNCTION, null_write);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

    //curl_easy_setopt(curl, CURLOPT_POSTQUOTE, headerlist);
	
	res = curl_easy_perform(curl);
	//curl_slist_free_all (headerlist);
	curl_easy_cleanup(curl);
    /* Check for errors */ 
    if(res != CURLE_OK)
		{
		fprintf(stderr, "curl_easy_perform() host:%s failed: %s\n",url,curl_easy_strerror(res));
		free(cmd);cmd=0;
		local_errno=2000+res;
		goto error;
		}
	
	
	
	}
	else
	{
		free(cmd);cmd=0;
		goto error;
	}
	
free(url);
free(cmd);cmd=0;

//end of func main


stat=fs_stat_to_string_json_fake("/",0,0);
if(!stat)goto error;	

root = json_object_old(false);
if(!root)goto error;
temp = json_string_old(stat);
if(!temp)goto error;


key = string("data");
json_object_insert_old(root, &key, temp);
json_serialized=json_serialize(root);
if(!json_serialized)goto error;

if(!remote_json_send(request,response, resources, json_serialized))
{
//ERROR to return json
free(json_serialized);json_serialized=0;
free(block);block=0;
return -1;
}

free(json_serialized);json_serialized=0;
free(block);block=0;
return 0;


error: 
free(block);
free(stat);

return remote_json_error(request, response, resources, local_errno);
}

struct output
{
	struct stream *stream;
	struct http_response *response;
};

static size_t ftp_file_write(void *buffer, size_t size, size_t nmemb, void *arg)
{
	struct output *output = (struct output *)arg;
	if (response_content_send(output->stream, output->response, buffer, size * nmemb)) return size * nmemb;
	else return 0;
}

int process_download(const char *url,const char *filename,off_t size, const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, CURL *curl)
{
	struct string *buffer;
	int status;

	// Initialize file type detection database
	/*
	magic_t magic;
	magic = magic_open(MAGIC_MIME);
	if (!magic) return InternalServerError;
	if (magic_load(magic, MAGIC_DB))
	{
		magic_close(magic);
		return InternalServerError;
	}
	*/

	// TODO: fix magic
	//  fix everything below

	// Content-Type - Detected with libmagic
	struct string key = string("Content-Type");
/*
#ifdef OS_BSD

	char *type = strdup(magic_file(magic,filename)); 
	magic_close(magic);
	if (type)
	{
		if (!header_init(&response->headers, key.data, key.length, type, strlen(type))) // TODO: strlen is slow
		{
			return InternalServerError;
		}
	}
	else
#endif
*/
	{
		struct string value = string("application/octet-stream");
		if (!response_header_add(response, &key, &value)) return InternalServerError;
	}

	struct output output;
	output.stream = &resources->stream;

	response->code = OK;
	output.response = response;

	if (!response_headers_send(output.stream, request, response, size)) return -1;
	if (response->content_encoding)
	{
		const union json *login=0;
		if(resources->session_access && json_type(resources->session.ro)==OBJECT)login=resources->session.ro;
		else if(resources->auth && resources->auth->login && json_type(resources->auth->login)==OBJECT)login=resources->auth->login;
		else return -1;
	
		if(!curl)return -1;
		CURLcode res;

		if(curl && curl_set_login(curl,login)) {
		/* Get a file listing from sunet */ 

		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ftp_file_write);
		curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, read_data);
		/* If you intend to use this on windows with a libcurl DLL, you must use
       CURLOPT_WRITEFUNCTION as well */ 

		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output);
		curl_easy_setopt(curl, CURLOPT_WRITEHEADER, NULL);
    
	//	struct curl_slist *headerlist=NULL;
	//headerlist = curl_slist_append(headerlist, "OPTS UTF8 ON");
   // curl_easy_setopt(curl, CURLOPT_POSTQUOTE, headerlist);

		res = curl_easy_perform(curl);
		
		//curl_slist_free_all (headerlist);
		/* Check for errors */ 
	
			if(res != CURLE_OK)
			{
			fprintf(stderr, "curl_easy_perform download() url:%s failed: %s\n",url,curl_easy_strerror(res));
			return -1;
			}
		}
		else
		{
		return -1;
		}
	}

	return 0;
}

int fs_download(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : {"block_id" = int , "path" = string}
block_id -> block_id from fs.get_block
path -> path of the stat
*/
union json *item;
struct blocks *block=0;
struct string *path=0,key;
int status=0;

//firstly get the block_id from the request
if(resources->auth_id)auth_id_check(resources);
if(!resources->auth && !session_is_logged_in(resources)) return NotFound;
//polzvam query za da vzema arguments
if(json_type(query)!=OBJECT) return NotFound;

int block_id=0;
key = string("block_id");
item=dict_get(query->object, &key);
if(item)
{
if(json_type(item)!=INTEGER)goto error;
block_id=item->integer;
}
block=access_get_blocks(resources,block_id);

key = string("path");
item=dict_get(query->object, &key);
if (!item || (json_type(item) != STRING)) return NotFound;

if(!access_auth_check_location(resources,&item->string_node,block->block_id))
	return NotFound;
path=access_fs_concat_path(block->location,&item->string_node,0);
if(!path) return NotFound;

	//CURL connection part

const union json *login=0;
if(resources->session_access && json_type(resources->session.ro)==OBJECT)login=resources->session.ro;
else if(resources->auth && resources->auth->login && json_type(resources->auth->login)==OBJECT)login=resources->auth->login;
else goto error;

CURL *curl=curl_easy_init();
if(!curl)goto error;
CURLcode res;
char *url=0,*tmp=0;
struct buffer *header=buf_alloc();
const double filesize;
int i=0;

key = string("host");
item=dict_get(login->object, &key);
if (!item || (json_type(item) != STRING)) return BadRequest;

if(*path->data!='/')goto error;
if(path->length>1)
{
tmp=curl_escape(path->data+1,path->length-1);
}
else tmp=path->data;
int tmp_len=strlen(tmp);
url=(char *)malloc(sizeof(char)*(item->string_node.length+8+tmp_len));
replace_2F(tmp,tmp_len);
sprintf(url,"ftp://%s/%s",item->string_node.data,tmp);
curl_free(tmp);
for(i=item->string_node.length+6+path->length;url[i]!='/';i--);


  if(curl && curl_set_login(curl,login)) {
    /* Get a file listing from sunet */ 

		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, read_data);
		curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, read_data);
		
		curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
		curl_easy_setopt(curl, CURLOPT_FILETIME, 1L);
		   
		
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
		curl_easy_setopt(curl, CURLOPT_WRITEHEADER, header);
		
		//struct curl_slist *headerlist=NULL;
		//headerlist = curl_slist_append(headerlist, "OPTS UTF8 ON");
	//	curl_easy_setopt(curl, CURLOPT_POSTQUOTE, headerlist);
		
		
		res = curl_easy_perform(curl);
		/* Check for errors */ 
		//curl_slist_free_all (headerlist);

		if(res != CURLE_OK)
			{
			fprintf(stderr, "curl_easy_perform() host:%s failed: %s\n",url,curl_easy_strerror(res));
			curl_easy_cleanup(curl);
			buf_free(header);
			free(url);
			free(path);
			return -1;
			}

		buf_null_terminate(header);
		
		if(!strstr((char *)header->p,"213"))
		{

			curl_easy_cleanup(curl);
			buf_free(header);
			free(url);
			free(path);
			return -1;
		}
		res = curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &filesize);
	}
	else
	{
		curl_easy_cleanup(curl); 
		buf_free(header);
		free(url);
		free(path);
		return -1;
	}
	

	buf_free(header);


	// Set Content-Disposition
	struct string *disposition = content_disposition(path);
	if (!disposition) return InternalServerError; // TODO: this may be NotFound (check the code above to make sure)
	key = string("Content-Disposition");
	response_header_add(response, &key, disposition);

	curl_easy_reset(curl);
	status=process_download(url,url+i+1,filesize,request,response,resources, curl);
	curl_easy_cleanup(curl);
	free(url);
	free(path);
	free(disposition);
	return status;
	
error:
	free(path);
	return -1;
}

int fs_remove(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : {"block_id" = int , "path" = string}
block_id -> block_id from fs.get_block
path -> 
*/
struct string *json_serialized=NULL;
union json *root,*item,*temp;
struct blocks *block=0;
struct string *path=0,key;
struct stat attribute;
int local_errno=0;

//firstly get the block_id from the request
if(resources->auth_id)auth_id_check(resources);
if(!resources->auth && !session_is_logged_in(resources)){local_errno=1001;goto error;}
if(!access_check_write_access(resources)){local_errno=1102;goto error;} //Auth check read only
//polzvam query za da vzema arguments
if(json_type(query)!=OBJECT)goto error;

int block_id=0;
key = string("block_id");
item=dict_get(query->object, &key);
if(item)
{
if(json_type(item)!=INTEGER)goto error;
block_id=item->integer;
}
block=access_get_blocks(resources,block_id);

key = string("path");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;
if(!access_auth_check_location(resources,&item->string_node,block->block_id)){local_errno=1101;goto error;} // Auth check
path=access_fs_concat_path(block->location,&item->string_node,1);

if(!path)goto error;

CURL *curl=curl_easy_init();
int status=fs_read_dir(path,0,999,resources,1,curl, 0);//TODO error handling
curl_easy_cleanup(curl);

if(!status)goto error;

root = json_object_old(false);
if(!root)goto error;
json_serialized=json_serialize(root);
if(!json_serialized)goto error;

if(!remote_json_send(request,response, resources, json_serialized))
{
//ERROR to return json
free(json_serialized);json_serialized=0;
free(path);
free(block);block=0;
return -1;
}

free(json_serialized);json_serialized=0;
free(path);
free(block);block=0;
return 0;

error: //ERROR tuka trqbva da vrushta json za greshka
free(path);
free(block);

return remote_json_error(request, response, resources, local_errno);
}


int fs_upload(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	
	//Request : {"block_id" = int , "path" = string}
	//block_id -> block_id from fs.get_block
	//path -> path of the stat
	
	struct string *json_serialized=NULL;
	union json *item,*root,*temp;
	struct blocks *block=0;
	struct string *path=0,key;
	int local_errno=0;
	struct string *status_key;

	if(resources->auth_id)auth_id_check(resources);
	if(!resources->auth && !session_is_logged_in(resources)){local_errno=1001;goto error;}
	if(!access_check_write_access(resources)){local_errno=1102;goto error;} //Auth check read only
	//polzvam query za da vzema arguments
	if(json_type(query)!=OBJECT)goto error;
	
	int block_id=0;
	key = string("block_id");
	item=dict_get(query->object, &key);
	if(item)
	{
	if(json_type(item)!=INTEGER)goto error;
	block_id=item->integer;
	}
	block=access_get_blocks(resources,block_id);

	key = string("path");
	item=dict_get(query->object, &key);
	if(!item)goto error;
	if(json_type(item)!=STRING)goto error;

	if(!access_auth_check_location(resources,&item->string_node,block->block_id)){local_errno=1101;goto error;} // Auth check
	path=access_fs_concat_path(block->location,&item->string_node,0);
	if(!path)goto error;

	key = string("status_key");
	item = dict_get(query->object, &key);
	if (item)
	{
		if (json_type(item) != STRING) goto error;
		status_key = &item->string_node;
	}
	else status_key = 0;

	
	struct string *filename;

	key = string("filename");
    item = dict_get(query->object, &key);
    if (item) // HTML 5 file upload
    {
        if (json_type(item) != STRING) goto error;
		filename = &item->string_node;
	}
	else filename = 0;
	
	if(path->data[path->length-1]=='/')
	{
		path->data[--path->length]='\0';
	}
	int status = http_upload(path, filename,resources ,request, &resources->stream, status_key);

	free(path);

	if (!status)
	{
		// Redirect if the client specified so
		key = string("redirect");
		item = dict_get(query->object, &key);
		if (item)
		{
			if (json_type(item) == STRING)
			{
				struct string *redirect = &item->string_node;
				key = string("Location");
				response_header_add(response, &key, redirect);
				status = MovedPermanently;
			}
			else status = NotFound; // TODO: change this
		}
		else status = OK;
	}

	//if (!header_add_static(response, "Content-Type", "text/plain; charset=UTF-8;")) goto error; // TODO: ERROR

	response->code = status;
	if (!response_headers_send(&resources->stream, request, response, 0)) goto error;

	//stream_term(&resources->stream);
	
	return 0;
	
error: //ERROR tuka trqbva da vrushta json za greshka
free(path);

return remote_json_error(request, response, resources, local_errno);
}

int fs_get_status(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : {"status_key" : integer}
status_id -> status id returned by copy or move

Response : {"status_size" : integer}
*/
struct string *json_serialized=NULL,key;
union json *root,*item,*temp;
struct cp_status *cp_status=0;
struct string *status_key=0;
int local_errno=0;
off_t size=0;
int state=0;

if(resources->auth_id)auth_id_check(resources);
if(!resources->auth && !session_is_logged_in(resources)){response->code = Forbidden;local_errno=1001;printf("ERROR 0\n");goto error;}
//polzvam query za da vzema arguments
if(json_type(query)!=OBJECT)
{
	response->code = NotFound;
	printf("ERROR 1\n");
	goto error;
}

	key = string("status_key");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != STRING))
	{
		response->code = NotFound;
		printf("ERROR 2\n");
		goto error;
	}
	status_key = &item->string_node;

//function main
	
	size = status_get(status_key, &state);
	if (size < 0)
	{
		printf("ERROR 3\n");
		goto error;
	}

	root = json_object_old(false);
	if(!root)
	{
		response->code = InternalServerError;
		printf("ERROR 4\n");
		goto error;
	}
	temp = json_integer(size);
	key = string("size");
	json_object_insert_old(root, &key, temp);
	
	temp = json_integer(state);
	key = string("status");
	json_object_insert_old(root, &key, temp);
	
	struct string *filename=status_get_name(status_key);
	if(filename)
	{
	temp = json_string_old(filename);
	key = string("newname");
	json_object_insert_old(root, &key, temp);
	free(filename);
	}
	
	json_serialized=json_serialize(root);
	if(!json_serialized)
	{
		response->code = InternalServerError;
		printf("ERROR 5\n");
		goto error;
	}

	if(!remote_json_send(request,response, resources, json_serialized))
	{
		response->code = InternalServerError;
		//ERROR to return json
		free(json_serialized);json_serialized=0;
		printf("ERROR 6\n");
		return -1;
	}
	
//end of func main



free(json_serialized);json_serialized=0;
return 0;


error: //ERROR tuka trqbva da vrushta json za greshka
if(!local_errno)local_errno=errno;
return remote_json_error(request, response, resources, local_errno);
}

int fs_move(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : {"block_id" : integer , "source_path" : string, "destination_block_id" : integer, "destination_path" : string}
block_id -> block_id from fs.get_block
source_path -> from
destination_path -> to

Response : {"status_key" : integer}
*/
struct string *json_serialized=NULL;
union json *root,*item;
struct blocks *block=0;
struct blocks *destination_block=0;
struct string *source_path=0,*destination_path=0,key,*status_key=0;
struct fs_print_status_struct *print_status=(struct fs_print_status_struct *)malloc(sizeof(struct fs_print_status_struct));
int local_errno=0;

		
		
if(resources->auth_id)auth_id_check(resources);
if(!resources->auth && !session_is_logged_in(resources)){local_errno=1001;goto error;}
if(!access_check_write_access(resources)){local_errno=1102;goto error;} //Auth check read only
//polzvam query za da vzema arguments
if(json_type(query)!=OBJECT)goto error;

int block_id=0;
key = string("block_id");
item=dict_get(query->object, &key);
if(item)
{
if(json_type(item)!=INTEGER)goto error;
block_id=item->integer;
}
block=access_get_blocks(resources,block_id);



destination_block=block;


key = string("source_path");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;
source_path=access_fs_concat_path(block->location,&item->string_node,0); 

if(!source_path)goto error;

key = string("destination_path");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;
destination_path=access_fs_concat_path(destination_block->location,&item->string_node,0); 

if(!destination_path)goto error;


key = string("status_key");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;
status_key=&item->string_node;

if(!status_key)goto error;
	
status_set(status_key, 0, STATE_PENDING);

//The main part

const union json *login=0;
if(resources->session_access && json_type(resources->session.ro)==OBJECT)login=resources->session.ro;
else if(resources->auth && resources->auth->login && json_type(resources->auth->login)==OBJECT)login=resources->auth->login;
else goto error;


	//CURL connection part
CURL *curl=curl_easy_init();
if(!curl)goto error;
CURLcode res;
char *url=0;
char *rnfr=0;
char *rnto=0;

struct curl_slist *headerlist=NULL;
struct buffer *header=buf_alloc();

key = string("host");
item=dict_get(login->object, &key);
if (!item || (json_type(item) != STRING)) return BadRequest;

url=(char *)malloc(sizeof(char)*(item->string_node.length+8));
sprintf(url,"ftp://%s/",item->string_node.data);

//headerlist = curl_slist_append(headerlist, "OPTS UTF8 ON");

rnfr=(char *)malloc(sizeof(char)*(source_path->length+6));
sprintf(rnfr,"RNFR %s",source_path->data);
headerlist = curl_slist_append(headerlist, rnfr);

rnto=(char *)malloc(sizeof(char)*(destination_path->length+6));
sprintf(rnto,"RNTO %s",destination_path->data);
headerlist = curl_slist_append(headerlist, rnto);

  if(curl && curl_set_login(curl,login)) {
    /* Get a file listing from sunet */ 

    
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, read_data);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, read_data);
	   
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEHEADER, header);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1);

    curl_easy_setopt(curl, CURLOPT_POSTQUOTE, headerlist);
	
	res = curl_easy_perform(curl);
	curl_slist_free_all (headerlist);
	curl_easy_cleanup(curl);
    /* Check for errors */ 
    if(res != CURLE_OK)
		{
		fprintf(stderr, "curl_easy_perform() host:%s failed: %s\n",url,curl_easy_strerror(res));
		buf_free(header);
		free(rnfr);rnfr=0;
		free(rnto);rnto=0;
		local_errno=2000+res;
		goto error;
		}
	
	buf_null_terminate(header);
	
	
	}
	
free(url);
free(rnfr);rnfr=0;
free(rnto);rnto=0;
	buf_free(header);

//end of func main


free(json_serialized);json_serialized=0;
free(block);block=0;
root = json_object_old(false);


if(!root)goto error;
json_serialized=json_serialize(root);
if(!json_serialized)goto error;
status_set(status_key, -1, STATE_FINISHED);
if(!remote_json_send(request,response, resources, json_serialized))
{
//ERROR to return json
free(json_serialized);json_serialized=0;
return -1;
}

free(json_serialized);json_serialized=0;
return 0;


error: //ERROR tuka trqbva da vrushta json za greshka
free(block);
if(status_key)status_set(status_key, -1, STATE_ERROR);
return remote_json_error(request, response, resources, local_errno);
}
