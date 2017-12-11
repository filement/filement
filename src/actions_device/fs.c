#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <ctype.h>
#include <math.h>

#ifdef OS_BSD
# if !defined(OS_ANDROID)
#  include <sys/statvfs.h>
# else
#  include <sys/vfs.h>
#  define statvfs statfs
# endif
# include <sys/socket.h>
# include <dirent.h>
#else
# include <windows.h>
# include <wchar.h>
# include "mingw.h"
#endif

#include "types.h"
#include "format.h"

// TODO: fix this
#ifndef O_NOFOLLOW
# define O_NOFOLLOW 0
#endif

#if !defined(OS_WINDOWS)
# include "actions.h"
# include "access.h"
# include "storage.h"
# include "io.h"
# include "magic.h"
# include "download.h"
# include "evfs.h"
# include "upload.h"
# include "status.h"
# include "operations.h"
# include "Epeg.h"
#else
# include "../actions.h"
# include "../access.h"
# include "../storage.h"
# include "../io.h"
# include "../magic.h"
# include "../download.h"
# include "../evfs.h"
# include "../upload.h"
# include "../status.h"
# include "../operations.h"
#endif

#include "fs.h"
#include "session.h"
#include "search.h"

#if defined(OS_WINDOWS)
   typedef enum _Epeg_Colorspace
     {
	EPEG_GRAY8,
	  EPEG_YUV8,
	  EPEG_RGB8,
	  EPEG_BGR8,
	  EPEG_RGBA8,
	  EPEG_BGRA8,
	  EPEG_ARGB32,
	  EPEG_CMYK
     }
   Epeg_Colorspace;
   
   typedef struct _Epeg_Image          Epeg_Image;
   typedef struct _Epeg_Thumbnail_Info Epeg_Thumbnail_Info;

   struct _Epeg_Thumbnail_Info
     {
	char                   *uri;
	unsigned long long int  mtime;
	int                     w, h;
	char                   *mimetype;
     };
   
    Epeg_Image   *epeg_file_open                 (const char *file);
    Epeg_Image   *epeg_memory_open               (unsigned char *data, int size);
    void          epeg_size_get                  (Epeg_Image *im, int *w, int *h);
    void          epeg_decode_size_set           (Epeg_Image *im, int w, int h);
    void          epeg_colorspace_get            (Epeg_Image *im, int *space);
    void          epeg_decode_colorspace_set     (Epeg_Image *im, Epeg_Colorspace colorspace);
    const void   *epeg_pixels_get                (Epeg_Image *im, int x, int y, int w, int h);
    void          epeg_pixels_free               (Epeg_Image *im, const void *data);
    const char   *epeg_comment_get               (Epeg_Image *im);
    void          epeg_thumbnail_comments_get    (Epeg_Image *im, Epeg_Thumbnail_Info *info);
    void          epeg_comment_set               (Epeg_Image *im, const char *comment);
    void          epeg_quality_set               (Epeg_Image *im, int quality);
    void          epeg_thumbnail_comments_enable (Epeg_Image *im, int onoff);
    void          epeg_file_output_set           (Epeg_Image *im, const char *file);
    void          epeg_memory_output_set         (Epeg_Image *im, unsigned char **data, int *size);
    int           epeg_encode                    (Epeg_Image *im);
    int           epeg_trim                      (Epeg_Image *im);
    void          epeg_close                     (Epeg_Image *im);

	#define fileno(__F) ((__F)->_file)
	
#endif

// TODO: on download don't add content-disposition for not found files

#define MIN_STATUS_SIZE 2097152

#define ARCHIVE_NAME_DEFAULT "filement_archive.zip"

#ifdef OS_WINDOWS
_CRTIMP char* __cdecl __MINGW_NOTHROW	_i64toa(__int64, char *, int);
size_t __int64_length(__int64 number, unsigned base);

WINBASEAPI BOOL WINAPI MoveFileWithProgressA(LPCSTR,LPCSTR,LPPROGRESS_ROUTINE,LPVOID,DWORD);
WINBASEAPI BOOL WINAPI MoveFileWithProgressW(LPCWSTR,LPCWSTR,LPPROGRESS_ROUTINE,LPVOID,DWORD);

_CRTIMP char* __cdecl __MINGW_NOTHROW 	_strdup (const char*) __MINGW_ATTRIB_MALLOC;
_CRTIMP char* __cdecl __MINGW_NOTHROW	strdup (const char*) __MINGW_ATTRIB_MALLOC;

#define MoveFileWithProgress MoveFileWithProgressA
#endif

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

//includes related with coreutils

static int request_paths(const struct vector *restrict request, struct vector *restrict paths, struct resources *restrict resources)
{
	struct string key, *location;
	size_t index;
	struct blocks *block=0;
	union json *item, *block_id, *path;
	int status = 0;

	for(index = 0; index < request->length; ++index)
	{
		item = vector_get(request, index);
		if (json_type(item) != OBJECT)
		{
			status = BadRequest;
			break;
		}

		key = string("block_id");
		block_id = dict_get(item->object, &key);
		if (!block_id || (json_type(block_id) != INTEGER))
		{
			status = BadRequest;
			break;
		}

		key = string("path");
		path = dict_get(item->object, &key);
		if (!path || (json_type(path) != STRING))
		{
			status = BadRequest;
			break;
		}
		
		block=access_get_blocks(resources,block_id->integer);
		if(!block){
					status = NotFound;
					goto finally;
				}



		if(!access_auth_check_location(resources,&path->string_node,block->block_id)){
					status = NotFound;
					free(block->location);
					free(block);
					goto finally;
				}
		location=access_fs_concat_path(block->location,&path->string_node,1);
		if (!location){
					free(block->location);
					free(block);
					status = NotFound;
					goto finally;
					}
		if (!vector_add(paths, location))
				{
					free(block->location);
					free(block);
					status = InternalServerError;
					goto finally;
				}
		
		
		free(block->location);
		free(block);block=0;
		
	}

finally:
	return status;
}

bool fs_check_path_existence(struct string *restrict path)
{
	#ifdef OS_WINDOWS
	struct _stati64 sb;
	#else
	struct stat sb;
	#endif

	if (stat(path->data, &sb) == -1) 
		return 0;

	switch (sb.st_mode & S_IFMT) {
		case S_IFDIR:  return 1;			   
		case S_IFREG:  return 1;			
		default:   return 0;				
	}
}

struct string *fs_error_to_string(struct string *restrict path,struct blocks *restrict block,char *d_name,int level,int err_num)
{
	struct string *buffer=0,*serialized_name=0;
	size_t len=0;

	len+=integer_digits(level, 10);
	len+=integer_digits(err_num, 10);		
	serialized_name = string_alloc(d_name, strlen(d_name));
	len+=serialized_name->length;
	len+=14;//spaces,type,\t

	buffer=string_alloc(0, len);
	if(!buffer)return 0;

	if(sprintf(buffer->data,"%d e %d 0 0 %s\\u0000",level,err_num,serialized_name->data)<0)
		return 0;					

	free(serialized_name);
	return buffer;
}

struct string *fs_stat_to_string_json(struct string *restrict path,struct blocks *restrict block, char *d_name,int level, bool *type)
{
	//TODO the block might be not serialized
	#ifdef OS_WINDOWS
	struct _stati64 attribute;
	#else
	struct stat attribute;
	#endif
	struct string *buffer=0,*serialized_name=0;
	size_t len=0;

	/*
	//TODO to check do I really need this check and If I do, to fix the sizes in WINDOWS
	if(block->location->length > path->length)
	return 0;
	*/

	if( lstat( path->data, &attribute ) == -1 )
	{
	fprintf(stderr,"can't stat errno %d %s\n",errno,path->data);
	return 0;
	}

	#ifdef OS_WINDOWS
	int bufsize=__int64_length(attribute.st_size, 10);
	len+=bufsize;
	#else
	len+=integer_digits(attribute.st_size, 10);
	#endif
	len+=integer_digits(attribute.st_mtime, 10);
	len+=integer_digits(attribute.st_ctime, 10);		
	len+=integer_digits(level, 10);
	serialized_name = string_alloc(d_name, strlen(d_name));
	len+=serialized_name->length;
	len+=7;//spaces,type,\t

	buffer=string_alloc(0, len);
	if(!buffer)return 0;

	if( (attribute.st_mode & S_IFDIR) && (!S_ISLNK(attribute.st_mode)) )
	{
		#ifdef OS_WINDOWS
		// :@ :@ :@ :@ :@ :@ :@ :@ :@ :@ :@ :@ :@ :@
		char *tmpsizebuf=(char *)malloc(sizeof(char)*(bufsize+1));
		_i64toa(attribute.st_size,tmpsizebuf,10);
		tmpsizebuf[bufsize]=0;
		if(sprintf(buffer->data,"%d d %s %u %u %s%c",level,tmpsizebuf,(unsigned)attribute.st_mtime,(unsigned)attribute.st_ctime, serialized_name->data,'\0')<0)return 0;
		if(type)*type=1;
		free(tmpsizebuf);
		#else
		if(sprintf(buffer->data,"%d d %jd %u %u %s%c",level,(intmax_t)attribute.st_size,(unsigned)attribute.st_mtime,(unsigned)attribute.st_ctime,serialized_name->data,'\0')<0)return 0;
		if(type)*type=1;									
		#endif
	}
	else if( attribute.st_mode & S_IFREG )
	{
		#ifdef OS_WINDOWS
		char *tmpsizebuf=(char *)malloc(sizeof(char)*(bufsize+1));
		_i64toa(attribute.st_size,tmpsizebuf,10);
		tmpsizebuf[bufsize]=0;
		if(sprintf(buffer->data,"%d f %s %u %u %s%c",level,tmpsizebuf,(unsigned)attribute.st_mtime,(unsigned)attribute.st_ctime, serialized_name->data,'\0')<0)return 0;
		if(type)*type=1;
		free(tmpsizebuf);
		#else
		if(sprintf(buffer->data,"%d f %jd %u %u %s%c",level,(intmax_t)attribute.st_size,(unsigned)attribute.st_mtime,(unsigned)attribute.st_ctime, serialized_name->data,'\0')<0)
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

struct string *fs_stat_to_string(struct string *restrict path,struct blocks *restrict block, char *d_name,int level, bool *type)
{
//TODO the block might be not serialized
#ifdef OS_WINDOWS
struct _stati64 attribute;
#else
struct stat attribute;
#endif
struct string *buffer=0;
size_t len=0;

/*
//TODO to check do I really need this check and If I do, to fix the sizes in WINDOWS
if(block->location->length > path->length)
return 0;
*/

if( lstat( path->data, &attribute ) == -1 )
{
fprintf(stderr,"can't stat errno %d %s\n",errno,path->data);
return 0;
}

#ifdef OS_WINDOWS
int bufsize=__int64_length(attribute.st_size, 10);
len+=bufsize;
#else
len+=integer_digits(attribute.st_size, 10);
#endif
len+=integer_digits(attribute.st_mtime, 10);
len+=integer_digits(attribute.st_ctime, 10);		
len+=integer_digits(level, 10);
//serialized_name = string_alloc(d_name, strlen(d_name));
len+=strlen(d_name);
len+=12;//spaces,type,\t

buffer=string_alloc(0, len);
if(!buffer)return 0;

	//TODO: to stop using fucken sprintf
	if (S_ISDIR(attribute.st_mode))
	{
		#ifdef OS_WINDOWS
		// :@ :@ :@ :@ :@ :@ :@ :@ :@ :@ :@ :@ :@ :@
		char *tmpsizebuf=(char *)malloc(sizeof(char)*(bufsize+1));
		_i64toa(attribute.st_size,tmpsizebuf,10);
		tmpsizebuf[bufsize]=0;
		if(sprintf(buffer->data,"%d d %s %u %u %s\\u0000",level,tmpsizebuf,(unsigned)attribute.st_mtime,(unsigned)attribute.st_ctime, d_name)<0)return 0;
		if(type)*type=1;
		free(tmpsizebuf);
		#else
		if(sprintf(buffer->data,"%d d %jd %u %u %s\\u0000",level,(intmax_t)attribute.st_size,(unsigned)attribute.st_mtime,(unsigned)attribute.st_ctime, d_name)<0)return 0;
		if(type)*type=1;									
		#endif
	}
	else if (S_ISREG(attribute.st_mode))
	{
		#ifdef OS_WINDOWS
		char *tmpsizebuf=(char *)malloc(sizeof(char)*(bufsize+1));
		_i64toa(attribute.st_size,tmpsizebuf,10);
		tmpsizebuf[bufsize]=0;
		if(sprintf(buffer->data,"%d f %s %u %u %s\\u0000",level,tmpsizebuf,(unsigned)attribute.st_mtime,(unsigned)attribute.st_ctime, d_name)<0)return 0;
		if(type)*type=1;
		free(tmpsizebuf);
		#else
		if(sprintf(buffer->data,"%d f %jd %u %u %s\\u0000",level,(intmax_t)attribute.st_size,(unsigned)attribute.st_mtime,(unsigned)attribute.st_ctime, d_name)<0)
			return 0;
		#endif
	}
	else return 0;
			
	return buffer;
}

int fs_remove_dir( struct string *restrict path,struct blocks *restrict block, int level) {	

	struct string *apath=0;
	DIR *dir=0; 
	struct dirent *entry;

	#ifdef OS_WINDOWS
	struct _stati64 attribute;
	#else
	struct stat attribute;
	#endif
	unsigned d_name_len=0;
	
	
	if( ( dir = opendir( path->data ) ) != NULL ) {		
		//TODO readdir is not thread safe
		while( ( entry = readdir( dir ) ) != NULL ) {	 
	
			
			if( strcmp( (char*)entry->d_name, "." ) == 0 )continue;
			if( strcmp( (char*)entry->d_name, ".." ) == 0 )continue;
			
			d_name_len=strlen((char*)entry->d_name);
			
			apath=string_alloc(0, (path->length+d_name_len+1)); //+1 for the /

			memcpy(apath->data, path->data, path->length);
			memcpy(apath->data+path->length, "/", 1);
			memcpy(apath->data+path->length+1, entry->d_name, d_name_len);
			
			if( lstat( apath->data, &attribute ) == -1 )continue;
			
			if( (attribute.st_mode & S_IFDIR) && (!S_ISLNK(attribute.st_mode)) )
			{
			 		 
						if(!fs_remove_dir( apath,block,level+1))				
							{
							fprintf(stderr,"ERROR fs_remove_dir:error in recursion. %s\n",apath->data);
							}
					if(remove (apath->data)==-1)
						perror("ERROR fs_remove_dir");
						
			}	
			else { 
				
						if(remove (apath->data)==-1)
						perror("ERROR fs_remove_dir");
						
				}
			
			free(apath);apath=0;
		}			
			
	}
	else {
		fprintf(stderr,"ERROR fs_remove_dir:can't read dir %s\n",path->data);
	}

	
	closedir( dir );dir=0;
	
	// REMOVE AFTER CLOSE
	if(!level)
	{	
	if(remove(path->data))
	{
		fprintf(stderr,"ERROR fs_remove_dir: can't remove %s ERRNO %d\n",path->data,errno);
		perror("ERROR fs_remove_dir");
		
		if(dir)closedir(dir);
		return 0;
		//ERROR
	}
	}
	
	if(dir)closedir(dir);
	return OK;
}

static int fs_read_dir(struct string *restrict path,struct blocks *restrict block, int level,int max_level, struct resources *restrict resources, struct http_response *restrict response) { 
	struct string *apath=NULL;
	struct string *stat=NULL,*chunk;
	DIR *dir=0; 
	struct dirent *entry;
	bool type;
	int status=0;
	int d_name_len=0;
	bool success = false;
#if !defined(OS_WINDOWS)
	struct statvfs info;
#endif
	int bufsize;
	int len=0;

if(!level)
		{

#ifdef OS_WINDOWS
			 typedef BOOL (WINAPI *P_GDFSE)(LPCTSTR, PULARGE_INTEGER, 
                                  PULARGE_INTEGER, PULARGE_INTEGER);
				BOOL  fResult;
				unsigned __int64 i64FreeBytesToCaller,
								   i64TotalBytes,
								   i64FreeBytes;
				P_GDFSE pGetDiskFreeSpaceEx = NULL;
				pGetDiskFreeSpaceEx = (P_GDFSE)GetProcAddress (
												   GetModuleHandle ("kernel32.dll"),
																	"GetDiskFreeSpaceExA");
				  if (pGetDiskFreeSpaceEx)
				  {
					 fResult = pGetDiskFreeSpaceEx (path->data,
											 (PULARGE_INTEGER)&i64FreeBytesToCaller,
											 (PULARGE_INTEGER)&i64TotalBytes,
											 (PULARGE_INTEGER)&i64FreeBytes);
					 if (fResult)
					 {
						
						len+=__int64_length(i64TotalBytes, 10);
						len+=__int64_length(i64FreeBytesToCaller, 10);
						len+=11;//v1,spaces,\t
						
						stat=string_alloc(0, len);
						if(!stat)return 0;

						sprintf(stat->data,"v1 %jd %jd \\u0000",i64TotalBytes,i64FreeBytesToCaller);
						
						if (!response_content_send(&resources->stream, response, stat->data, stat->length)) goto error;
						free(stat);stat=0;

					 }
				  }
#else
# if !defined(PUBCLOUD)
			success = !statvfs(path->data, &info);
			if(success)
			{
				len+=integer_digits((intmax_t)info.f_frsize * info.f_blocks, 10);
				len+=integer_digits((intmax_t)info.f_frsize * info.f_bfree, 10);
				len+=11;//v1,spaces,\t

				stat=string_alloc(0, len);
				if(!stat)return 0;
				sprintf(stat->data,"v1 %jd %jd \\u0000",(intmax_t)info.f_frsize * info.f_blocks,(intmax_t)info.f_frsize * info.f_bfree);

				if (!response_content_send(&resources->stream, response, stat->data, stat->length)) goto error;
				free(stat);stat=0;
			}
# endif
#endif


			stat=fs_stat_to_string(path,block,"/",0,0);
			if(!stat)goto error;
			if (!response_content_send(&resources->stream, response, stat->data, stat->length)) goto error;
			free(stat);stat=0;
			level++;
		}

	if( ( dir = opendir( path->data ) ) != NULL ) {  

	while( ( entry = readdir( dir ) ) != NULL ) {
			if( strcmp( (char*)entry->d_name, "." ) == 0 )continue;
			if( strcmp( (char*)entry->d_name, ".." ) == 0 )continue;
			d_name_len=strlen((char*)entry->d_name);
			
			apath=string_alloc(0, (path->length+d_name_len+1)); //+1 for the /

			memcpy(apath->data, path->data, path->length);
			#ifdef OS_BSD
			memcpy(apath->data+path->length, "/", 1);
			#endif
			#ifdef OS_WINDOWS
			memcpy(apath->data+path->length, "\\", 1);
			#endif
			memcpy(apath->data+path->length+1, entry->d_name, d_name_len);

			struct string *s = string_serialize((unsigned char *)entry->d_name, d_name_len);

			stat=fs_stat_to_string(apath,block,s->data,level,&type);
			free(s);
			if(!stat)continue;

			if (!response_content_send(&resources->stream, response, stat->data, stat->length)){free(apath);apath=0;free(stat);stat=0; goto error;}
			free(stat); stat = 0;

			if( type )
			{
				if(level<max_level)
				{
					if(!fs_read_dir( apath,block,level+1,max_level,resources, response))
					{
					fprintf(stderr,"ERROR read_dir_new:error in recursion. %s\n",apath->data);
					}
				}
			}	

			free(apath);apath=0;
		}			
		closedir( dir );dir=0;
	}
	else { // TODO errors
		if(level==1)
			{
				fprintf(stderr,"ERROR fs.list strange error in level 1. errno=%d\n", (int)errno);
				goto error;									
			}
		else
			{
				stat=fs_error_to_string(path,block,"/",level,errno);
				if (!response_content_send(&resources->stream, response, stat->data, stat->length)) goto error;
				free(stat);stat=0;
			} 
	}

if(dir)closedir( dir );
return OK;
error://TODO
if(dir)closedir( dir );
return 0;
}

#ifdef OS_WINDOWS
DWORD CALLBACK CopyProgressRoutine(
	LARGE_INTEGER TotalFileSize,
	LARGE_INTEGER TotalBytesTransferred,
	LARGE_INTEGER StreamSize,
	LARGE_INTEGER StreamBytesTransferred,
	DWORD dwStreamNumber,
	DWORD dwCallbackReason,
	HANDLE hSourceFile,
	HANDLE hDestinationFile,
	LPVOID lpData)
{   
	time_t time_end;
	unsigned long diff_data=0;
	/*
	int bufsize=0;
	char *tmpsizebuf=NULL;
	*/
	struct fs_print_status_struct *print_struct=(struct fs_print_status_struct *)lpData;





	switch (dwCallbackReason)
   {
	case CALLBACK_CHUNK_FINISHED:



	if(print_struct->time_init){time (&print_struct->time_start);print_struct->time_init=0;}

	print_struct->sum_size+=TotalBytesTransferred.QuadPart;
	diff_data=print_struct->sum_size-print_struct->last_size;
	if(diff_data<MIN_STATUS_SIZE)return PROGRESS_CONTINUE;
	print_struct->last_size=print_struct->sum_size;

	time (&time_end);
	if(difftime (time_end,print_struct->time_start)<1)return PROGRESS_CONTINUE;
	time (&print_struct->time_start);

	/*
	bufsize=__int64_length(TotalBytesTransferred.QuadPart, 10);
	tmpsizebuf=(char *)malloc(sizeof(char)*(bufsize+1));
	_i64toa(StreamBytesTransferred.QuadPart,tmpsizebuf,10);
	tmpsizebuf[bufsize]=0;
	*/

	 status_set(print_struct->key, print_struct->sum_size, -1);

		break;
	case CALLBACK_STREAM_SWITCH:

		break;
	}

	return PROGRESS_CONTINUE;
}
#endif

void fs_print_status(unsigned long data_size, void *pass_data)
{
	time_t time_end;
	unsigned long diff_data = 0;
	struct fs_print_status_struct *print_struct = (struct fs_print_status_struct *)pass_data;

	if (print_struct->time_init)
	{
		time(&print_struct->time_start);
		print_struct->time_init = 0;
	}
	print_struct->sum_size += data_size;

	diff_data = print_struct->sum_size - print_struct->last_size;
	if (diff_data < MIN_STATUS_SIZE) return;
	print_struct->last_size = print_struct->sum_size;

	time(&time_end);
	if (difftime(time_end, print_struct->time_start) < 2) return;
	time(&print_struct->time_start);

	status_set(print_struct->key, print_struct->sum_size, -1);
}

union json *fs_get_blocks_json(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query,int *local_errno)
{
union json *root, *temp, *array,*tmp_object;
struct blocks_array *blocks_array = 0;
struct string key;
int i;

#ifdef DEVICE
	struct string *path=0;
	struct blocks *block=0;
	#ifdef OS_WINDOWS
	struct _stati64 attribute;
	#else
	struct stat attribute;
	#endif
#endif

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
	
	temp=json_integer(blocks_array->blocks[i]->location_id);
	key = string("block_id");
	if(json_object_insert_old(tmp_object, &key, temp))goto error;
	
	temp=json_integer(blocks_array->blocks[i]->user_id);
	key = string("user_id");
	if(json_object_insert_old(tmp_object, &key, temp))goto error;

	temp=json_integer(blocks_array->blocks[i]->size);
	key = string("size");
	if(json_object_insert_old(tmp_object, &key, temp))goto error;
	
	temp=json_string_old(blocks_array->blocks[i]->location);
	key = string("location");
	if(json_object_insert_old(tmp_object, &key, temp))goto error;
	
	temp=json_string_old(blocks_array->blocks[i]->name);
	key = string("name");
	if(json_object_insert_old(tmp_object, &key, temp))goto error;
	#ifdef DEVICE
	//TODO to make it better
	if(resources->auth)
	{
	
		block=access_get_blocks(resources,blocks_array->blocks[i]->location_id);
		if(!block){*local_errno=1100;goto error;}
	
		if( lstat( block->location->data, &attribute ) != -1 )
		{
		
		temp=json_integer(attribute.st_size);
		key = string("bsize");
		if(json_object_insert_old(tmp_object, &key, temp)){free(block);goto error;}
		}
		
		free(block);
		
	}
	#endif
	
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

int fs_granted_get_blocks(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
//funkciqta trqbva da izprati pri pravilen login
/*
Request : {}
Response pri OK:

kernel.remotejson.response("1347971213468",{'blocks': [
{},{...}...
]});
*/
union json *root, *temp, *array,*tmp_object;
struct blocks_array *blocks_array = 0;
struct string key, *json_serialized=NULL;
int i;
int local_errno=0;

if(!session_is_logged_in(resources)){local_errno=1001;goto error;}
if(!access_is_session_granted(resources)){local_errno=1026;goto error;};

blocks_array = access_granted_get_blocks_array();
if(!blocks_array)goto error;

root = json_object_old(false);
array = json_array();
if(!root || !array)goto error;

for(i=0;i<blocks_array->blocks_size;i++)
	{
	tmp_object=json_object_old(false);
	
	temp=json_integer(blocks_array->blocks[i]->location_id);
	key = string("block_id");
	if(json_object_insert_old(tmp_object, &key, temp))goto error;
	
	temp=json_integer(blocks_array->blocks[i]->user_id);
	key = string("user_id");
	if(json_object_insert_old(tmp_object, &key, temp))goto error;

	temp=json_integer(blocks_array->blocks[i]->size);
	key = string("size");
	if(json_object_insert_old(tmp_object, &key, temp))goto error;
	
	temp=json_string_old(blocks_array->blocks[i]->location);
	key = string("location");
	if(json_object_insert_old(tmp_object, &key, temp))goto error;
	
	temp=json_string_old(blocks_array->blocks[i]->name);
	key = string("name");
	if(json_object_insert_old(tmp_object, &key, temp))goto error;
	
	json_array_insert_old(array, tmp_object);
	}
	
key = string("blocks");
if(json_object_insert_old(root, &key, array))goto error;
json_serialized=json_serialize(root);

if(!remote_json_send(request, response, resources, json_serialized))
{
//ERROR to return json
free(json_serialized);json_serialized=0;

return -1;
}


free_blocks_array(blocks_array);blocks_array=NULL;
return 0;

error: //ERROR tuka trqbva da vrushta json za greshka
free_blocks_array(blocks_array);
return remote_json_error(request, response, resources, local_errno);
}

int fs_storage_info(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : {"block_id" = int , "path" = string}
block_id -> block_id from fs.get_block
path -> path of the stat
*/
struct string *json_serialized=NULL;
union json *root, *item;
struct blocks *block=0;
struct string *path=0,key;
int local_errno=0;

//firstly get the block_id from the request
if(resources->auth_id)auth_id_check(resources);
if(!resources->auth && !session_is_logged_in(resources)){local_errno=1001;goto error;}

//polzvam query za da vzema arguments
if(json_type(query)!=OBJECT)goto error;

key = string("block_id");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=INTEGER)goto error;

block=access_get_blocks(resources,item->integer);
if(!block){local_errno=1100;goto error;}

key = string("path");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;

if(!access_auth_check_location(resources,&item->string_node,block->block_id)){local_errno=1101;goto error;}
path=access_fs_concat_path(block->location,&item->string_node,1);

if(!path)goto error;

#ifdef OS_WINDOWS

   typedef BOOL (WINAPI *P_GDFSE)(LPCTSTR, PULARGE_INTEGER, 
                                  PULARGE_INTEGER, PULARGE_INTEGER);
	BOOL  fResult;
    unsigned __int64 i64FreeBytesToCaller,
                       i64TotalBytes,
                       i64FreeBytes;
	P_GDFSE pGetDiskFreeSpaceEx = NULL;
	pGetDiskFreeSpaceEx = (P_GDFSE)GetProcAddress (
									   GetModuleHandle ("kernel32.dll"),
														"GetDiskFreeSpaceExA");
			  if (pGetDiskFreeSpaceEx)
			  {
				 fResult = pGetDiskFreeSpaceEx (path->data,
										 (PULARGE_INTEGER)&i64FreeBytesToCaller,
										 (PULARGE_INTEGER)&i64TotalBytes,
										 (PULARGE_INTEGER)&i64FreeBytes);
				 if (fResult)
				 {
					free(path);
					root = json_object_old(false);
					if(!root)goto error;
					key = string("total");
					json_object_insert_old(root, &key, json_integer(i64TotalBytes)); // TODO error check
					key = string("free");
					json_object_insert_old(root, &key, json_integer(i64FreeBytesToCaller)); // TODO error check
				 }
				 else
				 {
				 free(path);
				 goto error;
				 }
			  }
			  else
			  {
			  free(path);
			  goto error;	
			  }

#else
	struct statvfs info;
	bool success = !statvfs(path->data, &info);
	free(path);

if(!success)goto error;	

root = json_object_old(false);
if(!root)goto error;
key = string("total");
json_object_insert_old(root, &key, json_integer((off_t)info.f_frsize * (off_t)info.f_blocks)); // TODO error check
key = string("free");
json_object_insert_old(root, &key, json_integer((off_t)info.f_frsize * (off_t)info.f_bfree)); // TODO error check
#endif

json_serialized=json_serialize(root);
if(!json_serialized)goto error;

	if(!remote_json_send(request,response, resources, json_serialized))
	{
//ERROR to return json
		free(json_serialized);json_serialized=0;
		if(block){
			if(block->location)free(block->location);
			free(block);
		}
		return -1;
	}

	free(json_serialized);json_serialized=0;
	if(block){
	if(block->location)free(block->location);
	free(block);
	}
	return 0;

error: //ERROR tuka trqbva da vrushta json za greshka
	if(block){
	if(block->location)free(block->location);
	free(block);block=0;
	}


	if(!local_errno)local_errno=errno;
	return remote_json_error(request, response, resources, local_errno);
}

#if !defined(OS_WINDOWS)
static int stat_process(const struct file *restrict file, void *argument)
{
	struct string *buffer = argument;

	size_t length_size = format_uint_length(file->size, 10);
	size_t length_mtime = format_uint_length(file->mtime, 10);

	buffer->length = 4 + length_size + 1 + length_mtime + 1 + length_mtime + 1 + 1;
	buffer->data = malloc(buffer->length + 1);
	if (!buffer->data) return ERROR_MEMORY;

	// example result:
	// 0 d 4096 1398871760 1398871760 /

	char *start = buffer->data;
	*start++ = '0';
	*start++ = ' ';
	*start++ = ((file->type == EVFS_DIRECTORY) ? 'd' : 'f');
	*start++ = ' ';
	start = format_uint(start, file->size, 10, length_size);
	*start++ = ' ';
	start = format_uint(start, file->mtime, 10, length_mtime);
	*start++ = ' ';
	start = format_uint(start, file->mtime, 10, length_mtime);
	*start++ = ' ';
	*start++ = '/';
	*start = 0;

	return 0;
}
#endif

int fs_stat(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	// {"block_id": INTEGER, "path": STRING}

	struct string *json_serialized=NULL;
	union json *root, *item;
	struct blocks *block=0;
	struct string *path=0,key;
	int status = ERROR_MISSING;

	if (!resources->session_access && !auth_id_check(resources)) return ERROR_ACCESS;

	if (json_type(query) != OBJECT) return ERROR_MISSING;

	key = string("block_id");
	item=dict_get(query->object, &key);
	if(!item)goto error;
	if(json_type(item)!=INTEGER)goto error;
	unsigned block_id = item->integer;

	key = string("path");
	item=dict_get(query->object, &key);
	if(!item)goto error;
	if(json_type(item)!=STRING)goto error;

	block=access_get_blocks(resources,block_id);
	if(!block){status=1100;goto error;}
	if(!access_auth_check_location(resources,&item->string_node,block->block_id)){status=1101;goto error;}

	path = access_path_compose(block->location, &item->string_node, 1);
	if (block->location) free(block->location);
	free(block);
	if (!path) goto error;

	struct string buffer;
	
#if !defined(OS_WINDOWS)
    status = evfs_browse(path, 0, stat_process, &buffer, 0);
#else
	struct string *stat=0;
	stat = fs_stat_to_string_json(path,block,"/",0,0);
	if(!stat) status = -1;
	else buffer = *stat;
#endif
	free(path);
	if (status) return status;

	root = json_object_old(false);
	if (!root)
	{
		free(buffer.data);
		return ERROR_MEMORY;
	}

	key = string("data");
	json_object_insert_old(root, &key, json_string_old(&buffer));
#if !defined(OS_WINDOWS)
	free(buffer.data);
#endif

	json_serialized = json_serialize(root);
	json_free(root);
	if (!json_serialized) return ERROR_MEMORY;

	if (!remote_json_send(request,response, resources, json_serialized))
	{
		free(json_serialized);
		return -1;
	}

	free(json_serialized);
	return 0;

error: //ERROR tuka trqbva da vrushta json za greshka
	if(block)
	{
		if(block->location)free(block->location);
		free(block);block=0;
	}

	return remote_json_error(request, response, resources, status);
}

#ifdef OS_WINDOWS
static struct string *fs_find_string(struct string *restrict path,struct _stati64 *attribute)
#else
static struct string *fs_find_string(struct string *restrict path,struct stat *attribute)
#endif
{
//TODO the block might be not serialized

struct string *buffer=0;
size_t len=0;

/*
//TODO to check do I really need this check and If I do, to fix the sizes in WINDOWS
if(block->location->length > path->length)
return 0;
*/

#ifdef OS_WINDOWS
int bufsize=__int64_length(attribute->st_size, 10);
len+=bufsize;
#else
len+=integer_digits(attribute->st_size, 10);
#endif
len+=integer_digits(attribute->st_mtime, 10);
len+=integer_digits(attribute->st_ctime, 10);		
//serialized_name = string_alloc(d_name, strlen(d_name));
len+=path->length;
len+=5;//spaces,type,\t

buffer=string_alloc(0, len);
if(!buffer)return 0;


//TODO: to stop using fucken sprintf
if( attribute->st_mode & S_IFDIR )
			{
			#ifdef OS_WINDOWS
			// :@ :@ :@ :@ :@ :@ :@ :@ :@ :@ :@ :@ :@ :@
			char *tmpsizebuf=(char *)malloc(sizeof(char)*(bufsize+1));
			_i64toa(attribute->st_size,tmpsizebuf,10);
			tmpsizebuf[bufsize]=0;
			if(sprintf(buffer->data,"d %s %u %u %s",tmpsizebuf,(unsigned)attribute->st_mtime,(unsigned)attribute->st_ctime, path->data)<0)return 0;
			free(tmpsizebuf);
			}
			#else
			if(sprintf(buffer->data,"d %jd %u %u %s",(intmax_t)attribute->st_size,(unsigned)attribute->st_mtime,(unsigned)attribute->st_ctime, path->data)<0)return 0;								
			}
			#endif
else if( attribute->st_mode & S_IFREG )
			{
			#ifdef OS_WINDOWS
			char *tmpsizebuf=(char *)malloc(sizeof(char)*(bufsize+1));
			_i64toa(attribute->st_size,tmpsizebuf,10);
			tmpsizebuf[bufsize]=0;
			if(sprintf(buffer->data,"f %s %u %u %s",tmpsizebuf,(unsigned)attribute->st_mtime,(unsigned)attribute->st_ctime, path->data)<0)return 0;
			free(tmpsizebuf);
			#else
			if(sprintf(buffer->data,"f %jd %u %u %s",(intmax_t)attribute->st_size,(unsigned)attribute->st_mtime,(unsigned)attribute->st_ctime, path->data)<0)
				return 0;
			#endif
			}
else
	return 0;
			
return buffer;
}

// TODO deprecated; exists only for compatibility
int fs_find_get_results(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	// {"key": key, "min": min, "max": max}
	// key :: STRING
	// min :: INTEGER (optional; default = 0)
	// max :: INTEGER (optional; default = SIZE_MAX)

	// Returns the results in the interval [min, max).

	struct string key;
	union json *node, *item;

	size_t min, max;

	int status = ERROR_MEMORY;

	if (resources->auth_id) auth_id_check(resources);
	if (!resources->auth && !session_is_logged_in(resources)) return ERROR_ACCESS;

	if (json_type(query) != OBJECT) return ERROR_MISSING;

	key = string("min");
	item = dict_get(query->object, &key);
	if (item)
	{
		if (json_type(item) != INTEGER) goto error;
		min = item->integer;
	}
	else min = 0;

	key = string("max");
	item = dict_get(query->object, &key);
	if (item)
	{
		if (json_type(item) != INTEGER) goto error;
		max = item->integer;
	}
	else max = SIZE_MAX;

	key = string("findkey");
	node = dict_get(query->object, &key);
	if (!node) return ERROR_INPUT;

	const struct cache *cache = cache_use(node->string_node.data);
	if (!cache) return ERROR_MISSING;

	key = string("found");
	node = dict_get(((union json *)cache->value)->object, &key);
	if (!node)
	{
		cache_finish(cache);
		return ERROR_MISSING;
	}

	union json *result = json_array();

	struct string value;
	struct vector *array = &node->array_node;
	size_t index = min;
	while (index < array->length)
	{
		item = vector_get(array, index);

		value.length = item->string_node.length - 3;
		value.data = malloc(value.length);
		if (!value.data) ; // TODO
		*value.data = ((*item->string_node.data == '-') ? 'f' : *item->string_node.data);
		format_bytes(value.data + 1, item->string_node.data + 4, value.length - 1);

		result = json_array_insert(result, json_string(value.data, value.length)); // TODO error check

		free(value.data);

		if (++index == max) break;
	}

	// Add terminating entry if the search is complete.
	key = string("status");
	node = dict_get(((union json *)cache->value)->object, &key);
	if (!node->integer && (array->length < max))
		result = json_array_insert(result, json_string("", 0));

	if (!result)
	{
		cache_finish(cache);
		return ERROR_MEMORY;
	}

	struct string *json = json_serialize(result);
	json_free(result);
	cache_finish(cache);
	if (!json) return ERROR_MEMORY;

	remote_json_chunked_start(&resources->stream, request, response);
	response_content_send(&resources->stream, response, json->data, json->length);
	free(json);
	remote_json_chunked_end(&resources->stream, response);

	return 0;

error:
	return remote_json_error(request, response, resources, status);
}

/*int fs_find(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	return ffs_search(request, response, resources, query);
}*/

#if (defined(OS_MAC) && !defined(OS_IOS)) || defined(OS_WINDOWS)
int fs_find_index(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : {"block_id" = int , "path" = string, "name" = string, "min" = int, "max" = int}
block_id -> block_id from fs.get_block
path -> path of the stat
name -> what we are looking for
*/
struct string *json_serialized=NULL;
union json *item=0,*root=0,*temp=0;
struct blocks *block=0;
struct string *path=0,*name=0,*found=0,key;
int max_level=0;
int local_errno=0;
int i=0,u=0;
int rel_path_id=0;
int min=0;
int max=0;
struct string *s=0;

//firstly get the block_id from the request
//TODO to make async data return method and then I can make it with auth_id
//if(resources->auth_id)auth_id_check(resources);
if(!resources->auth && !session_is_logged_in(resources)){local_errno=1001;goto error;}

//polzvam query za da vzema arguments
if(json_type(query)!=OBJECT)goto error;

key = string("block_id");
item=dict_get(query->object, &key);
if(!item)goto error;

if(json_type(item)!=INTEGER)goto error;

block=access_get_blocks(resources,item->integer);
if(!block){local_errno=1100;goto error;}

key = string("path");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;

if(!access_auth_check_location(resources,&item->string_node,block->block_id)){local_errno=1101;goto error;}  // Auth check
path=access_fs_concat_path(block->location,&item->string_node,1);
if(block){
if(block->location)free(block->location);
free(block);block=0;
}
if(!path)goto error;

key = string("min");
item=dict_get(query->object, &key);
if(item)
{
if(json_type(item)!=INTEGER)goto error;
min=item->integer;
}

key = string("max");
item=dict_get(query->object, &key);
if(item)
{
if(json_type(item)!=INTEGER)goto error;
max=item->integer;
}

key = string("name");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;
name=&item->string_node;
if(!name)goto error;

//TODO to close the connection here !
#if defined(OS_WINDOWS)
if(name->length>MAX_PATH)goto error;
if(path->length>MAX_PATH)goto error;

for(i=0;i<path->length;i++)if(path->data[i]=='\\')path->data[i]='/';
wchar_t wname[MAX_PATH];
wchar_t wpath[MAX_PATH];

if(xutftowcs_path(wname, name->data) < 0)goto error;
if(xutftowcs_path(wpath, path->data) < 0)goto error;


struct vector *results=search_index_results(wpath,wname);
if(!results)goto error;
path->data[1]=path->data[0];
path->data[0]='/';
i=0;
#elif defined(OS_MAC)
struct string wildcard = string("*"), *suffix = string_concat(&wildcard, name, &wildcard);
struct vector *results=search_index_results(path,suffix);
free(suffix);
#endif

 //TODO possible memory leak to check do I need to free(path) in the methods
  struct search_entry *data=0;
  struct string *stat_string=0;
root=json_array();
if(!root)goto error;
for(i=min;i<results->length;i++)
 {
 if(max && i>=max)break;
 
	data=vector_get(results, i);
	//printf("%s\n",data->path);
	found=string_alloc((char *)data->path,strlen(data->path));
	if(!found)continue;
	rel_path_id=access_fs_get_relative_path(path,found,0);
	if(rel_path_id<0)continue; // TODO to check do I have to throw an error
	s = string_serialize((unsigned char *)found->data+rel_path_id, found->length-rel_path_id);
	free(found);
	stat_string=fs_find_string(s,&data->info);
	free(s);
	if(!stat_string)goto error;
	temp=json_string_old(stat_string);
	
	if(temp)json_array_insert_old(root, temp);
 }
search_index_free(results);

json_serialized=json_serialize(root);
if(!json_serialized)goto error;

json_free(root);

if(!remote_json_send(request,response, resources, json_serialized))
{
//ERROR to return json
free(json_serialized);json_serialized=0;
free(path);
return -1;
}

free(json_serialized);json_serialized=0;
free(path);
return 0;

error:
if(block){
if(block->location)free(block->location);
free(block);
}
free(path);
return remote_json_error(request, response, resources, local_errno);
}
#else
int fs_find_index(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : {"block_id" = int , "path" = string, "name" = string}
block_id -> block_id from fs.get_block
path -> path of the stat
name -> what we are looking for
*/
struct string *json_serialized=NULL;
union json *item=0,*root=0,*temp=0;
struct blocks *block=0;
struct string *path=0,*name=0,*found=0,key;
int max_level=0;
int local_errno=0;
int i=0;
int rel_path_id=0;
struct string *s=0;
return remote_json_error(request, response, resources, local_errno);
}
#endif

//TODO readdir is not thread safe
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

key = string("block_id");
item=dict_get(query->object, &key);
if(!item)goto error;

if(json_type(item)!=INTEGER)goto error;

block=access_get_blocks(resources,item->integer);
if(!block){local_errno=1100;goto error;}

key = string("path");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;

if(!access_auth_check_location(resources,&item->string_node,block->block_id)){local_errno=1101;goto error;}  // Auth check
path=access_fs_concat_path(block->location,&item->string_node,1);

if(!path)goto error;

key = string("max_level");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=INTEGER)goto error;
max_level=item->integer;
if(!max_level)goto error;

//function main

remote_json_chunked_init(request, response, resources);
fs_read_dir(path,block,0,max_level,resources, response);//TODO error handling
remote_json_chunked_close(response, resources);
if(block)
{
	free(block->location);
	free(block);
}
free(path);
return 0;

/*
union json *item,*root,*temp, *block, *path;
struct string key;
int max_level=0;
int local_errno=0;

union json *json = 0;

//firstly get the block_id from the request
if(resources->auth_id)auth_id_check(resources);
if(!resources->auth && !session_is_logged_in(resources)){local_errno=1001;goto error;}

	//polzvam query za da vzema arguments
	if (json_type(query) != OBJECT) goto error;

	key = string("block_id");
	block = dict_get(query->object, &key);
	if (!block || (json_type(block) != INTEGER)) goto error;

	key = string("path");
	path = dict_get(query->object, &key);
	if (!path || (json_type(path) != STRING)) goto error;

	key = string("max_level");
	item = dict_get(query->object, &key);
	if(!item)goto error;
	if(!item || json_type(item)!=INTEGER)goto error;
	max_level = item->integer;

	json = json_object_old(false);
	if (!json) goto error;
	if (key = string("block_id"), json_object_insert_old(json, &key, json_integer(block->integer))) goto error;
	if (key = string("path"), json_object_insert_old(json, &key, json_string_old(&path->string_node))) goto error;
	if (key = string("depth"), json_object_insert_old(json, &key, json_integer(max_level))) goto error;

	extern int ffs_list(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query);
	int status = ffs_list(request, response, resources, json);
	json_free(json);
	return status;
*/

error:
if(block){
if(block->location)free(block->location);
free(block);
}
free(path);

	if(!local_errno)local_errno=errno;
	return remote_json_error(request, response, resources, local_errno);
}

int fs_granted_list(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
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
int i=0;
int local_errno=0;

if( !session_is_logged_in(resources)){local_errno=1001;goto error;}
if( !access_is_session_granted(resources)){local_errno=1026;goto error;};

//polzvam query za da vzema arguments
if(json_type(query)!=OBJECT)goto error;

key = string("block_id");
item=dict_get(query->object, &key);
if(!item)goto error;

if(json_type(item)!=INTEGER)goto error;
struct blocks_array *blocks_array=access_granted_get_blocks_array();
for(i=0;i<blocks_array->blocks_size;i++)
{
	if(blocks_array->blocks[i]->block_id==item->integer)block=blocks_array->blocks[i];
}

if(!block){local_errno=1100;goto error;}

key = string("path");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;

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
fs_read_dir(path,block,0,max_level,resources, response);//TODO error handling
free(path);
remote_json_chunked_close(response,resources);
if(block){
if(block->location)free(block->location);
free(block);block=0;
}
return 0;

error:
if(block){
if(block->location)free(block->location);
free(block);block=0;
}
free(path);
	if(!local_errno)local_errno=errno;
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

key = string("block_id");
item=dict_get(query->object, &key);
if(!item)goto error;

if(json_type(item)!=INTEGER)goto error;
block=access_get_blocks(resources,item->integer);
if(!block){local_errno=1100;goto error;}

key = string("path");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;
if(!access_auth_check_location(resources,&item->string_node,block->block_id)){local_errno=1101;goto error;} // Auth check
path=access_fs_concat_path(block->location,&item->string_node,1);

if(!path)goto error;

//function main
#ifdef OS_WINDOWS
if ((pfd = open(path->data, O_WRONLY | O_CREAT | O_EXCL,
	S_IRUSR | S_IWUSR)) == -1)
#endif
#ifdef OS_BSD
if ((pfd = open(path->data, O_WRONLY | O_NOFOLLOW | O_CREAT | O_EXCL,
	S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1)
#endif
{
	 perror ("The following error occurred");
	//TODO ERRORS
	free(path);
	goto error;
}
close(pfd);


stat=fs_stat_to_string_json(path,block,"/",0,0);
free(path);
if(!stat)goto error;	

//end of func main

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
if(block){
if(block->location)free(block->location);
free(block);block=0;
}
return -1;
}

free(json_serialized);json_serialized=0;
if(block){
if(block->location)free(block->location);
free(block);block=0;
}
return 0;


error: //ERROR tuka trqbva da vrushta json za greshka
if(block){
if(block->location)free(block->location);
free(block);block=0;
}
free(stat);

if(!local_errno)local_errno=errno;
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

key = string("block_id");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=INTEGER)goto error;
block=access_get_blocks(resources,item->integer);
if(!block){local_errno=1100;goto error;}

key = string("path");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;
if(!access_auth_check_location(resources,&item->string_node,block->block_id)){local_errno=1101;goto error;} // Auth check
path=access_fs_concat_path(block->location,&item->string_node,1);

if(!path)goto error;

//function main
#ifdef OS_WINDOWS
int result_code = mkdir(path->data,S_IRWXU);
#endif
#ifdef OS_BSD
int result_code = mkdir(path->data, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
#endif
if(result_code){free(path);goto error;}


stat=fs_stat_to_string_json(path,block,"/",0,0);
free(path);
if(!stat)goto error;	

//end of func main

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
if(block){
if(block->location)free(block->location);
free(block);block=0;
}
return -1;
}

free(json_serialized);json_serialized=0;
if(block){
if(block->location)free(block->location);
free(block);block=0;
}
return 0;


error: 
if(block){
if(block->location)free(block->location);
free(block);block=0;
}
free(stat);

if(!local_errno)local_errno=errno;
return remote_json_error(request, response, resources, local_errno);
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
#ifdef OS_WINDOWS
struct _stati64 attribute;
#else
struct stat attribute;
#endif
int local_errno=0;

//firstly get the block_id from the request
if(resources->auth_id)auth_id_check(resources);
if(!resources->auth && !session_is_logged_in(resources)){local_errno=1001;goto error;}

if(!access_check_write_access(resources)){local_errno=1102;goto error;} //Auth check read only
//polzvam query za da vzema arguments
if(json_type(query)!=OBJECT)goto error;

key = string("block_id");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=INTEGER)goto error;
block=access_get_blocks(resources,item->integer);
if(!block){local_errno=1100;goto error;}

key = string("path");
item=dict_get(query->object, &key);
if(!item)goto error;
if(json_type(item)!=STRING)goto error;
if(!access_auth_check_location(resources,&item->string_node,block->block_id)){local_errno=1101;goto error;} // Auth check
path=access_fs_concat_path(block->location,&item->string_node,1);

if(!path)goto error;

//function main

	if( lstat( path->data, &attribute ) == -1 )goto error;

		if( attribute.st_mode & S_IFDIR )
			{ 
			#ifdef OS_WINDOWS
			if(!DeleteDirectory(path->data))goto error;	
			#else
			if(!fs_remove_dir(path,block,0))goto error;	
			#endif
			}
	else
	{
		if (remove(path->data)<0)
		{ 
		perror("ERROR fs_remove");
		goto error;
		}

	}
free(path);path=0;

//end of func main

root = json_object_old(false);
if(!root)goto error;
json_serialized=json_serialize(root);
if(!json_serialized)goto error;

if(!remote_json_send(request,response, resources, json_serialized))
{
//ERROR to return json
free(json_serialized);json_serialized=0;
if(block){
if(block->location)free(block->location);
free(block);block=0;
}
return -1;
}

free(json_serialized);json_serialized=0;
if(block){
if(block->location)free(block->location);
free(block);block=0;
}
return 0;


error: //ERROR tuka trqbva da vrushta json za greshka
if(block){
if(block->location)free(block->location);
free(block);block=0;
}
free(path);
if(!local_errno)local_errno=errno;
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
if(!resources->auth && !session_is_logged_in(resources)){local_errno=1001;goto error;}
if(!resources->auth && !session_is_logged_in(resources)){response->code = Forbidden;local_errno=1001;goto error;}
//polzvam query za da vzema arguments
if(json_type(query)!=OBJECT)
{
	response->code = NotFound;
	local_errno = 1200;
	goto error;
}

	key = string("status_key");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != STRING))
	{
		response->code = NotFound;
		local_errno = 1201;
		goto error;
	}
	status_key = &item->string_node;

//function main
	
	size = status_get(status_key, &state);
	if (size < 0) {local_errno = 1202; goto error;}

	root = json_object_old(false);
	if(!root)
	{
		response->code = InternalServerError;
		local_errno = 1203;
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
		local_errno = 1204;
		goto error;
	}

	if(!remote_json_send(request,response, resources, json_serialized))
	{
		response->code = InternalServerError;
	//ERROR to return json
	free(json_serialized);json_serialized=0;
	return -1;
	}

//end of func main

free(json_serialized);json_serialized=0;
return 0;


error: //ERROR tuka trqbva da vrushta json za greshka
if(!local_errno)local_errno=errno;
return remote_json_error(request, response, resources, local_errno);
}

int fs_remove_status(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : {"status_id" : integer}
status_id -> status id returned by copy or move

Response : {}
*/
struct string *json_serialized=NULL,key;
union json *root,*item,*temp;
unsigned long status_id=0;
int local_errno=0;

//firstly get the block_id from the request
if(resources->auth_id)auth_id_check(resources);
if(!resources->auth && !session_is_logged_in(resources)){local_errno=1001;goto error;}

//polzvam query za da vzema arguments
if(json_type(query)!=OBJECT)goto error;

key = string("status_id");
item=dict_get(query->object, &key);
if(!item)goto error;

if(json_type(item)!=INTEGER)goto error;
status_id=(unsigned long)item->integer;
if(!status_id)goto error;

//function main
	
	
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

//end of func main

free(json_serialized);json_serialized=0;
return 0;

error: //ERROR tuka trqbva da vrushta json za greshka
if(!local_errno)local_errno=errno;
return remote_json_error(request, response, resources, local_errno);
}

int fs_upload(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	/*
	Request : {"block_id" = int , "path" = string}
	block_id -> block_id from fs.get_block
	path -> path of the stat
	*/
	int status = -1;
	struct string *json_serialized=NULL;
	union json *item,*root,*temp;
	struct blocks *block=0;
	struct string *path = 0, key, value;
	int local_errno=0;
	struct string *status_key;

	if (resources->auth_id) auth_id_check(resources);
	if (!resources->auth && !session_is_logged_in(resources)) return ERROR_ACCESS;
	
	if (!access_check_write_access(resources)) return ERROR_ACCESS;
	//polzvam query za da vzema arguments
	if (json_type(query)!=OBJECT) return ERROR_INPUT;

	key = string("block_id");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != INTEGER)) return ERROR_INPUT;
	block = access_get_blocks(resources, item->integer);
	if (!block) return ERROR_MISSING;

	key = string("path");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != STRING))
	{
		status = ERROR_INPUT;
		goto finally;
	}
	if (!access_auth_check_location(resources, &item->string_node, block->block_id))
	{
		status = ERROR_ACCESS;
		goto finally;
	}
	path = access_fs_concat_path(block->location, &item->string_node, 1);
	if (!path) goto finally;

	key = string("status_key");
	item = dict_get(query->object, &key);
	if (item)
	{
		if (json_type(item) != STRING)
		{
			status = ERROR_INPUT;
			goto finally;
		}
		status_key = &item->string_node;
	}
	else status_key = 0;

	struct string *filename;

	key = string("filename");
	item = dict_get(query->object, &key);
	if (item) // HTML 5 file upload
	{
		if (json_type(item) != STRING)
		{
			status = ERROR_INPUT;
			goto finally;
		}
		filename = &item->string_node;
	}
	else filename = 0;

	if (request->method == METHOD_POST)
		status = http_upload(path, filename, request, &resources->stream, false, false, status_key);
	else
		status = 0;

	// TODO: http_upload returns OK if everything went right. maybe the code below needs to be changed
	if (!status)
	{
		// Redirect if the client specified so
		key = string("redirect");
		item = dict_get(query->object, &key);
		if (item)
		{
			if (json_type(item) == STRING)
			{
				key = string("Location");
				response_header_add(response, &key, &item->string_node); // TODO: this may fail
				status = MovedPermanently;
			}
			else status = NotFound; // TODO: change this
		}
		else status = OK;
	}

	key = string("Connection");
	value = string("close");
	if (!response_header_add(response, &key, &value))
	{
		status = ERROR_MEMORY;
		goto finally;
	}

	response->code = status;
	if (!response_headers_send(&resources->stream, request, response, 0))
	{
		status = -1; // TODO change this
		goto finally;
	}
	stream_term(&resources->stream);
	status = ERROR_CANCEL;

finally:
	if (block) free(block->location);
	free(block);
	free(path);

	return status;
}

int fs_save(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
	// {"block_id": INTEGER, "path": STRING, "append": BOOLEAN, "keep": BOOLEAN}

	int status = -1;
	union json *item;
	struct blocks *block = 0;
	struct string *path = 0, key, value;
	bool append = false, keep = false;
	struct string *filename = 0;

	if (resources->auth_id) auth_id_check(resources);
	if (!resources->auth && !session_is_logged_in(resources)) return Forbidden;
	if (!access_check_write_access(resources)) return Forbidden;

	if (json_type(query) != OBJECT) return NotFound;

	key = string("append");
	item = dict_get(query->object, &key);
	if (item && (json_type(item) == BOOLEAN)) append = item->boolean;

	key = string("keep");
	item = dict_get(query->object, &key);
	if (item && (json_type(item) == BOOLEAN)) keep = item->boolean;

	key = string("filename");
	item = dict_get(query->object, &key);
	if (item && (json_type(item) == STRING)) filename = &item->string_node; // used for HTML 5 upload

	key = string("block_id");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != INTEGER)) return NotFound;
	block = access_get_blocks(resources, item->integer);
	if (!block) return NotFound;

	key = string("path");
	item = dict_get(query->object, &key);
	if (!item || (json_type(item) != STRING))
	{
		status = NotFound;
		goto finally;
	}
	if (!access_auth_check_location(resources, &item->string_node, block->block_id))
	{
		status = Forbidden;
		goto finally;
	}
	path = access_fs_concat_path(block->location, &item->string_node, 1);
	if (!path) goto finally;

	status = http_upload(path, filename, request, &resources->stream, append, keep, 0);

	// TODO: http_upload returns OK if everything went right. maybe the code below needs to be changed
	if (!status)
	{
		// Redirect if the client specified so.
		key = string("redirect");
		item = dict_get(query->object, &key);
		if (item && (json_type(item) == STRING))
		{
			key = string("Location");
			response_header_add(response, &key, &item->string_node); // TODO: this may fail
			status = MovedPermanently;
		}
		else status = OK;
	}

	key = string("Connection");
	value = string("close");
	if (!response_header_add(response, &key, &value))
	{
		status = -1;
		goto finally;
	}

	response->code = status;
	if (!response_headers_send(&resources->stream, request, response, 0)) goto finally;
	stream_term(&resources->stream);
	status = -1;

finally:
	if (block->location) free(block->location);
	free(block);
	free(path);

	return status;
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
	int nodisp=0;
	//firstly get the block_id from the request
	if(resources->auth_id)auth_id_check(resources);
	if(!resources->auth && !session_is_logged_in(resources)) return NotFound;
	//polzvam query za da vzema arguments
	if(json_type(query)!=OBJECT) return BadRequest;

	key = string("block_id");
	item=dict_get(query->object, &key);
	if (!item || (json_type(item) != INTEGER)) return BadRequest;
	block=access_get_blocks(resources,item->integer);
	if(!block) return NotFound;

	key = string("path");
	item=dict_get(query->object, &key);
	if (!item || (json_type(item) != STRING)) return BadRequest;

	if(!access_auth_check_location(resources,&item->string_node,block->block_id)) {if(block){if(block->location)free(block->location);free(block);block=0;}return NotFound;} // Auth check
	path=access_fs_concat_path(block->location,&item->string_node,1);
	if(block->location){free(block->location);free(block);block=0;}
	if (!path) return NotFound;

	key = string("nodisp");
	item=dict_get(query->object, &key);
	if (item && (json_type(item) == INTEGER))nodisp=1;
	
	// Set Content-Disposition
	if(!nodisp)
	{
	struct string *disposition = content_disposition(path);
	if (!disposition) return InternalServerError; // TODO: this may be NotFound (check the code above to make sure)
	key = string("Content-Disposition");
	response_header_add(response, &key, disposition);
	free(disposition);
	}
	
	status = http_download(path, request, response, &resources->stream);
	free(path);
	return status;
}

#if defined(FILEMENT_THUMBS)
static void create_thumb(struct string *path,struct string *buffer,int max_width,int max_height,int quality)
{

Epeg_Image *im;
int w, h;
int thumb_width, thumb_height;
int max_dimension;
# ifndef MIN
#  define MIN(X,Y) ((X) < (Y) ? (X) : (Y))
# endif

buffer->data = 0;
buffer->length = 0;

 im = epeg_file_open(path->data); 
if(!im)return ; 
epeg_size_get(im, &w, &h);
/*
max_dimension=MIN(max_width,max_height); 
	   if (w > h) {
	       thumb_width = max_dimension;
	       thumb_height = max_dimension * h / w;
	   } else {
	       thumb_height = max_dimension;
	       thumb_width = max_dimension * w / h;
	   }
*/
if(!w || !h)return ;
if (!max_width) { max_width = w; }
if (!max_height) { max_height = h; }  
double ratio=1;
double ratiow=0,ratioh=0;
   if(w > max_width || h > max_height){
   ratiow=max_width/(double)w;
   ratioh=max_height/(double)h;
   ratio=MIN(ratiow,ratioh);
   }

# if defined(OS_ANDROID)
#  undef round
#  define round(n) (int)((n) + 0.5)
# endif
   
   thumb_width=(int)round(w*ratio);
   thumb_height=(int)round(h*ratio);

	int length = 0;
	buffer->data = 0;
  
   epeg_decode_size_set(im, thumb_width, thumb_height);
   epeg_quality_set               (im, quality);
   epeg_thumbnail_comments_enable (im, 0);
   epeg_memory_output_set 		  (im, (unsigned char **)&buffer->data, &length);
   epeg_encode                    (im);
   epeg_close                     (im);
   

	buffer->length = length;

return ;
}

int fs_create_thumb(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : {"block_id" = int , "path" = string, "save_path":string, "save_file":string, "max_width" = int, "max_height" = int "quality"=int}
block_id -> block_id from fs.get_block
path -> path of the stat
*/
	union json *item;
	struct blocks *block=0;
	struct string *path=0,*save_path=0,*tmp=0,*save_file=0,key;
	int status=0;
	int max_width=0;
	int max_height=0;
	int quality=0;
	int save_file_fd=-1;
	struct string buffer;
	buffer.data=0;
	//firstly get the block_id from the request
	if(resources->auth_id)auth_id_check(resources);
	if(!resources->auth && !session_is_logged_in(resources)) return NotFound;
	//polzvam query za da vzema arguments
	if(json_type(query)!=OBJECT) return BadRequest;

	key = string("block_id");
	item=dict_get(query->object, &key);
	if (!item || (json_type(item) != INTEGER)) return BadRequest;
	block=access_get_blocks(resources,item->integer);
	if(!block) return NotFound;
	
	key = string("max_width");
	item=dict_get(query->object, &key);
	if (item)
	{
	if (json_type(item) != INTEGER) return BadRequest;
	max_width=item->integer;
	if(!max_width) return NotFound;
	}
	else max_width=0;
	
	key = string("max_height");
	item=dict_get(query->object, &key);
	if (item)
	{
	if(json_type(item) != INTEGER) return BadRequest;
	max_height=item->integer;
	if(!max_height) return NotFound;
	}
	else max_height=0;
	
	if(!max_height && !max_width) return NotFound;
	
	key = string("quality");
	item=dict_get(query->object, &key);
	if (item)
	{
	if ( (json_type(item) != INTEGER)) return BadRequest;
	quality=item->integer;
	if(!quality) return NotFound;	
	}
	else quality=85;

	#if defined(PUBCLOUD) || defined(OS_ANDROID) || defined(OS_QNX)
	key = string("save_path");
	item=dict_get(query->object, &key);
	if(item)
	{
		if(json_type(item)!=STRING) return BadRequest;
		save_path=access_fs_concat_path(block->location,&item->string_node,1);
		if(!save_path) return BadRequest;
		
		key = string("save_file");
		item=dict_get(query->object, &key);
		if(!item || json_type(item)!=STRING){ free(save_path); return BadRequest;}
		path=access_fs_concat_path(save_path,&item->string_node,1);
		if(!path)return BadRequest;
		save_file=&item->string_node;
		
		key = string("/.filement_thumbs/");
		tmp=string_concat(save_path,&key);
		free(save_path);
		if(!tmp)return BadRequest;
		mkdir(tmp->data,S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH); 

		save_path=string_concat(tmp,save_file);
		free(tmp);tmp=0;
		if(!save_path)return BadRequest;

		if( access( save_path->data, F_OK ) != -1 ) {
			status = http_download(save_path, request, response, &resources->stream);

			free(save_path);
			free(path);
			return status;
		}
	
		#ifdef OS_BSD
		save_file_fd = creat(save_path->data, 0644);
		#else
		save_file_fd = open(save_path->data, O_CREAT | O_WRONLY | O_TRUNC | O_BINARY, 0644);
		#endif
	}
	else // no save 
	{
	#endif
	key = string("path");
	item=dict_get(query->object, &key);
	if(!item) return BadRequest;
	if ( json_type(item) != STRING) return BadRequest;

	if(!access_auth_check_location(resources,&item->string_node,block->block_id)) {if(block){if(block->location)free(block->location);free(block);block=0;}return NotFound;} // Auth check
	path=access_fs_concat_path(block->location,&item->string_node,1);
	if(block->location){free(block->location);}
	free(block);block=0;
	if (!path) return NotFound;
	#if defined(PUBCLOUD) || defined(OS_ANDROID) || defined(OS_QNX)
	}
	#endif

	//TODO no point of making 2 times open 1st for getting the mime type, second from create_thumb
	#ifdef OS_BSD
	int file = open(path->data, O_RDONLY | O_NOFOLLOW);
	#else
	int file = open(path->data, O_RDONLY | O_BINARY);
	#endif
	if (file < 0)goto error;
	
	const struct string *type;
	char magic_buffer[MAGIC_SIZE];
	ssize_t size;
	size = read(file, magic_buffer, MAGIC_SIZE);
	close(file);
	if (size < 0) type = &type_unknown;
	else type = mime(magic_buffer, size);
	key = string("image/jpeg");
	if(string_diff(type,&key))goto error;
 
	create_thumb(path,&buffer,max_width,max_height,quality);

	if(!buffer.length || !buffer.data){goto error;}

	key = string("Content-Type");
	if (!response_header_add(response, &key, type))
	{
	   goto error;
	}
	response->code = OK;
	if (!response_headers_send(&resources->stream, request, response, buffer.length))
	{
	 goto error;
	}
	
	status=response_content_send(&resources->stream, response, buffer.data, buffer.length);

	free(path);
	
	#if defined(PUBCLOUD) || defined(OS_ANDROID) || defined(OS_QNX)
	if(save_file_fd>0 && status){
		writeall(save_file_fd, buffer.data, buffer.length);
	}
	#endif
	free(buffer.data);
	free(save_path);
	if(save_file_fd>0){close(save_file_fd);}
	return 0;

error:

	if(save_file_fd>0){
		close(save_file_fd);
		if(save_path)unlink(save_path->data);
	} 
	free(buffer.data);
	free(path);
	free(save_path);
	return BadRequest;
}
#endif
