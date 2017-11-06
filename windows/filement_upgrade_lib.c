#include <windows.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../include/types.h"
#include "../src/lib/log.h"
#include "../include/filement.h"
#include "../src/lib/stream.h"
#include "../src/storage.h"
#include "../src/device/distribute.h"
#include "../src/device/upgrade.h"


#ifdef OS_WINDOWS
int do_upgrade=0;
//struct string app_revision = {.data = "0", .length = 1};

__declspec(dllexport) int __cdecl filement_export_check_device(wchar_t *_Wloc);

__declspec(dllexport) int __cdecl filement_export_upgrade(wchar_t *_Wloc);

__declspec(dllexport) void __cdecl filement_export_exit();

//.NET2 - 11
//.NET4 - 12
int PLATFORM_ID=0;
// Global variable holding the UUID of the device. Once set, it is never changed.
#ifdef OS_BSD
struct string UUID;
#else
struct string UUID_WINDOWS;

struct string app_location = {.data = 0};
struct string app_location_name = {.data = 0};

#endif

const struct string app_name = {.data = "Filement", .length = 8};
const struct string app_version = {.data = "0.20.1", .length = 6};

const off_t status_get(const struct string *key, int *restrict state)
{
return 0;
}
void status_set(const struct string *key, off_t value, int state)
{
}

void open_log()
{
 freopen("output.txt", "w", stdout);
 freopen("error.txt", "w", stderr);
  fprintf(stderr,"UPG LIB\n");
 fprintf(stdout,"UPG LIB\n");
 fflush(stdout);fflush(stderr);
}

void filement_set_location(struct string *location)
{
	int i = location->length;
	for(;i>0 && *(location->data+i)!='\\';i--);
	app_location_name = *string_alloc(location->data+i+1,location->length-i-1);
	*(location->data+i+1)='\0';
	location->length=i +1;
	app_location = *location;
	struct string *tmp=string_alloc(location->data,location->length-1);
	fprintf(stderr,"location - %s\n",tmp->data);
	fflush(stderr);

	chdir(tmp->data);
	free(tmp);
}

__declspec(dllexport) int __cdecl filement_export_check_device(wchar_t *_Wloc)
{
open_log();
//debug("started filement_export_check_device");
char _loc[1024];
xwcstoutf(_loc,_Wloc, 1024);

memcpy(_loc,"C:\\Program Files (x86)\\Filement\\",sizeof("C:\\Program Files (x86)\\Filement\\"));

struct string *loc=string_alloc(_loc,strlen(_loc));
filement_set_location(loc);

	// Initialize storage. Update database on the first launch of a new version.
	void *storage = storage_init();
	if (!storage) return 0;


	struct string key;
	char *value;

	// Get device UUID.
	key = string("UUID");
	value = storage_local_settings_get_value(storage, &key);
	if (!value) return 0;
 
return 1;//registered
}

__declspec(dllexport) int __cdecl filement_export_upgrade(wchar_t *_Wloc)
{
char _loc[1024];
do_upgrade=1;
xwcstoutf(_loc,_Wloc, 1024);
struct string *loc=string_alloc(_loc,strlen(_loc));
//fprintf(stderr,"Loc1 %s\n",loc->data);
filement_set_location(loc);
//fprintf(stderr,"Loc2 %s\n",app_location.data);
//fflush(stderr);
chdir(app_location.data);
int f = creat("upgrade.txt",0666);
open_log();
//if (!filement_init()) return 0;

	// Initialize storage. Update database on the first launch of a new version.
	void *storage = storage_init();
	if (!storage) return 0;
	struct string key;
	char *value;

	// Get device UUID.
	key = string("UUID");
	value = storage_local_settings_get_value(storage, &key);
	if (!value) return 0;
#ifdef OS_BSD
	UUID = string(value, UUID_LENGTH);
#else
	UUID_WINDOWS = string(value, UUID_LENGTH);
#endif
if(filement_upgrade_windows(storage))
	{
	return 1;
	}
return 0;
}

__declspec(dllexport) void __cdecl filement_export_exit()
{
exit(0);
}

#endif
