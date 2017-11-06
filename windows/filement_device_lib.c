#include <windows.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <wchar.h>
#include <Shlobj.h>
#include <Shlwapi.h>
#include <pthread.h>
#define SIGKILL 2
#include "../include/types.h"
#include "../src/lib/log.h"
#include "../include/filement.h"



#ifdef OS_WINDOWS

int do_upgrade=0;
//.NET2 - 11
//.NET4 - 12
int PLATFORM_ID=0;
struct string app_revision = {.data = "0", .length = 1};

__declspec(dllexport) void __cdecl filement_windows_stop_thread();

__declspec(dllexport) void __cdecl filement_windows_revision(wchar_t *_Wrevision);

__declspec(dllexport) int __cdecl filement_export_check_device(wchar_t *_Wloc);

__declspec(dllexport) int __cdecl filement_export_register_device(wchar_t *_Wloc,wchar_t *_Wemail,wchar_t *_Wpin,wchar_t *_Wpassword,wchar_t *_Wdev_name, int plid);

__declspec(dllexport) int __cdecl filement_export_start_server(wchar_t *_Wloc);

__declspec(dllexport) void filement_export_setup(wchar_t *_Wloc);

__declspec(dllexport) int __cdecl filement_export_upgrade(wchar_t *_Wloc);

__declspec(dllexport) void __cdecl filement_export_exit();

__declspec(dllexport) void __cdecl filement_export_exec_reinit(wchar_t *_Wloc);

__declspec(dllexport) wchar_t * __cdecl filement_export_get_version();

void open_log();
pthread_t *service_thread=0;
int initialized=0;
static int registered = 0;

BOOL DeleteDirectory(const char* pathname);

void open_log()
{
/*
 freopen("output.txt", "a", stdout);
 freopen("error.txt", "a", stderr);
 fprintf(stderr,"Device LIB\n");
 fprintf(stdout,"Device LIB\n");
 fflush(stdout);fflush(stderr);
*/
}

static void prepare_start(wchar_t *_Wloc)
{
char _loc[1024];

xwcstoutf(_loc,_Wloc, 1024);
struct string *loc=string_alloc(_loc,strlen(_loc));
filement_set_location(loc);
chdir(app_location.data);

open_log();

//debug("started 3\n");
char szPath[MAX_PATH];
memcpy(szPath,app_location.data,app_location.length);
szPath[app_location.length]='\0';
PathAppend(szPath, "\\backup");
DeleteDirectory(szPath);

memcpy(szPath,app_location.data,app_location.length);
szPath[app_location.length]='\0';
PathAppend(szPath, "\\temp");
DeleteDirectory(szPath);
}

__declspec(dllexport) void __cdecl filement_windows_stop_thread()
{
if(!service_thread)return ;

pthread_kill(*service_thread,SIGKILL);
pthread_cancel(*service_thread);
service_thread=0;

return ;
}

__declspec(dllexport) void __cdecl filement_windows_revision(wchar_t *_Wrevision)
{
char _revision[1024];


xwcstoutf(_revision,_Wrevision, 1024);
	app_revision = string(_revision,strlen(_revision));
}

void *filement_serve_thread(void *test)
{
filement_upgrade();
filement_serve();
}

__declspec(dllexport) int __cdecl filement_export_check_device(wchar_t *_Wloc)
{
//debug("started filement_export_check_device");
char _loc[1024];
xwcstoutf(_loc,_Wloc, 1024);

struct string *loc=string_alloc(_loc,strlen(_loc));
filement_set_location(loc);
chdir(app_location.data);

open_log();


if (!initialized)
{
	registered = filement_init();
	initialized = 1;
}



if(registered && !service_thread)
	{
	prepare_start(_Wloc);
	service_thread=malloc(sizeof(pthread_t));
	pthread_create(service_thread, 0, &filement_serve_thread, 0);
	}

return registered;
}

__declspec(dllexport) int __cdecl filement_export_register_device(wchar_t *_Wloc,wchar_t *_Wemail,wchar_t *_Wpin,wchar_t *_Wpassword,wchar_t *_Wdev_name, int plid)
{
char _loc[1024];
char _email[1024];
char _pin[1024];
char _dev_name[1024];
char _password[1024];

PLATFORM_ID=plid;

xwcstoutf(_loc,_Wloc, 1024);
struct string *loc=string_alloc(_loc,strlen(_loc));
filement_set_location(loc);
chdir(app_location.data);

open_log();
fprintf(stderr, "0\n"); fflush(stderr);

if(registered)return 0;
		 
		xwcstoutf(_email,_Wemail, 1024);
		struct string email = string(_email, strlen(_email));
		xwcstoutf(_dev_name,_Wdev_name, 1024);
		struct string dev_name = string(_dev_name, strlen(_dev_name));
		xwcstoutf(_password,_Wpassword, 1024);
		struct string password = string(_password, strlen(_password));
		xwcstoutf(_pin,_Wpin, 1024);

		fprintf(stderr, "1\n"); fflush(stderr);
		if (filement_register(&email, &dev_name, &password))
		{
			fprintf(stderr, "registered\n"); fflush(stderr);
			registered=1;
			
			if(registered && !service_thread)
			{
			prepare_start(_Wloc);
			service_thread=malloc(sizeof(pthread_t));
			pthread_create(service_thread, 0, &filement_serve_thread, 0);
			}
			
			return 1;
		}
		else
		{
			fprintf(stderr, "not registered\n"); fflush(stderr);
			registered=0;
			return 0;
		}

}

__declspec(dllexport) int __cdecl filement_export_start_server(wchar_t *_Wloc)
{
return 1;//compatibility with the interface

fprintf(stderr, "start_server()\n"); fflush(stderr);
prepare_start(_Wloc);
chdir(app_location.data);
int f = creat("start_server.txt",0666);
write(f,"1\n",2);
/*
wchar_t szPathRoot[MAX_PATH];
wchar_t szPath[MAX_PATH];

	if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, szPathRoot))) 
	{

	//TODO no point of copying memory, I may set just 0 at the end
		PathAppendW(szPathRoot, L"\\Filement");
		memcpy(szPath,szPathRoot,MAX_PATH);
		PathAppendW(szPath, L"\\backup");
		DeleteDirectory_rec(szPath);
		memcpy(szPath,szPathRoot,MAX_PATH);
		PathAppendW(szPath, L"\\temp");
		DeleteDirectory_rec(szPath);
	}
*/

if (!initialized)
{
	initialized = 1;
	if (!filement_init())return 0; //not registered
}
filement_upgrade();
filement_serve();

return 1;
}

__declspec(dllexport) int __cdecl filement_export_upgrade(wchar_t *_Wloc)
{
/*
char _loc[1024];
do_upgrade=1;

xwcstoutf(_loc,_Wloc, 1024);
struct string *loc=string_alloc(_loc,strlen(_loc));
//fprintf(stderr,"Loc1 %s\n",loc->data);
filement_set_location(loc);
//fprintf(stderr,"Loc2 %s\n",app_location.data);
//fflush(stderr);
chdir(app_location.data);

open_log();

if (!initialized)
{
if (!filement_init()){initialized = 1;return 0;} //not registered
}

if(filement_upgrade_windows(storage))
	{
	return 1;
	}
*/
return 0;
}

__declspec(dllexport) void __cdecl filement_export_setup(wchar_t *_Wloc)
{
char _loc[1024];

xwcstoutf(_loc,_Wloc, 1024);
struct string *loc=string_alloc(_loc,strlen(_loc));
filement_set_location(loc);
chdir(app_location.data);

open_log();

//filement_setup();
free(loc);
}

__declspec(dllexport) void __cdecl filement_export_exit()
{
if(service_thread)
{
pthread_kill(*service_thread,SIGKILL);
pthread_cancel(*service_thread);
service_thread=0;
}
exit(0);
}


__declspec(dllexport) void __cdecl filement_export_exec_reinit(wchar_t *_Wloc)
{
char _loc[1024];

xwcstoutf(_loc,_Wloc, 1024);

_execl(_loc, "Filement", "reinit", 0);
exit(0);
}

__declspec(dllexport) wchar_t * __cdecl filement_export_get_version()
{
struct string *ver=filement_get_version();
wchar_t *retW=malloc(1024);


xutftowcsn(retW,ver->data, 1024, -1);
return retW;
}



#endif
