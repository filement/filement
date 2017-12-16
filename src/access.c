#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <ctype.h>

#if defined(OS_LINUX) || defined(OS_ANDROID)
# include <sys/fsuid.h>
#endif

#if defined(OS_ANDROID)
# include <sys/types.h>
# include <dirent.h>
#endif

#ifdef OS_WINDOWS
#include <windows.h>
#include <shlobj.h>
#include "mingw.h"
#endif

#include "types.h"
#include "format.h"
#include "protocol.h"
#include "io.h"
#include "actions.h"
#include "storage.h"

#ifdef OS_WINDOWS
_CRTIMP char* __cdecl __MINGW_NOTHROW	_i64toa(__int64, char *, int);
size_t __int64_length(__int64 number, unsigned base);

WINBASEAPI BOOL WINAPI MoveFileWithProgressA(LPCSTR,LPCSTR,LPPROGRESS_ROUTINE,LPVOID,DWORD);
WINBASEAPI BOOL WINAPI MoveFileWithProgressW(LPCWSTR,LPCWSTR,LPPROGRESS_ROUTINE,LPVOID,DWORD);
#define MoveFileWithProgress MoveFileWithProgressA
#endif

static bool session_is_logged_in(struct resources *restrict resources)
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

// Generates a normalized path corresponding to the relative path.
// TODO relative is not really relative; rename it
static ssize_t normalize(unsigned char *restrict path, const char *relative, size_t length)
{
	size_t start = 0, index = 0;

	// assert(length && (relative[0] == '/'));

	// Instead of / as path separator, put the offset from the previous path separator (this allows easy handling of .. path component).

	size_t offset = 0;
	for(; 1; ++offset)
	{
		if ((offset == length) || (relative[offset] == '/')) // end of path component
		{
			switch (index - start)
			{
			case 1:
				if (offset == length) goto finally; // TODO remove code duplication - this line is repeated 4 times
				continue; // skip repeated /

			case 2: // check for .
				if (relative[offset - 1] != '.') break;
				index -= 1;
				if (offset == length) goto finally;
				continue;

			case 3: // check for ..
				if ((relative[offset - 2] == '.') && (relative[offset - 1] == '.'))
				{
					// .. in the root directory points to the same directory
					if (start) start -= path[start];
					index = start + 1;
					if (offset == length) goto finally;
					continue;
				}
				break;
			}

			if (offset == length) break;

			path[index] = index - start;
			start = index;
		}
		else path[index] = relative[offset];

		index += 1;
		if ((index - start) > (unsigned char)-1) return -1; // path component too long
	}

finally:

	length = index;

	// Restore the path component separators to slashes.
	do
	{
		index = start;
		start -= path[index];
		path[index] = '/';
	} while (index);

	// Return path length without terminating slash.
	return (length - (path[length - 1] == '/'));
}

#if defined(DEVICE)
#ifdef OS_WINDOWS
extern struct string UUID_WINDOWS;
#else
extern struct string UUID;
#endif
bool access_check_general(const struct string *action_name,const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
struct string key;
union json *temp, *item;
struct string UUID_TMP=string("00000000000000000000000000000000");

#ifdef OS_WINDOWS
if(UUID_WINDOWS.data && !memcmp(UUID_WINDOWS.data, UUID_TMP.data, UUID_SIZE * 2))
#else
if(UUID.data && !memcmp(UUID.data, UUID_TMP.data, UUID_SIZE * 2))
#endif
	{
		if(key = string("ping"), string_equal(action_name, &key))return true;
		else if(key = string("config.register"), string_equal(action_name, &key))return true;
		return false;
	}

if(!session_is_logged_in(resources))return true; //TODO change this, because of auth key possibly
//TODO check for auth_id
if(key = string("ping"), string_equal(action_name, &key))return true;
else if(key = string("session.login"), string_equal(action_name, &key))return true;
else if(key = string("session.grant_login"), string_equal(action_name, &key))return true;
else if(key = string("config.info"), string_equal(action_name, &key))return true;
else if(key = string("session.logout"), string_equal(action_name, &key))return true;
else if(key = string("proxy.reset"), string_equal(action_name, &key))return true;

if(resources->session_access && json_type(resources->session.ro)!=OBJECT)return false;
key = string("authenticated");
item=dict_get(resources->session.ro->object, &key);
	if(item)
	{
		if(json_type(item)==INTEGER && item->integer) return true;
		else return false;
	}
	else
	{
		return false;
	}

return false;
}
#endif

bool access_is_session_granted(struct resources *restrict resources)
{
struct string key;
union json *item;

if(!resources->session_access || json_type(resources->session.ro)!=OBJECT)return false;

key = string("grant");
item=dict_get(resources->session.ro->object, &key);
if(!item || json_type(item)!=INTEGER)return false;

if(item->integer)return true;

return false;
}

int access_fs_get_relative_path(struct string *restrict initial_path,struct string *restrict path,bool win_slash)
{
if(initial_path->length > path->length)return -1;
//TODO to check do i have to make more checks because the path is made with fs_concat
if(win_slash)
{
if(path->data[initial_path->length-1]=='\\')return initial_path->length-1;
else if(path->data[initial_path->length]=='\\')return initial_path->length;
}
else
{
if(path->data[initial_path->length-1]=='/')return initial_path->length-1;
if(path->data[initial_path->length]=='/')return initial_path->length;
}

return -1;
}

#if !defined(OS_WINDOWS)
struct string *access_path_compose(struct string *restrict prefix, struct string *restrict suffix, bool modify)
{
	if (!prefix || !suffix) return 0;

	if (!prefix->length || (prefix->data[0] != '/')) return 0;
	if (suffix->length && (suffix->data[0] != '/')) return 0;

	// remove terminating / from prefix and suffix
	if (prefix->data[prefix->length - 1] == '/') prefix->length -= 1;
	if (suffix->length && (suffix->data[suffix->length - 1] == '/')) suffix->length -= 1;

	// allocate memory for path; make sure there is space for . if the path is the root directory
	struct string *path = malloc(sizeof(struct string) + prefix->length + 1 + suffix->length + 1);
	if (!path) return 0;
	path->data = (char *)(path + 1);

	// Form absolute path.
	char *start;
	if (prefix->length) start = format_bytes(path->data, prefix->data, prefix->length);
	else start = path->data;
	if (suffix->length)
	{
		ssize_t length = normalize(start, suffix->data, suffix->length);
		if (length < 0) // invalid path
		{
			free(path);
			return 0;
		}
		start += length;
	}

	// Handle empty path.
	if (start == path->data)
	{
		*start++ = '/';
		*start++ = '.';
	}

	path->length = start - path->data;
	path->data[path->length] = 0;
	return path;
}
#else /* defined(OS_WINDOWS) */
struct string *access_path_compose(struct string *restrict core_path, struct string *restrict path, bool modify)
{
	if (!core_path || !path) return 0;

	struct string left = *core_path, separator = string("/"), right = *path;
	struct string *con_path;

	// assert(core_path->length && (cora_path->data[0] == '/'));

	// Make sure there is no slash between the left and right part and no slash at the end.

	if (left.data[left.length - 1] == '/')
	{
		if (left.length > 1) left.length -= 1;
		else left = string("/."); // final path must not end in /
	}

	while (right.length)
	{
		if (right.data[0] == '/')
		{
			right.data += 1;
			right.length -= 1;
			if (!right.length) break;
		}

		if (right.data[right.length - 1] == '/')
		{
			right.length -= 1;
			if (!right.length) break;
		}

		// Obtain path by concatenation.
		con_path = string_concat(&left, &separator, &right);
		goto ready;
	}

	con_path = string_alloc(left.data, left.length);

ready:

# if defined(FTP)
	if (modify)
	{
		struct string *temp = con_path;
		con_path = string_concat(temp, &separator);
		free(temp);
		if (!con_path) return 0;
	}
# endif
	
# if defined(OS_WINDOWS)
if (!modify)
# endif
	{
		char *up = strstr(con_path->data, "/..");
		if (up)
		{
			size_t index = up + sizeof("/..") - 1 - con_path->data;
			if ((index == con_path->length) || (con_path->data[index] == '/')) goto error;
		}
		return con_path;
	}

# ifdef OS_WINDOWS
	if(modify)
	{
	if(con_path->length<2){free(con_path);return 0;}
	if(!isascii(con_path->data[1])){free(con_path);return 0;}
	con_path->data[0]=con_path->data[1];
	con_path->data[1]=':';

	int i;
	for(i=2;i<con_path->length;i++)
		if(con_path->data[i]=='/')con_path->data[i]='\\';

	if (con_path->length < 3)
	{
		struct string *temp = con_path, sep = string("\\");
		con_path = string_concat(temp, &sep);
		free(temp);
		if (!con_path) return 0;
	}

	if((*con_path->data != '.') && !strstr(con_path->data, "\\."))
		return con_path;
	}
# endif

error:
	free(con_path);
	return 0;
}
#endif

struct string *access_fs_concat_path(struct string *restrict core_path, struct string *restrict path, bool modify)
{
	if (!core_path || !path) return 0;

	struct string left = *core_path, separator = string("/"), right = *path;
	struct string *con_path;

	// assert(core_path->length && (cora_path->data[0] == '/'));

	// Make sure there is no slash between the left and right part and no slash at the end.

	if (left.data[left.length - 1] == '/')
	{
		if (left.length > 1) left.length -= 1;
		else left = string("/."); // final path must not end in /
	}

	while (right.length)
	{
		if (right.data[0] == '/')
		{
			right.data += 1;
			right.length -= 1;
			if (!right.length) break;
		}

		if (right.data[right.length - 1] == '/')
		{
			right.length -= 1;
			if (!right.length) break;
		}

		// Obtain path by concatenation.
		con_path = string_concat(&left, &separator, &right);
		goto ready;
	}

	con_path = string_alloc(left.data, left.length);

ready:

#if defined(FTP)
	if (modify)
	{
		struct string *temp = con_path;
		con_path = string_concat(temp, &separator);
		free(temp);
		if (!con_path) return 0;
	}
#endif
	
#if defined(OS_WINDOWS)
if (!modify)
#endif
	{
		char *up = strstr(con_path->data, "/..");
		if (up)
		{
			size_t index = up + sizeof("/..") - 1 - con_path->data;
			if ((index == con_path->length) || (con_path->data[index] == '/')) goto error;
		}
		return con_path;
	}

#ifdef OS_WINDOWS
	if(modify)
	{
	if(con_path->length<2){free(con_path);return 0;}
	if(!isascii(con_path->data[1])){free(con_path);return 0;}
	con_path->data[0]=con_path->data[1];
	con_path->data[1]=':';

	int i;
	for(i=2;i<con_path->length;i++)
		if(con_path->data[i]=='/')con_path->data[i]='\\';

	if (con_path->length < 3)
	{
		struct string *temp = con_path, sep = string("\\");
		con_path = string_concat(temp, &sep);
		free(temp);
		if (!con_path) return 0;
	}

	if((*con_path->data != '.') && !strstr(con_path->data, "\\."))
		return con_path;
	}
#endif

error:
	free(con_path);
	return 0;
}

bool access_check_host(struct string *host)
{
//TODO to make better host validation (no dot to dot)
size_t i=0;
for(;i<host->length;i++)
	{
	if(!isalnum(*(host->data+i)) && (*(host->data+i) != '-') && (*(host->data+i) != '.'))return false;
	}

return true;
}

#if defined(DEVICE) || defined(PUBCLOUD) || defined(FTP)
struct blocks_array *access_get_blocks_array(struct resources *restrict resources)//TODO users
{
int i=0,u=0;
struct blocks_array *blocks_array=0;
struct blocks *blocks = 0;
union json *item=0;
struct string key;
long user_id=0;

if(resources->auth && resources->auth->blocks_array && resources->auth->blocks_array->blocks_size)
{
	blocks_array=(struct blocks_array *)malloc( sizeof(struct blocks_array) * (sizeof(struct blocks *)*resources->auth->blocks_array->blocks_size) );
	if(!blocks_array)return 0;
	memset(blocks_array,0, (sizeof(struct blocks_array) * (sizeof(struct blocks *)*resources->auth->blocks_array->blocks_size)) );
	for(i=0;i<resources->auth->blocks_array->blocks_size;i++)
	{
	blocks_array->blocks[u++]=resources->auth->blocks_array->blocks[i];
	blocks_array->blocks_size=u;
	}
}
# ifdef FTP
else
{ 
	blocks_array=(struct blocks_array *)malloc(sizeof(struct blocks_array) + sizeof(struct blocks *));
	blocks_array->blocks_size=1;
	blocks_array->blocks[0]=(struct blocks *)malloc(sizeof(struct blocks));
	blocks_array->blocks[0]->block_id = 1;
	blocks_array->blocks[0]->user_id = 0;
	blocks_array->blocks[0]->location = string_alloc("/",1);
	blocks_array->blocks[0]->size = 0;
	blocks_array->blocks[0]->name = string_alloc("",0);
}
# else
else if(resources->session_access && json_type(resources->session.ro) == OBJECT)
{
	key = string("user_id");
	item=dict_get(resources->session.ro->object, &key);
	if(!item)return 0;
	if(json_type(item)!=INTEGER)return 0;
	blocks_array=storage_blocks_get_blocks(resources->storage,item->integer);
}
# endif
return blocks_array;
}
#endif

void free_blocks_array(struct blocks_array *blocks_array)
{
    int i;
    if(!blocks_array)return;
    for(i=0;i<blocks_array->blocks_size;i++)
    {
        free(blocks_array->blocks[i]->location);
        free(blocks_array->blocks[i]);
    }
    free(blocks_array);
}

struct blocks_array *access_granted_get_blocks_array(void)//TODO users
{
	int i = 0, u = 0;
	struct blocks_array *blocks_array = 0;
	struct blocks *blocks = 0;
	
	
#if defined(OS_WINDOWS)
	CHAR tmp_data[MAX_PATH];
	HRESULT result;
	DWORD uDriveMask = GetLogicalDrives();
	char driver_str[4];
	int drive = 65; // intial drive A;

	if (uDriveMask == 0)
	{
		blocks_array=(struct blocks_array *)malloc(sizeof(struct blocks_array) + sizeof(struct blocks *));
		blocks_array->blocks_size=1;
		blocks_array->blocks[0]=(struct blocks *)malloc(sizeof(struct blocks));
		// TODO error check
		blocks_array->blocks[0]->block_id = 1;
		blocks_array->blocks[0]->location_id = 1;
		blocks_array->blocks[0]->user_id = 0;
		blocks_array->blocks[0]->location = string_alloc("/C/",3);
		blocks_array->blocks[0]->size = 0;
		blocks_array->blocks[0]->name = string_alloc("",0);
		return blocks_array;
	}
	else
	{
		for (i=0; i < 32; ++i)
		{
			if ((uDriveMask & 1) == 1)u++;
			uDriveMask >>= 1;
		}
		blocks_array=(struct blocks_array *)malloc(sizeof(struct blocks_array) + sizeof(struct blocks *)*(u+6));
		// TODO error check
		uDriveMask = GetLogicalDrives();
		blocks_array->blocks_size=u+6;
		for (i=0,u=0; i < 32; ++i)
		{
			if ((uDriveMask & 1) == 1)
			{
				blocks_array->blocks[u]=(struct blocks *)malloc(sizeof(struct blocks));
				// TODO error check
				blocks_array->blocks[u]->block_id = u+1;
				blocks_array->blocks[u]->location_id = u+1;
				blocks_array->blocks[u]->user_id = 0;
				snprintf(driver_str, 4,"/%c/",drive);
				blocks_array->blocks[u]->location = string_alloc(driver_str,3);
				blocks_array->blocks[u]->size = 0;
				blocks_array->blocks[u]->name = string_alloc("",0);
				u++;	
			}
			++drive;
			uDriveMask >>= 1;
		}
		
		
		 result = SHGetFolderPath(NULL, CSIDL_PROFILE, NULL, 0, tmp_data);
		if (result == S_OK)
		{
				blocks_array->blocks[u]=(struct blocks *)malloc(sizeof(struct blocks));
				// TODO error check
				blocks_array->blocks[u]->block_id = u+1;
				blocks_array->blocks[u]->location_id = u+1;
				blocks_array->blocks[u]->user_id = 0;
				tmp_data[1]=tmp_data[0];
				tmp_data[0]='/';
				tmp_data[2]='/';
				for(i=strlen(tmp_data)-1;i>=0;i--)
					{
					if(tmp_data[i]=='\\')tmp_data[i]='/';
					}
				blocks_array->blocks[u]->location = string_alloc(tmp_data,strlen(tmp_data));
				blocks_array->blocks[u]->size = 0;
				blocks_array->blocks[u]->name = string_alloc("User",sizeof("User")-1);
				u++;
		}
		
		 result = SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, 0, tmp_data);
		if (result == S_OK)
		{
				blocks_array->blocks[u]=(struct blocks *)malloc(sizeof(struct blocks));
				// TODO error check
				blocks_array->blocks[u]->block_id = u+1;
				blocks_array->blocks[u]->location_id = u+1;
				blocks_array->blocks[u]->user_id = 0;
				tmp_data[1]=tmp_data[0];
				tmp_data[0]='/';
				tmp_data[2]='/';
				for(i=strlen(tmp_data)-1;i>=0;i--)
					{
					if(tmp_data[i]=='\\')tmp_data[i]='/';
					}
				blocks_array->blocks[u]->location = string_alloc(tmp_data,strlen(tmp_data));
				blocks_array->blocks[u]->size = 0;
				blocks_array->blocks[u]->name = string_alloc("My Documents",sizeof("My Documents")-1);
				u++;
		}
		
		 result = SHGetFolderPath(NULL, CSIDL_DESKTOP, NULL, 0, tmp_data);
		if (result == S_OK)
		{
				blocks_array->blocks[u]=(struct blocks *)malloc(sizeof(struct blocks));
				// TODO error check
				blocks_array->blocks[u]->block_id = u+1;
				blocks_array->blocks[u]->location_id = u+1;
				blocks_array->blocks[u]->user_id = 0;
				tmp_data[1]=tmp_data[0];
				tmp_data[0]='/';
				tmp_data[2]='/';
				for(i=strlen(tmp_data)-1;i>=0;i--)
					{
					if(tmp_data[i]=='\\')tmp_data[i]='/';
					}
				blocks_array->blocks[u]->location = string_alloc(tmp_data,strlen(tmp_data));
				blocks_array->blocks[u]->size = 0;
				blocks_array->blocks[u]->name = string_alloc("Desktop",sizeof("Desktop")-1);
				u++;
		}
		
		 result = SHGetFolderPath(NULL, CSIDL_MYMUSIC, NULL, 0, tmp_data);
		if (result == S_OK)
		{
				blocks_array->blocks[u]=(struct blocks *)malloc(sizeof(struct blocks));
				// TODO error check
				blocks_array->blocks[u]->block_id = u+1;
				blocks_array->blocks[u]->location_id = u+1;
				blocks_array->blocks[u]->user_id = 0;
				tmp_data[1]=tmp_data[0];
				tmp_data[0]='/';
				tmp_data[2]='/';
				for(i=strlen(tmp_data)-1;i>=0;i--)
					{
					if(tmp_data[i]=='\\')tmp_data[i]='/';
					}
				blocks_array->blocks[u]->location = string_alloc(tmp_data,strlen(tmp_data));
				blocks_array->blocks[u]->size = 0;
				blocks_array->blocks[u]->name = string_alloc("My Music",sizeof("My Music")-1);
				u++;
		}
		
		 result = SHGetFolderPath(NULL, CSIDL_MYVIDEO, NULL, 0, tmp_data);
		if (result == S_OK)
		{
				blocks_array->blocks[u]=(struct blocks *)malloc(sizeof(struct blocks));
				// TODO error check
				blocks_array->blocks[u]->block_id = u+1;
				blocks_array->blocks[u]->location_id = u+1;
				blocks_array->blocks[u]->user_id = 0;
				tmp_data[1]=tmp_data[0];
				tmp_data[0]='/';
				tmp_data[2]='/';
				for(i=strlen(tmp_data)-1;i>=0;i--)
					{
					if(tmp_data[i]=='\\')tmp_data[i]='/';
					}
				blocks_array->blocks[u]->location = string_alloc(tmp_data,strlen(tmp_data));
				blocks_array->blocks[u]->size = 0;
				blocks_array->blocks[u]->name = string_alloc("My Videos",sizeof("My Videos")-1);
				u++;
		}
		
		 result = SHGetFolderPath(NULL, CSIDL_MYPICTURES, NULL, 0, tmp_data);
		if (result == S_OK)
		{
				blocks_array->blocks[u]=(struct blocks *)malloc(sizeof(struct blocks));
				// TODO error check
				blocks_array->blocks[u]->block_id = u+1;
				blocks_array->blocks[u]->location_id = u+1;
				blocks_array->blocks[u]->user_id = 0;
				tmp_data[1]=tmp_data[0];
				tmp_data[0]='/';
				tmp_data[2]='/';
				for(i=strlen(tmp_data)-1;i>=0;i--)
					{
					if(tmp_data[i]=='\\')tmp_data[i]='/';
					}
				blocks_array->blocks[u]->location = string_alloc(tmp_data,strlen(tmp_data));
				blocks_array->blocks[u]->size = 0;
				blocks_array->blocks[u]->name = string_alloc("My Pictures",sizeof("My Pictures")-1);
				u++;
		}
		
		return blocks_array;
	}

#elif defined(OS_ANDROID)
	DIR *dir=0,*dirtest=0; 
	struct dirent *entry=0,*entrytest=0;
	int test=0;
	i=0;
		if( ( dir = opendir( "/mnt/" ) ) != NULL ) {	
		//TODO readdir is not thread safe
			while( ( entry = readdir( dir ) ) != NULL ) {
					if( strcmp( (char*)entry->d_name, "." ) == 0 )continue;
					if( strcmp( (char*)entry->d_name, ".." ) == 0 )continue;
					if( strcmp( (char*)entry->d_name, "obb" ) == 0 )continue;
					if( strcmp( (char*)entry->d_name, "asec" ) == 0 )continue;
					if( strcmp( (char*)entry->d_name, "secure" ) == 0 )continue;
				i++;
			}
			closedir(dir);
		}	
		if(!i)
		{
			blocks_array=(struct blocks_array *)malloc(sizeof(struct blocks_array) + sizeof(struct blocks *));
			blocks_array->blocks_size=1;
		    blocks_array->blocks[0]=(struct blocks *)malloc(sizeof(struct blocks));
            blocks_array->blocks[0]->block_id = 1;
			blocks_array->blocks[0]->location_id = 1;
			blocks_array->blocks[0]->user_id = 0;
			blocks_array->blocks[0]->location = string_alloc("/",1);
			blocks_array->blocks[0]->size = 0;
			return blocks_array;
		}
		
		blocks_array=(struct blocks_array *)malloc(sizeof(struct blocks_array) + sizeof(struct blocks *)*i);
		i=0;
		blocks_array->blocks_size=0;
	char dirname[4096];
	char linkname[4096];
	memcpy(dirname,"/mnt/",5);
	int len=0;
	int linklen=0;
	
		if( ( dir = opendir( "/mnt/" ) ) != NULL ) {		
		//TODO readdir is not thread safe
			while( ( entry = readdir( dir ) ) != NULL ) {
					if( strcmp( (char*)entry->d_name, "." ) == 0 )continue;
					if( strcmp( (char*)entry->d_name, ".." ) == 0 )continue;
					if( strcmp( (char*)entry->d_name, "asec" ) == 0 )continue;
					if(entry->d_type==DT_DIR || entry->d_type==DT_LNK)
						{
							
							len=strlen((char*)entry->d_name);
							//*format(dirname, str("mnt", 5), str((char*)entry->d_name, len)) = 0;
							memcpy(dirname+5,(char*)entry->d_name,len);
							dirname[len+5]=0;
							if(entry->d_type==DT_DIR)
								{
								if( ( dirtest = opendir( dirname ) ) != NULL ) {
									while( ( entrytest = readdir( dirtest ) ) != NULL ) {
									if( strcmp( (char*)entrytest->d_name, "." ) == 0 )continue;
									if( strcmp( (char*)entrytest->d_name, ".." ) == 0 )continue;
										
									test=1;
									break;
									}
									closedir(dirtest);
								}
									if(test)
									{
									blocks_array->blocks[i]=(struct blocks *)malloc(sizeof(struct blocks));
									blocks_array->blocks[i]->location = string_alloc(dirname,len+5);
									}
								}
							else if(entry->d_type==DT_LNK)
							{
								struct stat info;
								char *target = dirname;

								do
								{
									linklen = readlink(target, linkname, 4090);
									if (linklen < 0) continue;
									linkname[linklen] = 0;
									target = linkname;
									if (lstat(target, &info) < 0) continue;
								} while (S_ISLNK(info.st_mode));

								if(linkname[0]=='/')
								{
									if( ( dirtest = opendir( linkname ) ) != NULL ) {
										while( ( entrytest = readdir( dirtest ) ) != NULL ) {
											if( strcmp( (char*)entrytest->d_name, "." ) == 0 )continue;
											if( strcmp( (char*)entrytest->d_name, ".." ) == 0 )continue;
												
											test=1;
											break;
											}
										closedir(dirtest);
									}
									
									if(test)
									{
									blocks_array->blocks[i]=(struct blocks *)malloc(sizeof(struct blocks));
									blocks_array->blocks[i]->location = string_alloc(linkname,linklen);
									}
								}
								else
								{
									memcpy(dirname+5,linkname,linklen);//TODO check do I have to 0
									dirname[len+linklen+5]=0;
									
									if( ( dirtest = opendir( dirname ) ) != NULL ) {
										while( ( entrytest = readdir( dirtest ) ) != NULL ) {
											if( strcmp( (char*)entrytest->d_name, "." ) == 0 )continue;
											if( strcmp( (char*)entrytest->d_name, ".." ) == 0 )continue;
												
											test=1;
											break;
											}
										closedir(dirtest);
									}
									if(test)
									{
									blocks_array->blocks[i]=(struct blocks *)malloc(sizeof(struct blocks));
									blocks_array->blocks[i]->location = string_alloc(dirname,linklen+5);
									}
								}
							}
							if(test)
							{
							blocks_array->blocks[i]->block_id = i+1;
							blocks_array->blocks[i]->location_id = i+1;
							blocks_array->blocks[i]->user_id = 0;
							blocks_array->blocks[i]->size = 0;
							blocks_array->blocks_size++;
							i++;
							}
							test=0;
						}
					else continue;		
				
			}
			closedir(dir);
		}
 
	return blocks_array;

#else

	blocks_array = malloc(sizeof(struct blocks_array) + sizeof(struct blocks *));
	if (!blocks_array) return 0;
	blocks_array->blocks[0] = malloc(sizeof(struct blocks));
	if (!blocks_array->blocks[0])
	{
		free(blocks_array);
		return 0;
	}

	blocks_array->blocks_size = 1;
	blocks_array->blocks[0]->block_id = 1;
	blocks_array->blocks[0]->location_id = 1;
	blocks_array->blocks[0]->user_id = 0;
	blocks_array->blocks[0]->size = 0;

	blocks_array->blocks[0]->name = string_alloc("", 0);
	if (!blocks_array->blocks[0]->name)
	{
		free(blocks_array->blocks[0]);
		free(blocks_array);
		return 0;
	}

	blocks_array->blocks[0]->location = string_alloc("/", 1);
	if (!blocks_array->blocks[0]->location)
	{
		free(blocks_array->blocks[0]->name);
		free(blocks_array->blocks[0]);
		free(blocks_array);
		return 0;
	}

	return blocks_array;

#endif
}

struct blocks *access_storage_get_blocks(struct resources *restrict resources, int block_id)//TODO users
{
int i=0;

if(resources->auth && resources->auth->blocks_array && resources->auth->blocks_array->blocks_size){
	for(i=0;i<resources->auth->blocks_array->blocks_size;i++)
		{
			if(resources->auth->blocks_array->blocks[i] && (resources->auth->blocks_array->blocks[i]->block_id == block_id))
			return resources->auth->blocks_array->blocks[i];
		}
}

return 0;
}

#if defined(DEVICE) || defined(PUBCLOUD) || defined(FTP)
struct blocks *access_get_blocks(struct resources *restrict resources, int block_id)//TODO users
{
//TODO memory leak during free, must invoke free function for *blocks because of location

	int i=0;
	struct blocks *block=0;
	union json *item=0;
	struct string *path=0;
	struct string key;
	long user_id=0;

	if(resources->auth && resources->auth->blocks_array && resources->auth->blocks_array->blocks_size){
		for(i=0;i<resources->auth->blocks_array->blocks_size;i++)
		{
			if(resources->auth->blocks_array->blocks[i] && (resources->auth->blocks_array->blocks[i]->location_id == block_id))
			{
# ifdef FTP
			return resources->auth->blocks_array->blocks[i];
# else
			block=storage_blocks_get_by_block_id(resources->storage,resources->auth->blocks_array->blocks[i]->block_id,resources->auth->blocks_array->blocks[i]->user_id);
			if(!block)return 0;
			
			path=access_fs_concat_path(block->location,resources->auth->blocks_array->blocks[i]->location,0);
			free(block->location);
			block->location=path;
			return block;
# endif
			}
		}
	}
# ifdef FTP
	else
	{
		block=(struct blocks *)malloc(sizeof(struct blocks));
		if(!block)return 0;
		block->block_id = 1;
		block->user_id = 0;
		block->location = string_alloc("/",1);
		block->size = 0;
		block->name = string_alloc("",0);
		return block;
	}
# else
else if(resources->session_access && json_type(resources->session.ro) == OBJECT)
{
	key = string("user_id");
	item=dict_get(resources->session.ro->object, &key);
	if(!item)return 0;
	if(json_type(item)!=INTEGER)return 0;

	block = storage_blocks_get_by_block_id(resources->storage, block_id, item->integer);
#  if defined(PUBCLOUD)
	if (block)
	{
		setfsuid(block->uid);
		setfsgid(block->gid);
	}
#  endif
	return block;
}
# endif

return 0;
}
#endif

bool access_auth_check_location(struct resources *restrict resources,struct string *request_location,int block_id)
//compare whether the request path contains the auth location
{
//currently there is no point of this func
return true;
if(!resources->auth)return true;

int i=0;
if( resources->auth->blocks_array && resources->auth->blocks_array->blocks_size){
	for(i=0;i<resources->auth->blocks_array->blocks_size;i++)
		{
			if(resources->auth->blocks_array->blocks[i] && (resources->auth->blocks_array->blocks[i]->block_id == block_id) && resources->auth->blocks_array->blocks[i]->location)
			{
				if( resources->auth->blocks_array->blocks[i]->location->length < request_location->length && !memcmp(resources->auth->blocks_array->blocks[i]->location->data, request_location->data, resources->auth->blocks_array->blocks[i]->location->length))return true;
			}
		}
}

return false;
}

bool access_check_write_access(struct resources *restrict resources)
{
if(resources->auth && !resources->auth->rw){return false;}
return true;
}
