#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <ctype.h>

#ifdef OS_BSD
#include <dirent.h>
#include <sys/socket.h>
#include <sys/wait.h> 
#include <signal.h>
#endif

#ifdef OS_WINDOWS
#include <windows.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include "mingw.h"
#endif

#include "types.h"
#include "format.h"

#ifdef OS_BSD
# include "actions.h"
# include "access.h"
# include "storage.h"
# include "io.h"
# include "magic.h"
# include "download.h"
# include "evfs.h"
# include "upload.h"
# include "status.h"
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
#endif

#include "media.h"
#include "session.h"

#define PROTOCOL_SCHEME "http://"
#define FFMPEG "ffmpeg"

/* TODO problematic movies:
	Good, Band And Ugly
	Being John Malkovich
	Intolerable Cruelty
*/

#ifdef OS_WINDOWS
WINBASEAPI BOOL WINAPI MoveFileWithProgressA(LPCSTR,LPCSTR,LPPROGRESS_ROUTINE,LPVOID,DWORD);
WINBASEAPI BOOL WINAPI MoveFileWithProgressW(LPCWSTR,LPCWSTR,LPPROGRESS_ROUTINE,LPVOID,DWORD);
#define MoveFileWithProgress MoveFileWithProgressA
#endif

#ifdef OS_WINDOWS
extern struct string app_location;
extern struct string app_location_name;
#endif

#if defined(FILEMENT_AV)
# include <libavformat/avformat.h>
# include <libavcodec/avcodec.h>
# include <libavutil/dict.h>
# include <libavutil/avassert.h>
# include <libavutil/avutil.h>
#endif

char *get_ffmpeg_exec_path(void)
{
#if defined(OS_WINDOWS)
//return strdup("C:\\ffmpeg_build\\bin\\ffmpeg.exe");

char *ffmpeg_dir=malloc(sizeof(char)*(app_location.length+sizeof("external\\ffmpeg.exe")));
sprintf(ffmpeg_dir,"%s%s",app_location.data,"external\\ffmpeg.exe");// assume that app_location ends with /
return ffmpeg_dir;
#elif defined(OS_MAC)
return strdup("/Applications/Filement.app/Contents/MacOS/ffmpeg"); // where to put the license
#elif defined(OS_LINUX) || defined(OS_ANDROID) || defined(OS_FREEBSD)

	char *filename = 0;
	char *var = getenv("PATH"), *start = var, *end = var;

	while (1)
	{
		size_t pathlen;
		struct stat info;

		end = strchrnul(start, ':');
		pathlen = end - start;

		// Allocate memory for the path.
		char *buffer = realloc(filename, sizeof(char) * (pathlen + 1 + sizeof(FFMPEG)));
		if (!buffer)
		{
			free(filename);
			return 0;
		}
		filename = buffer;

		format_bytes(filename, start, pathlen);
		filename[pathlen] = '/';
		format_bytes(filename + pathlen + 1, FFMPEG, sizeof(FFMPEG));

		// Check for existence, execution permissions and file type.
		if (!access(filename, F_OK | X_OK) && !stat(filename, &info) && S_ISREG(info.st_mode))
			return filename;

		if (!*end)
		{
			free(filename);
			return 0;
		}

		end = start = end + 1;
	}

#else
return 0;
#endif

}

static struct string *basedir(const struct string *dest)
{
	size_t last = dest->length - 1, index = last;
#ifdef OS_BSD
	while (index && (dest->data[index - 1] != '/'))
#else
	while (index && (dest->data[index - 1] != '\\'))
#endif
		index -= 1;
	return string_alloc(dest->data, index-1);
}

static struct string *basename_(const struct string *dest)
{
	size_t last = dest->length - 1, index = last;
#ifdef OS_BSD
	while (index && (dest->data[index - 1] != '/'))
#else
	while (index && (dest->data[index - 1] != '\\'))
#endif
		index -= 1;
	return string_alloc(dest->data + index, last - index + 1);
}

static char tohex(char code) {
  static char hex[] = "0123456789abcdef";
  return hex[code & 15];
}

static char *urlencode(char *str) {
  char *pstr = str, *buf = malloc(strlen(str) * 3 + 1), *pbuf = buf;
  while (*pstr) {
	if (isalpha(*pstr) || isdigit(*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~')
	  *pbuf++ = *pstr;
	/*else if (*pstr == ' ')
	  *pbuf++ = '+';*/
	else
	  *pbuf++ = '%', *pbuf++ = tohex(*pstr >> 4), *pbuf++ = tohex(*pstr & 15);
	pstr++;
  }
  *pbuf = '\0';
  return buf;
}

#ifdef OS_WINDOWS

static const char *quote_arg(const char *arg)
{
/* count chars to quote */
int len = 0, n = 0;
int force_quotes = 0;
char *q, *d;
const char *p = arg;
if (!*p) force_quotes = 1;
while (*p) {
if (isspace(*p) || *p == '*' || *p == '?' || *p == '{' || *p == '\'')
force_quotes = 1;
else if (*p == '"')
n++;
else if (*p == '\\') {
int count = 0;
while (*p == '\\') {
count++;
p++;
len++;
}
if (*p == '"')
n += count*2 + 1;
continue;
}
len++;
p++;
}
if (!force_quotes && n == 0)
return strdup(arg);

/* insert \ where necessary */
d = q = malloc(len+n+3);
*d++ = '"';
while (*arg) {
if (*arg == '"')
*d++ = '\\';
else if (*arg == '\\') {
int count = 0;
while (*arg == '\\') {
count++;
*d++ = *arg++;
}
if (*arg == '"') {
while (count-- > 0)
*d++ = '\\';
*d++ = '\\';
}
}
*d++ = *arg++;
}
*d++ = '"';
*d++ = 0;
return q;
}

#endif

static unsigned int get_movie_len(struct string *path)
{
AVFormatContext *fmt_ctx = NULL;
AVDictionaryEntry *tag = NULL;

if(!path)return 0;

		av_register_all();
		if (avformat_open_input(&fmt_ctx, path->data, NULL, NULL))return 0;
		if(avformat_find_stream_info(fmt_ctx, NULL) < 0)return 0;
if(fmt_ctx->duration<0)return 0;
unsigned int duration=(fmt_ctx->duration+999999)/1000000;
avformat_close_input(&fmt_ctx);
return duration;
}

int media_info(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
#if defined(FILEMENT_AV)
struct string *json_serialized=NULL,key;
union json *root=0,*item,*temp,*tmp_object;
unsigned long status_id=0;
int local_errno=0;
AVFormatContext *fmt_ctx = NULL;
AVDictionaryEntry *tag = NULL;
struct blocks *block=0;
struct string *path=0;
int ret;

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

		av_register_all();
		if ((ret = avformat_open_input(&fmt_ctx, path->data, NULL, NULL)))goto error;

		if((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0)goto error;
		
		root = json_object_old(false);
		
		temp=json_integer(fmt_ctx->bit_rate/1000);
		if(!string_init(&key, "bitrate", 7))goto error;
		if(json_object_insert_old(root, &key, temp))goto error;
		
		if(fmt_ctx->duration<0)
		{
		temp=json_integer(0);
		}
		else temp=json_integer((fmt_ctx->duration+999999)/1000000);
		if(!string_init(&key, "duration", 8))goto error;
		if(json_object_insert_old(root, &key, temp))goto error;
		
		tmp_object = json_object_old(false);
		while ((tag = av_dict_get(fmt_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
		{
			
			if(!string_init(&key, tag->key, strlen(tag->key)))goto error;
			item=json_string_old(string_alloc(tag->value,strlen(tag->value)));
			if(!item)goto error;
			if(json_object_insert_old(tmp_object, &key, item))goto error;
		}
		if(!string_init(&key, "metadata", 8))goto error;
		if(json_object_insert_old(root, &key, tmp_object))goto error;

json_serialized=json_serialize(root);
json_free(root); root = 0;

if (!json_serialized) goto error;

if(!remote_json_send(request, response, resources, json_serialized))
{
	//ERROR to return json
	free(json_serialized);json_serialized=0;
	avformat_close_input(&fmt_ctx);
	return -1;
}

	avformat_close_input(&fmt_ctx);
	free(json_serialized);json_serialized=0;
	
return 0;
error: //ERROR tuka trqbva da vrushta json za greshka
if(root)json_free(root);
   if(fmt_ctx)avformat_close_input(&fmt_ctx);
if(!local_errno)local_errno=errno;
return remote_json_error(request, response, resources, local_errno);
#else
	return NotFound;
#endif
}

char *restrict ffmpeg_subtitles(const struct string *option)
{

	struct string subtitles = string("subtitles=");

	size_t index, count = subtitles.length;
	for(index = 0; index < option->length; ++index)
		switch (option->data[index])
		{
		case '[':
		case ']':
		case ':':
		case ' ':
		case '\\':
		case ',':
		case '"':
		case '.':
			count += 1;
		default:
			count += 1;
		}
		
	char *filename = malloc(sizeof(char) * (count + 1));
	if (!filename) return 0;
	
	memcpy(filename, subtitles.data, subtitles.length);

	count = subtitles.length;
	for(index = 0; index < option->length; ++index)
		switch (option->data[index])
		{
		case '[':
		case ']':
		case ':':
		case ' ':
		case '\\':
		case ',':
		case '"':
		case '.':
			filename[count++] = '\\';
		default:
			filename[count++] = option->data[index];
		}

	filename[count] = 0;
	
	return filename;
}

static int media_extract_get_last_seconds(char *buffer,int length)
{
	int i=length-1;
	int u=0;
	char tmp_buf[16];
	for(;i>-1;i--)
	{
		if(buffer[i]=='t' || buffer[i]=='i' || buffer[i]=='m' || buffer[i]=='e' || buffer[i]=='=')//TODO currently it will work for something like thim= hitm=... not just for time=, but this way is faster
		{
		u++;	
		}
		else u=0;
		
		if(u==5)//hit
		{
			for(u=0;u<length && u<15;u++)
				{
					if(buffer[(i+5)+u]!=' ')tmp_buf[u]=buffer[(i+5)+u];
					else
					{
						if(u<8 && (tmp_buf[u+2]!=':' || tmp_buf[u+5]!=':' ) )break;//validation check
						
						return 3600 * strtol(tmp_buf,0,10) + 60 * strtol(tmp_buf+3,0,10) + strtol(tmp_buf+6,0,10);
					}
				}
		}
	}
return -1;
}

int media_convert(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
struct string *json_serialized=NULL;
	union json *item,*root;
	struct blocks *block=0;
	struct string *path=0,*dest_path=0,*status_key=0;
	struct string key;
	int local_errno=0;
	
	
	//firstly get the block_id from the request
	if(!session_is_logged_in(resources)){local_errno=1001;goto error;}
	//polzvam query za da vzema arguments
	if(json_type(query)!=OBJECT)goto error;

	key = string("block_id");
	item=dict_get(query->object, &key);
	if(!item)goto error;
	if(json_type(item)!=INTEGER)goto error;
	block=access_get_blocks(resources,item->integer);
	if(!block)goto error;

	key = string("path");
	item=dict_get(query->object, &key);
	if(!item)goto error;
	if(json_type(item)!=STRING)goto error;
	path=access_fs_concat_path(block->location,&item->string_node,1);
	if(block){if(block->location)free(block->location);free(block);block=0;}
	if(!path)goto error;
/*
	key = string("Connection");
	struct string value = string("close");
	response_header_add(response, &key, &value);

	root = json_object_old(true);
	if(!root)goto error;
	json_serialized=json_serialize(root);
	if(!json_serialized)goto error;

	status_set(status_key, 0, STATE_PENDING);

	if(!remote_json_send(request,response, resources, json_serialized))
	{
	//ERROR to return json
	free(json_serialized);json_serialized=0;
	goto error;
	}

	stream_term(&resources->stream);
*/

	key=string(".filement.mp4");
	dest_path=string_concat(path,&key);
	
	//Convertion code

	
	char *commands[100];
	char szCmdline[1024]; //TODO to check whether is it enought.
int	i=0;
	commands[i++]="filement-transcoding";
		commands[i++]=strdup("-i");
		commands[i++]=strdup(path->data);
		commands[i++]=strdup("-y");
		commands[i++]=strdup("-c:a"); 
		commands[i++]=strdup("aac");
		commands[i++]=strdup("-ab");
		commands[i++]=strdup("96k");
		commands[i++]=strdup("-strict"); 
		commands[i++]=strdup("-2");
		commands[i++]=strdup("-c:v");
		commands[i++]=strdup("libx264");
		commands[i++]=strdup("-profile:v");
		commands[i++]=strdup("main");
		commands[i++]=strdup("-preset");
		commands[i++]=strdup("medium");
		commands[i++]=strdup("-crf");
		commands[i++]=strdup("21");
		commands[i++]=strdup("-f");
		commands[i++]=strdup("mp4");
		commands[i++]=strdup("-movflags");
		commands[i++]=strdup("frag_keyframe+faststart");
		commands[i++]=strdup(dest_path->data);
		commands[i]=0;			

	//MAIN FFMPEG PART
	
	//END OF FFMPEG PART
	
	
#ifdef OS_MAC
// TODO: error can occur here
setenv("FONTCONFIG_FILE", "fonts.conf", 1);
setenv("FC_CONFIG_DIR", "/Applications/Filement.app/Contents/Resources/", 1);
setenv("FONTCONFIG_PATH", "/Applications/Filement.app/Contents/Resources/", 1);
#endif

#ifdef OS_WINDOWS
	
HANDLE g_hChildStd_OUT_Rd = NULL;
HANDLE g_hChildStd_OUT_Wr = NULL;
SECURITY_ATTRIBUTES saAttr; 
char *tmpstr=0;

//setting ENV path for windows
tmpstr=malloc(sizeof(char)*(app_location.length+sizeof("external")));
sprintf(tmpstr,"%s%s",app_location.data,"external"); // assume that app_location endup with /

SetEnvironmentVariable("FONTCONFIG_FILE","fonts.conf");
SetEnvironmentVariable("FC_CONFIG_DIR",tmpstr);
SetEnvironmentVariable("FONTCONFIG_PATH",tmpstr);
free(tmpstr);


   char *szexec=get_ffmpeg_exec_path();

   PROCESS_INFORMATION piProcInfo; 
   STARTUPINFOW siStartInfo;
   BOOL bSuccess = FALSE; 

	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
   saAttr.bInheritHandle = TRUE; 
   saAttr.lpSecurityDescriptor = NULL; 
   
   if ( ! CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 0) ) 
	 return -1;
	  
   if ( ! SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0) )
	  return -1;

	  ZeroMemory( &piProcInfo, sizeof(PROCESS_INFORMATION) );
	  
		 ZeroMemory( &siStartInfo, sizeof(STARTUPINFOW) );
   siStartInfo.cb = sizeof(STARTUPINFOW); 
   //siStartInfo.hStdOutput = g_hChildStd_OUT_Wr;
   siStartInfo.hStdError = g_hChildStd_OUT_Wr;
 //  siStartInfo.hStdOutput = g_hChildStd_OUT_Wr;
   //siStartInfo.dwFlags |= STARTF_USESTDHANDLES;
  
   siStartInfo.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	siStartInfo.wShowWindow=11;
	  
	  
	// Create the child process. 
	wchar_t wcmd[MAX_PATH], wdir[MAX_PATH], *wargs, *wenvblk = NULL; 
	char cmd[MAX_PATH];
	memset (cmd,'\0',MAX_PATH);
		if (xutftowcs_path(wcmd, szexec) < 0)
		return -1;

		
		int last_ptr=0;
		memcpy(cmd+last_ptr," ",1);
		last_ptr++;
		
		for (i=1; commands[i]; i++) {
		char *quoted = (char *)quote_arg(commands[i]);
		free(commands[i]);
		int len=strlen(quoted);
		memcpy(cmd+last_ptr,quoted,len);
		last_ptr+=len;
		memcpy(cmd+last_ptr," ",1);
		last_ptr++;
		free(quoted);
		}
		
		
		//fprintf(stderr,"%s\n",cmd);

		wargs = malloc((2 * last_ptr + 1) * sizeof(wchar_t));
		xutftowcs(wargs, cmd, 2 * last_ptr + 1);
			
   bSuccess = CreateProcessW(wcmd, 
	  wargs,	 // command line 
	  NULL,		  // process security attributes 
	  NULL,		  // primary thread security attributes 
	  TRUE,		  // handles are inherited 
	  0,			 // creation flags 
	  NULL,		  // use parent's environment 
	  NULL,		  // use parent's current directory 
	  &siStartInfo,  // STARTUPINFO pointer 
	  &piProcInfo);  // receives PROCESS_INFORMATION 
   
   free(wargs);
   // If an error occurs, exit the application. 
   if ( ! bSuccess ) 
	  return -1;
	CloseHandle(g_hChildStd_OUT_Wr);

	
	#define BUFSIZE 32768
	char buffer[BUFSIZE];
	int ret=0,progress=0;
	while (true)
	{
		ret=ReadFromPipe(g_hChildStd_OUT_Rd,buffer,BUFSIZE);
		if(ret<=0)break;
		//Handle errors
		progress=media_extract_get_last_seconds(buffer,ret);
		if(progress>0)status_set(status_key,progress, STATE_PENDING);
	}

   TerminateProcess(piProcInfo.hProcess,0);
   
   CloseHandle(piProcInfo.hProcess);
   CloseHandle(piProcInfo.hThread);

#else //OS_BSD

char *tmpstr=0;

/* //TODO to check do I need ENV variables for linux
tmpstr=malloc(sizeof(char)*(app_location.length+sizeof("ffmpeg")));
sprintf(tmpstr,"%s%s",app_location.data,"ffmpeg"); // assume that app_location endup with /

SetEnvironmentVariable("FONTCONFIG_FILE","fonts.conf");
SetEnvironmentVariable("FC_CONFIG_DIR",tmpstr);
SetEnvironmentVariable("FONTCONFIG_PATH",tmpstr);
free(tmpstr);
*/

	char *szexec=get_ffmpeg_exec_path();

	struct sigaction sa;
	void remove_zombies(int sig);
	int pid;
	int pipefd[2];
	fflush(stdout);

	pipe(pipefd);

	sigfillset(&sa.sa_mask);
	sa.sa_handler = remove_zombies;
	sa.sa_flags = 0;
	sigaction(SIGCHLD, &sa, NULL);

	pid = fork();
	if(pid==-1)return -1;
	else if(!pid)//child
	{
		close(pipefd[0]); 
		//close(0);
		//close(1);
		//close(2);
		dup2(pipefd[1], 2); 
		

		//close(pipefd[1]);  
		/* exec ls */
	/*	
		int i=0;
		for (; commands[i]; i++);
		char *pipestr=malloc(sizeof(char)*13);
		sprintf(pipestr,"pipe:%d",pipefd[1]);
		commands[i-1]=pipestr;
	*/	
		execv(szexec, commands);  
		close(pipefd[1]);
		exit(1);
	}
	
	for (; commands[i]; i++) {
		free(commands[i]);
	}
	close(pipefd[1]); 
	char buffer[PIPE_BUF];
	int ret=0;
	int progress=0;

	while(1)
	{
		ret=read(pipefd[0], buffer, PIPE_BUF-1);
		if(ret<=0)break;
		
		progress=media_extract_get_last_seconds(buffer,ret);
		if(progress>0)status_set(status_key,progress, STATE_PENDING);
	}
	close(pipefd[0]);

	free(path);

#endif 

	free(szexec);
	free(path);path=0;
	free(dest_path);dest_path=0;
return -1;
error: //ERROR tuka trqbva da vrushta json za greshka
free(szexec);
free(path);
free(dest_path);

if(!local_errno)local_errno=errno;
return remote_json_error(request, response, resources, local_errno);		
}

int media_stream(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query)
{
/*
Request : {"block_id" = int , "path" = string, "output_format" = "mp3" | "ogg" | "webm" | "mp4" | "hls" | "ts" | "mkv", "bitrate"=64, "subtitles"="filename","seek"= "00:30:00"|"1800"  }
block_id -> block_id from fs.get_block
path -> path of the stat
*/
union json *item;
union json *json = 0, *json_item = 0;
struct blocks *block=0;
struct string *path=0,*rel_path=0,*base_path=0,*base_name=0,key,key_value;
struct string *option_output_format=0;
int option_bitrate=0;
struct string *option_subtitles=0;
int block_id=0;
int len=0;
int is_video=0;
int is_chunked=1;
int status=0;

//firstly get the block_id from the request

//you can't stream with auth_id for now !
if(!session_is_logged_in(resources)) return NotFound;

//polzvam query za da vzema arguments
if(json_type(query)!=OBJECT) return BadRequest;

key = string("block_id");
item=dict_get(query->object, &key);
if (!item || (json_type(item) != INTEGER)) return BadRequest;
block=access_get_blocks(resources,item->integer);
if(!block) return NotFound;
block_id=item->integer;

key = string("path");
item=dict_get(query->object, &key);
if(!item || (json_type(item) != STRING)) return BadRequest;

rel_path=&item->string_node;
//if(!access_auth_check_location(resources,&item->string_node,block->block_id))return 0;// Auth check
path=access_fs_concat_path(block->location,&item->string_node,1);
if(!path) return NotFound;

base_path=basedir(path);
if(!base_path) goto error;

base_name=basename_(path);
if(!base_name) goto error;

#ifdef OS_WINDOWS
struct _stati64 attribute;
#else
struct stat attribute;
#endif
if( stat( base_path->data, &attribute ) == -1 ) goto error;
if ((attribute.st_mode & S_IFMT) != S_IFDIR) goto error;

if( stat( path->data, &attribute ) == -1 ) goto error;
if ((attribute.st_mode & S_IFMT) != S_IFREG) goto error;


if(path->length-base_path->length>256)goto error;

//end of the regular checking, now is time to check the specific things
//free the options will be free on json_free
key = string("output_format");
item=dict_get(query->object, &key);
if (!item || (json_type(item) != STRING)) return BadRequest;
if(key = string("mp3"), string_equal(&item->string_node,&key))option_output_format=&item->string_node;
else if(key = string("ogg"), string_equal(&item->string_node,&key))option_output_format=&item->string_node;
else if(key = string("webm"), string_equal(&item->string_node,&key)){is_video=1,option_output_format=&item->string_node;}
else if(key = string("hls"), string_equal(&item->string_node,&key)){is_video=1,option_output_format=&item->string_node;}
else if(key = string("ts"), string_equal(&item->string_node,&key)){is_video=1,option_output_format=&item->string_node;}
else if(key = string("mp4"), string_equal(&item->string_node,&key)){is_video=1,option_output_format=&item->string_node;}
else if(key = string("mkv"), string_equal(&item->string_node,&key)){is_video=1,option_output_format=&item->string_node;}

if(!option_output_format)goto error;

key = string("bitrate");
item=dict_get(query->object, &key);
if(item)
{
	if(json_type(item)!=INTEGER)
	{
	goto error;
	}
	if(item->integer>20000)goto error; //TODO this must be changed depending on the account type
	
	option_bitrate=item->integer;
}

key = string("subtitles");
item=dict_get(query->object, &key);
if(item)
{
	if(json_type(item)!=STRING)
	{
	goto error;
	}
	#ifdef OS_WINDOWS
	if(strstr(item->string_node.data, "\\"))goto error;
	#else
	if(strstr(item->string_node.data, "/"))goto error;
	#endif
	option_subtitles=&item->string_node; //TODO to check may I put -vf subtitles= the subtitles in "
}

//end of checking

char *commands[100];
char szCmdline[1024]; //TODO to check whether is it enought.

commands[0]=strdup("filement-transcoding");

if(!is_video)//is audio
{//TODO to check sprintf for errors
	
	
	if(key = string("mp3"), string_equal(option_output_format,&key))
	{
		if(option_bitrate)
		{
		commands[1]=strdup("-i");
		commands[2]=malloc(sizeof(char)*base_name->length+1);
		sprintf(commands[2],"%s",base_name->data);
		commands[3]=strdup("-strict"); 
		commands[4]=strdup("-2");
		commands[5]=strdup("-b:a");
		commands[6]=malloc(sizeof(char)*5+2);
		sprintf(commands[6],"%dk",option_bitrate);
		commands[7]=strdup("-f");
		commands[8]=malloc(sizeof(char)*option_output_format->length+1);
		sprintf(commands[8],"%s",option_output_format->data);
		commands[9]=strdup("-");
		commands[10]=0;
		}
		else
		{
		commands[1]=strdup("-i");
		commands[2]=malloc(sizeof(char)*base_name->length+1);
		sprintf(commands[2],"%s",base_name->data);
		commands[3]=strdup("-strict"); 
		commands[4]=strdup("-2");
		commands[5]=strdup("-f");
		commands[6]=malloc(sizeof(char)*option_output_format->length+1);
		sprintf(commands[6],"%s",option_output_format->data);
		commands[7]=strdup("-");
		commands[8]=0;
		}
		
		key = string("Content-Type");
		key_value = string("audio/mpeg");
		if (!response_header_add(response, &key, &key_value)) goto error; // TODO: memory error
	}
	else if(key = string("ogg"), string_equal(option_output_format,&key))
	{	
		if(option_bitrate)
		{
		commands[1]=strdup("-i");
		commands[2]=malloc(sizeof(char)*base_name->length+1);
		sprintf(commands[2],"%s",base_name->data);
		commands[3]=strdup("-c:a"); 
		commands[4]=strdup("libvorbis");
		commands[5]=strdup("-strict"); 
		commands[6]=strdup("-2");
		commands[7]=strdup("-b:a");
		commands[8]=malloc(sizeof(char)*5+2);
		sprintf(commands[8],"%dk",option_bitrate);
		commands[9]=strdup("-f");
		commands[10]=malloc(sizeof(char)*option_output_format->length+1);
		sprintf(commands[10],"%s",option_output_format->data);
		commands[11]=strdup("-");
		commands[12]=0;
		}
		else
		{
		commands[1]=strdup("-i");
		commands[2]=malloc(sizeof(char)*base_name->length+1);
		sprintf(commands[2],"%s",base_name->data);
		commands[3]=strdup("-c:a"); 
		commands[4]=strdup("libvorbis");
		commands[5]=strdup("-strict"); 
		commands[6]=strdup("-2");
		commands[7]=strdup("-f");
		commands[8]=malloc(sizeof(char)*option_output_format->length+1);
		sprintf(commands[8],"%s",option_output_format->data);
		commands[9]=strdup("-");
		commands[10]=0;
		}
		
		key = string("Content-Type");
		key_value = string("audio/ogg");
		if (!response_header_add(response, &key, &key_value)) goto error; // TODO: memory error
	}
	else goto error;
	
	
}
else
{
	if(key = string("hls"), string_equal(option_output_format,&key))
	{
	//TODO to make it dynamic every 10secs to generate for the current ts
		struct string *buffer;
		char urlbuf[1800];

		#ifdef OS_BSD
			extern struct string UUID;
		#else
			extern struct string UUID_WINDOWS;
		#endif

		// {"session_id": "...", "actions": {"media.stream": {"block_id": block_id, "path": "...","output_format": "ts", "subtitles": "...","bitrate": option_bitrate}}}

		struct string key, json_value;
		union json *container;

		if (!(json = json_object_old(false))) goto error;

		key = string("session_id");
		json_value = string(resources->session_id, CACHE_KEY_SIZE);
		if (json_object_insert_old(json, &key, json_string_old(&json_value))) goto error;

		if (!(json_item = json_object_old(false))) goto error;
		key = string("actions");
		if (json_object_insert_old(json, &key, json_item)) goto error;

		container = json_item;
		if (!(json_item = json_object_old(false))) goto error;
		key = string("media.stream");
		if (json_object_insert_old(container, &key, json_item)) goto error;
		container = json_item;

		if (!(json_item = json_integer(block_id))) goto error;
		key = string("block_id");
		if (json_object_insert_old(container, &key, json_item)) goto error;

		if (!(json_item = json_string_old(rel_path))) goto error;
		key = string("path");
		if (json_object_insert_old(container, &key, json_item)) goto error;

		json_value = string("ts");
		if (!(json_item = json_string_old(&json_value))) goto error;
		key = string("output_format");
		if (json_object_insert_old(container, &key, json_item)) goto error;

		if (option_bitrate)
		{
			if (!(json_item = json_integer(option_bitrate))) goto error;
			key = string("bitrate");
			if (json_object_insert_old(container, &key, json_item)) goto error;
		}

		if (option_subtitles)
		{
			if (!(json_item = json_string_old(option_subtitles))) goto error;
			key = string("subtitles");
			if (json_object_insert_old(container, &key, json_item)) goto error;
		}

		buffer = json_serialize(json);
		if (!buffer) goto error; // memory error
		json_free(json);
		json = 0;
		json_item = 0;

		char *encoded_url=urlencode(buffer->data);
		free(buffer);

		key = string("host");
		struct string *value = dict_get(&request->headers, &key);
		if(!value) goto error;
		#ifdef OS_WINDOWS
		len=snprintf(urlbuf, 1800, PROTOCOL_SCHEME "%s/%s/?%s", value->data,UUID_WINDOWS.data,encoded_url);
		#else
		len=snprintf(urlbuf, 1800, PROTOCOL_SCHEME "%s/%s/?%s", value->data,UUID.data,encoded_url);
		#endif

		if(!len || (len==1800 && urlbuf[len-1]!='\0'))goto error;

		//TODO duration checking
		char m3u8_buffer[2048]; // TODO: is this enough
		size_t body_string_length = snprintf(m3u8_buffer, 2048, "#EXTM3U\n#EXT-X-TARGETDURATION:%d\n#EXT-X-MEDIA-SEQUENCE:0\n#EXTINF:%d, no desc\n%s\n#EXT-X-ENDLIST",5,5, urlbuf);
		if(!body_string_length || (body_string_length==2048 && m3u8_buffer[body_string_length-1]!='\0')) goto error;

		free(path);path=0;
		free(base_path);base_path=0;
		free(base_name);base_name=0;
	
		key = string("Content-Type");
		key_value = string("audio/x-mpegurl");
		if (!response_header_add(response, &key, &key_value)) goto error; // TODO: memory error
		
		response->code = OK;

		if (!response_headers_send(&resources->stream, request, response, body_string_length)) goto error;
		if (!response_content_send(&resources->stream, response, m3u8_buffer, body_string_length)) goto error;

		return 0;
	}
	else if(key = string("ts"), string_equal(option_output_format,&key))
	{
		//TODO better comandline handling and options
		if(option_subtitles)
		{
		commands[1]=strdup("-i");
		commands[2]=malloc(sizeof(char)*base_name->length+1);
		sprintf(commands[2],"%s",base_name->data);
		commands[3]=strdup("-vf"); 

		commands[4] = ffmpeg_subtitles(option_subtitles);
		if (!commands[4]) goto error;

		commands[5]=strdup("-c:a"); 
		commands[6]=strdup("libmp3lame");
		commands[7]=strdup("-b:a"); 
		commands[8]=strdup("64k");
		commands[9]=strdup("-strict"); 
		commands[10]=strdup("-2");
		commands[11]=strdup("-c:v");
		commands[12]=strdup("libx264");
		commands[13]=strdup("-profile:v");
		commands[14]=strdup("main");
		commands[15]=strdup("-preset");
		commands[16]=strdup("ultrafast");
		commands[17]=strdup("-tune");
		commands[18]=strdup("zerolatency");
		commands[19]=strdup("-bsf:v");
		commands[20]=strdup("h264_mp4toannexb");
		commands[21]=strdup("-bufsize");
		commands[22]=strdup("3000k");
		commands[23]=strdup("-f");
		commands[24]=strdup("mpegts");
		commands[25]=strdup("-");
		commands[26]=0;
		}
		else
		{
		commands[1]=strdup("-i");
		commands[2]=malloc(sizeof(char)*base_name->length+1);
		sprintf(commands[2],"%s",base_name->data);
		commands[3]=strdup("-c:a"); 
		commands[4]=strdup("libmp3lame");
		commands[5]=strdup("-b:a"); 
		commands[6]=strdup("64k");
		commands[7]=strdup("-strict"); 
		commands[8]=strdup("-2");
		commands[9]=strdup("-c:v");
		commands[10]=strdup("libx264");
		commands[11]=strdup("-profile:v");
		commands[12]=strdup("main");
		commands[13]=strdup("-preset");
		commands[14]=strdup("ultrafast");
		commands[15]=strdup("-tune");
		commands[16]=strdup("zerolatency");
		commands[17]=strdup("-bsf:v");
		commands[18]=strdup("h264_mp4toannexb");
		commands[19]=strdup("-bufsize");
		commands[20]=strdup("3000k");
		commands[21]=strdup("-f");
		commands[22]=strdup("mpegts");
		commands[23]=strdup("-");
		commands[24]=0;		
		}
		key = string("Content-Type");
		key_value = string("video/MP2T");
		if (!response_header_add(response, &key, &key_value)) goto error; // TODO: memory error
		
			key = string("Accept-Ranges");
		key_value = string("bytes");
		if (!response_header_add(response, &key, &key_value))goto error;
		
		is_chunked=0;
	
	}
	else if(key = string("webm"), string_equal(option_output_format,&key))
	{
		//TODO better comandline handling and options
		if(option_subtitles)
		{
		commands[1]=strdup("-i");
		commands[2]=malloc(sizeof(char)*base_name->length+1);
		sprintf(commands[2],"%s",base_name->data);
		commands[3]=strdup("-vf"); 
		commands[4] = ffmpeg_subtitles(option_subtitles);
		if (!commands[4]) goto error;
		commands[5]=strdup("-c:a"); 
		commands[6]=strdup("libvorbis");
		commands[7]=strdup("-strict"); 
		commands[8]=strdup("-2");
		commands[9]=strdup("-c:v");
		commands[10]=strdup("libvpx");
		commands[11]=strdup("-b:v");
		commands[12]=strdup("1M");
		commands[13]=strdup("-f");
		commands[14]=strdup("webm");
		commands[15]=strdup("-");
		commands[16]=0;
		}
		else
		{
		commands[1]=strdup("-i");
		commands[2]=malloc(sizeof(char)*base_name->length+1);
		sprintf(commands[2],"%s",base_name->data);
		commands[3]=strdup("-c:a"); 
		commands[4]=strdup("libvorbis");
		commands[5]=strdup("-strict"); 
		commands[6]=strdup("-2");
		commands[7]=strdup("-c:v");
		commands[8]=strdup("libvpx");
		commands[9]=strdup("-b:v");
		commands[10]=strdup("1M");
		commands[11]=strdup("-f");
		commands[12]=strdup("webm");
		commands[13]=strdup("-");
		commands[14]=0;
		}
		
		key = string("Content-Type");
		key_value = string("video/webm");
		if (!response_header_add(response, &key, &key_value)) goto error;
	}
	else if(key = string("mp4"), string_equal(option_output_format,&key))
	{
		if(option_subtitles)
		{
		commands[1]=strdup("-i");
		commands[2]=malloc(sizeof(char)*base_name->length+1);
		sprintf(commands[2],"%s",base_name->data);
		commands[3]=strdup("-vf"); 
		commands[4] = ffmpeg_subtitles(option_subtitles);
		if (!commands[4]) goto error;
		commands[5]=strdup("-c:a"); 
		commands[6]=strdup("aac");
		commands[7]=strdup("-strict"); 
		commands[8]=strdup("-2");
		commands[9]=strdup("-c:v");
		commands[10]=strdup("libx264");
		commands[11]=strdup("-profile:v");
		commands[12]=strdup("main");
		commands[13]=strdup("-preset");
		commands[14]=strdup("ultrafast");
		commands[15]=strdup("-tune");
		commands[16]=strdup("zerolatency");
		commands[17]=strdup("-bsf:v");
		commands[18]=strdup("h264_mp4toannexb");
		commands[19]=strdup("-bufsize");
		commands[20]=strdup("3000k");
		commands[21]=strdup("-f");
		commands[22]=strdup("mp4");
		commands[23]=strdup("-movflags");
		commands[24]=strdup("frag_keyframe+faststart");
		commands[25]=strdup("-");
		commands[26]=0;
		}
		else
		{
		
		commands[1]=strdup("-i");
		commands[2]=malloc(sizeof(char)*base_name->length+1);
		sprintf(commands[2],"%s",base_name->data);
		commands[3]=strdup("-c:a"); 
		commands[4]=strdup("aac");
		commands[5]=strdup("-strict"); 
		commands[6]=strdup("-2");
		commands[7]=strdup("-c:v");
		commands[8]=strdup("libx264");
		commands[9]=strdup("-profile:v");
		commands[10]=strdup("main");
		commands[11]=strdup("-preset");
		commands[12]=strdup("ultrafast");
		commands[13]=strdup("-tune");
		commands[14]=strdup("zerolatency");
		commands[15]=strdup("-bsf:v");
		commands[16]=strdup("h264_mp4toannexb");
		commands[17]=strdup("-f");
		commands[18]=strdup("mp4");
		commands[19]=strdup("-movflags");
		commands[20]=strdup("frag_keyframe+faststart");
		commands[21]=strdup("-");
		commands[22]=0;		
		
		}
		//TODO better comandline handling and options

		key = string("Content-Type");
		key_value = string("video/mp4");
		if (!response_header_add(response, &key, &key_value)) goto error;
	}
	else if(key = string("mkv"), string_equal(option_output_format,&key))
	{
		if(option_subtitles)
		{
		commands[1]=strdup("-i");
		commands[2]=malloc(sizeof(char)*base_name->length+1);
		sprintf(commands[2],"%s",base_name->data);
		commands[3]=strdup("-vf"); 
		commands[4] = ffmpeg_subtitles(option_subtitles);
		if (!commands[4]) goto error;
		commands[5]=strdup("-c:a"); 
		commands[6]=strdup("aac");
		commands[7]=strdup("-strict"); 
		commands[8]=strdup("-2");
		commands[9]=strdup("-c:v");
		commands[10]=strdup("libx264");
		commands[11]=strdup("-profile:v");
		commands[12]=strdup("main");
		commands[13]=strdup("-preset");
		commands[14]=strdup("ultrafast");
		commands[15]=strdup("-tune");
		commands[16]=strdup("zerolatency");
		commands[17]=strdup("-bsf:v");
		commands[18]=strdup("h264_mp4toannexb");
		commands[19]=strdup("-bufsize");
		commands[20]=strdup("3000k");
		commands[21]=strdup("-f");
		commands[22]=strdup("matroska");
		commands[23]=strdup("-");
		commands[24]=0;
		}
		else
		{
		commands[1]=strdup("-i");
		commands[2]=malloc(sizeof(char)*base_name->length+1);
		sprintf(commands[2],"%s",base_name->data);
		commands[3]=strdup("-c:a"); 
		commands[4]=strdup("aac");
		commands[5]=strdup("-strict"); 
		commands[6]=strdup("-2");
		commands[7]=strdup("-c:v");
		commands[8]=strdup("libx264");
		commands[9]=strdup("-profile:v");
		commands[10]=strdup("main");
		commands[11]=strdup("-preset");
		commands[12]=strdup("ultrafast");
		commands[13]=strdup("-tune");
		commands[14]=strdup("zerolatency");
		commands[15]=strdup("-bsf:v");
		commands[16]=strdup("h264_mp4toannexb");
		commands[17]=strdup("-f");
		commands[18]=strdup("matroska");
		commands[19]=strdup("-");
		commands[20]=0;		
		}
		//TODO better comandline handling and options
		key = string("Content-Type");
		key_value = string("video/mp4");
		if (!response_header_add(response, &key, &key_value)) goto error;
	}
	else goto error;
	

} //else of if(!is_video)

#ifdef OS_MAC
// TODO: error can occur here
setenv("FONTCONFIG_FILE", "fonts.conf", 1);
setenv("FC_CONFIG_DIR", "/Applications/Filement.app/Contents/Resources/", 1);
setenv("FONTCONFIG_PATH", "/Applications/Filement.app/Contents/Resources/", 1);
#endif

#ifdef OS_WINDOWS
	
HANDLE g_hChildStd_OUT_Rd = NULL;
HANDLE g_hChildStd_OUT_Wr = NULL;
SECURITY_ATTRIBUTES saAttr; 
char *movie_dir=base_path->data; 
char *tmpstr=0;

//setting ENV path for windows
tmpstr=malloc(sizeof(char)*(app_location.length+sizeof("external")));
sprintf(tmpstr,"%s%s",app_location.data,"external"); // assume that app_location endup with /

SetEnvironmentVariable("FONTCONFIG_FILE","fonts.conf");
SetEnvironmentVariable("FC_CONFIG_DIR",tmpstr);
SetEnvironmentVariable("FONTCONFIG_PATH",tmpstr);
free(tmpstr);


   char *szexec=get_ffmpeg_exec_path();
//   C:\ffmpeg>ffmpeg -i f.avi -vf subtitles=f.srt -ss 00:30:00 -c:a vorbis -strict -2 -c:v libx264 -preset ultrafast fout.mp4
   //TCHAR szCmdline[]=TEXT("-i C:\\test\\ffmpeg\\bin\\test720p.avi -c:v libx264 -preset ultrafast -f matroska - ");
  // TCHAR szCmdline[]=TEXT(" -i f.avi -vf subtitles=f.srt -c:a mp3 -strict -2 -c:v libx264 -preset ultrafast -f matroska - ");
  // TCHAR szCmdline[]=TEXT(" -i f.avi -vf subtitles=f.srt -c:a libvorbis -strict -2 -c:v libvpx -preset ultrafast -f webm - ");
 // TCHAR szCmdline[]=TEXT(" -i f.avi -vf subtitles=f.srt -strict -2 -c:v libx264 -preset ultrafast -f m4v - ");
 // TCHAR szCmdline[]=TEXT(" -i f.avi -c:a aac -strict -2 -c:v libx264 -preset ultrafast -f mpegts - ");
	//TCHAR szCmdline[]=TEXT("  -i f.avi -an -vcodec libx264 -f mp4 -movflags empty_moov+separate_moof+isml+rtphint -frag_duration 3 - ");
	//TCHAR szCmdline[]=TEXT("  -i f.avi -an -vcodec libx264 -profile:v baseline -f mp4 -movflags empty_moov+separate_moof+isml+rtphint -frag_duration 3 - ");
	//TCHAR szCmdline[]=TEXT(" -i f.avi -an -vcodec libx264 -profile:v baseline -crf 22 -f mp4 -movflags faststart+empty_moov+separate_moof+isml+rtphint -frag_duration 3 - ");
	//TCHAR szCmdline[]=TEXT(" -i f.avi -acodec libvo_aacenc -ab 64k -vcodec libx264 -preset fast -profile:v main -f mp4 -movflags frag_keyframe+faststart - ");
	
   PROCESS_INFORMATION piProcInfo; 
   STARTUPINFOW siStartInfo;
   BOOL bSuccess = FALSE; 

	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
   saAttr.bInheritHandle = TRUE; 
   saAttr.lpSecurityDescriptor = NULL; 
   
   if ( ! CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 0) ) 
	 return -1;
	  
   if ( ! SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0) )
	  return -1;

	  ZeroMemory( &piProcInfo, sizeof(PROCESS_INFORMATION) );
	  
		 ZeroMemory( &siStartInfo, sizeof(STARTUPINFOW) );
   siStartInfo.cb = sizeof(STARTUPINFOW); 
   siStartInfo.hStdOutput = g_hChildStd_OUT_Wr;
   //siStartInfo.dwFlags |= STARTF_USESTDHANDLES;
  
   siStartInfo.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	siStartInfo.wShowWindow=11;
	  
	  
	// Create the child process. 
	wchar_t wcmd[MAX_PATH], wdir[MAX_PATH], *wargs, *wenvblk = NULL; 
	char cmd[MAX_PATH];
	memset (cmd,'\0',MAX_PATH);
		if (xutftowcs_path(wcmd, szexec) < 0)
		return -1;
		if (xutftowcs_path(wdir, movie_dir) < 0)
		return -1;
	
		


		

		int i=1;
		int last_ptr=0;
		memcpy(cmd+last_ptr," ",1);
		last_ptr++;
		
		for (; commands[i]; i++) {
		char *quoted = (char *)quote_arg(commands[i]);
		free(commands[i]);
		int len=strlen(quoted);
		memcpy(cmd+last_ptr,quoted,len);
		last_ptr+=len;
		memcpy(cmd+last_ptr," ",1);
		last_ptr++;
		free(quoted);
		}
		
		
		//fprintf(stderr,"%s\n",cmd);

		wargs = malloc((2 * last_ptr + 1) * sizeof(wchar_t));
		xutftowcs(wargs, cmd, 2 * last_ptr + 1);
			
   bSuccess = CreateProcessW(wcmd, 
	  wargs,	 // command line 
	  NULL,		  // process security attributes 
	  NULL,		  // primary thread security attributes 
	  TRUE,		  // handles are inherited 
	  0,			 // creation flags 
	  NULL,		  // use parent's environment 
	  wdir,		  // use parent's current directory 
	  &siStartInfo,  // STARTUPINFO pointer 
	  &piProcInfo);  // receives PROCESS_INFORMATION 
   
   free(wargs);
   // If an error occurs, exit the application. 
   if ( ! bSuccess ) 
	  return -1;
	CloseHandle(g_hChildStd_OUT_Wr);
	struct string value;

	response->code = OK;

	//TODO fix 2GB max ts stream
	if (!response_headers_send(&resources->stream, request, response, (is_chunked ? RESPONSE_CHUNKED : 2147483647))) return -1; // memory error

	#define BUFSIZE 32768
	struct string *chunk=0;
	char buffer[BUFSIZE];
	int ret=0;
	while (true)
	{
		ret=ReadFromPipe(g_hChildStd_OUT_Rd,buffer,BUFSIZE);
		if(ret<=0)break;
		if (!response_content_send(&resources->stream, response, buffer, ret)) break;
	}

   TerminateProcess(piProcInfo.hProcess,0);
   
   CloseHandle(piProcInfo.hProcess);
   CloseHandle(piProcInfo.hThread);

	free(path);

	if (is_chunked) status = response_chunk_last(&resources->stream, response);
#else //OS_BSD

char *movie_dir=base_path->data; 
char *tmpstr=0;

/* //TODO to check do I need ENV variables for linux
tmpstr=malloc(sizeof(char)*(app_location.length+sizeof("ffmpeg")));
sprintf(tmpstr,"%s%s",app_location.data,"ffmpeg"); // assume that app_location endup with /

SetEnvironmentVariable("FONTCONFIG_FILE","fonts.conf");
SetEnvironmentVariable("FC_CONFIG_DIR",tmpstr);
SetEnvironmentVariable("FONTCONFIG_PATH",tmpstr);
free(tmpstr);
*/

	char *szexec=get_ffmpeg_exec_path();

	struct sigaction sa;
	void remove_zombies(int sig);
	int pid;
	int pipefd[2];
	fflush(stdout);

	pipe(pipefd);

	sigfillset(&sa.sa_mask);
	sa.sa_handler = remove_zombies;
	sa.sa_flags = 0;
	sigaction(SIGCHLD, &sa, NULL);

	pid = fork();
	if(pid==-1)return -1;
	else if(!pid)//child
	{
		close(pipefd[0]); 
		//close(0);
		//close(1);
		//close(2);
		dup2(pipefd[1], 1); 
		
		chdir(movie_dir); //TODO to check can I make chroot, because is more secure

		//close(pipefd[1]);  
		/* exec ls */
	/*	
		int i=0;
		for (; commands[i]; i++);
		char *pipestr=malloc(sizeof(char)*13);
		sprintf(pipestr,"pipe:%d",pipefd[1]);
		commands[i-1]=pipestr;
	*/	
		execv(szexec, commands);  
		close(pipefd[1]);
		exit(1);
	}
	close(pipefd[1]);
	size_t i;
	for (i = 0; commands[i]; i++) {
		free(commands[i]);
	}
	char buffer[PIPE_BUF];
	int ret=0;
	struct string *chunk=0;
	struct string value;

	response->code = OK;

	//TODO fix 2GB max ts stream
	if (!response_headers_send(&resources->stream, request, response, (is_chunked ? RESPONSE_CHUNKED : 2147483647))) goto error; // memory error

	while(1)
	{
		ret=read(pipefd[0], buffer, PIPE_BUF-1);
		if(ret<=0)break;

		status = response_content_send(&resources->stream, response, buffer, ret);

		if (!status)
		{
			kill( pid, SIGKILL );
			break;
		}
	}
	close(pipefd[0]);

	free(path);

	if (is_chunked) status = response_chunk_last(&resources->stream, response);

#endif 
	stream_term(&resources->stream);

	free(szexec);
	return 0;
	
error:
	if (json) json_free(json);
	if (json_item) json_free(json_item);
	free(path);
	free(base_path);
	free(base_name);
	free(szexec);
	return NotFound;
}

#ifdef OS_WINDOWS
unsigned long ReadFromPipe(HANDLE pipe,char *buffer,unsigned long bufsize) // to make it bigger than int 
{ 
   DWORD dwRead;
   BOOL bSuccess = FALSE;
  
	  bSuccess = ReadFile( pipe, buffer, bufsize, &dwRead, NULL);
	  if( ! bSuccess || dwRead == 0 ) return 0;
   
   return (unsigned long)dwRead;
}
#else
void remove_zombies(int sig)
{
	int status;

   waitpid(-1, &status, WNOHANG);
   
} 
#endif
