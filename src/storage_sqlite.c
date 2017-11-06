#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef OS_WINDOWS
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <windows.h>
#include <Shlobj.h>
#include <Shlwapi.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <dirent.h>
#endif

#include "types.h"
#include "format.h"
#include "storage.h"
#include "sqlite.h"
#include "io.h"
#include "json.h"
#include "security.h"

#ifdef OS_WINDOWS
_CRTIMP char* __cdecl __MINGW_NOTHROW 	_strdup (const char*) __MINGW_ATTRIB_MALLOC;
_CRTIMP char* __cdecl __MINGW_NOTHROW	strdup (const char*) __MINGW_ATTRIB_MALLOC;
#endif

//TODO da go napravq po-burzo kato v 1 masiv pri storage_init napravq prepared stmts i v funkciite prosto zamestvam dannite.
//taka spestqvam vremeto za podgotvqne na SQL zaqvkite vseki put

#ifdef OS_ANDROID
char *sqlite_path;
#endif

// WARNING: This function is not thread-safe (because of getenv()).
static struct string *storage_path(void)
{
#if defined(OS_ANDROID)
	return string_alloc(sqlite_path, strlen(sqlite_path));
#elif defined(OS_WINDOWS)
	TCHAR szPath[MAX_PATH];

	if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, szPath))) 
	{
		PathAppend(szPath, "\\Filement");
		mkdir(szPath);
		#if !TEST
		PathAppend(szPath, "\\Filement.db");
		#else
		PathAppend(szPath, "\\flmntdev.db");
		#endif
		return string_alloc(szPath,strlen(szPath));
	}
	#if !TEST
	else return string_alloc("Filement.db",11);
	#else
	else return string_alloc("flmntdev.db",11);
	#endif
#endif

	struct string *path;
	size_t length_home, length;

#if !TEST
# if !defined(OS_IOS)
	struct string filename = string(".filement.db");
# else
	struct string filename = string("Library/.filement.db");
# endif
#else
# if !defined(OS_IOS)
	struct string filename = string(".flmntdev.db");
# else
	struct string filename = string("Library/.flmntdev.db");
# endif
#endif
	char *home = getenv("HOME");
	if (!home) return 0;
	length_home = strlen(home);

	length = length_home + 1 + filename.length;
	path = malloc(sizeof(struct string) + length + 1);
	if (!path) return 0;
	path->data = (char *)(path + 1);
	path->length = length;

	char *start = format_bytes(path->data, home, length_home);
	*start++ = '/';
	*format_bytes(start, filename.data, filename.length) = 0;

	return path;
}

void *storage_init(void)
{
	sqlite3 *sqlite = 0;

	struct string *path = storage_path();
	if (!path) return 0;

	if (sqlite3_open(path->data, &sqlite) != SQLITE_OK)
	{
		sqlite3_close(sqlite);
		free(path);
		return 0;
	}

	free(path);
	return (void *)sqlite;
}

bool storage_create(void *restrict storage)
{
	sqlite3 *sqlite = storage;

	//TODO to make it better
	sqlite3_exec(sqlite,"DROP TABLE IF EXISTS local_settings",0,0,0);
	sqlite3_exec(sqlite,"DROP TABLE IF EXISTS cp_status",0,0,0);
	sqlite3_exec(sqlite,"DROP TABLE IF EXISTS passport_users",0,0,0);
	sqlite3_exec(sqlite,"DROP TABLE IF EXISTS local_users",0,0,0);
	sqlite3_exec(sqlite,"DROP TABLE IF EXISTS blocks",0,0,0);
	sqlite3_exec(sqlite,"DROP TABLE IF EXISTS auth",0,0,0);

	// Create storage tables
	while (true)
	{
		struct string key, value;

		if (sqlite3_exec(sqlite,"CREATE TABLE IF NOT EXISTS local_settings (key TEXT PRIMARY KEY,value TEXT)",0,0,0))
			break;
		if (sqlite3_exec(sqlite,"CREATE TABLE IF NOT EXISTS cp_status (key INTEGER PRIMARY KEY AUTOINCREMENT,size INTEGER,status INTEGER)",0,0,0))
			break;
		if (sqlite3_exec(sqlite,"CREATE TABLE IF NOT EXISTS passport_users (passport_id INT PRIMARY KEY)",0,0,0))
			break;
		if (sqlite3_exec(sqlite,"CREATE TABLE IF NOT EXISTS local_users (id INTEGER PRIMARY KEY AUTOINCREMENT,user_name TEXT UNIQUE,password BLOB,enc_type TEXT,json_additional_data TEXT)",0,0,0))
			break;
		if (sqlite3_exec(sqlite,"CREATE TABLE IF NOT EXISTS blocks (block_id INTEGER PRIMARY KEY AUTOINCREMENT,user_id INTEGER,location TEXT,size INT,name TEXT)",0,0,0))
			break;
		if (sqlite3_exec(sqlite,"CREATE TABLE IF NOT EXISTS auth (location_id INTEGER PRIMARY KEY AUTOINCREMENT,auth_id TEXT,user_id INTEGER,block_id INTEGER,location TEXT,count INT, rw INT, name TEXT, data TEXT)",0,0,0))
			break;

		// Store device version in the database.
		key = string("version");
		extern const struct string app_version;
		if (!storage_local_settings_add_value(sqlite, &key, &app_version)) break;

		// Store DLNA switch in the database.
		key = string("DLNA");
		value = string("1");
		if (!storage_local_settings_add_value(storage, &key, &value)) break;

		// Initialization finished successfully
		return true;
	}

	// Table creation error.
	sqlite3_close(sqlite);
	return false;
}

bool storage_latest(void *restrict storage)
{
	struct string key = string("version");
	extern const struct string app_version;
	char *version = storage_local_settings_get_value(storage, &key);

	// Devices < 0.18 do not store version in the database.
	// TODO remove this when support for device < 0.18 is dropped
	if (!version)
	{
		// This is only necessary for devices < 0.17 but it does no harm on other devices.
		// TODO remove this when support for device < 0.17 is dropped
		sqlite3_exec(storage, "ALTER TABLE blocks ADD COLUMN name TEXT", 0, 0, 0);

		// Add old version to the database so that this function can treat all devices the same way.
		struct string version_old = string("0.17.0");
		if (!storage_local_settings_add_value(storage, &key, &version_old)) return false; // TODO error
		version = storage_local_settings_get_value(storage, &key);
	}

	// WARNING: This relies on app_version.data being NUL-terminated.
	bool latest = !strcmp(version, app_version.data);

	free(version);
	return latest;
}

bool storage_setup(void *restrict storage)
{
	struct string key = string("version");
	extern const struct string app_version;
	//char *version = storage_local_settings_get_value(storage, &key);

	// Update the version stored in the database.
	bool success = storage_local_settings_set_value(storage, &key, &app_version);

	// Add DLNA switch to the database.
	if (success)
	{
		struct string value = string("1");
		key = string("DLNA");
		storage_local_settings_add_value(storage, &key, &value); // this may already exist
	}

	//free(version);
	return success;
}

// Nothing can be done on error so no need to return it
bool storage_term(void *restrict storage)
{
	return (sqlite3_close((sqlite3 *)storage) == SQLITE_OK);
}

void storage_reset(void *restrict storage)
{
	struct string *path = storage_path();
	if (!path) return; // memory error
	storage_term(storage);
	unlink(path->data);
	free(path);
}

bool storage_local_settings_add_value(void *restrict storage, const struct string *key, const struct string *value)
{
	sqlite3_stmt *stmt;
	sqlite3 *sqlite = storage;

	const char sql[] = "INSERT INTO local_settings values (?,?)";
	if(sqlite3_prepare_v2(sqlite, sql, sizeof (sql), & stmt, 0))
	{
		sqlite3_finalize(stmt);
		return false;
	}

	if(sqlite3_bind_text (stmt, 1, key->data,key->length,0)){sqlite3_finalize(stmt);return false;}
	if(sqlite3_bind_text (stmt, 2, value->data,value->length,0)){sqlite3_finalize(stmt);return false;}

	if(SQLITE_DONE != sqlite3_step(stmt)){sqlite3_finalize(stmt);return false;}

	sqlite3_finalize(stmt);
	return true;
}

bool storage_local_settings_delete_value(void *restrict storage, const struct string *key)
{
	sqlite3_stmt *stmt=0;
	sqlite3 *sqlite = storage;

	const char sql[] = "DELETE FROM local_settings WHERE key = ?";
	
	if(sqlite3_prepare_v2(sqlite, sql, sizeof(sql), & stmt, 0)){sqlite3_finalize(stmt);return false;}
	
	if(sqlite3_bind_text (stmt, 1, key->data,key->length,0)){sqlite3_finalize(stmt);return false;}
	
	if(SQLITE_DONE != sqlite3_step(stmt)){sqlite3_finalize(stmt);return false;}

	return true;
}

bool storage_local_settings_set_value(void *restrict storage, const struct string *key, const struct string *value)
{
	sqlite3_stmt *stmt=0;
	sqlite3 *sqlite = storage;

	const char sql[] = "UPDATE local_settings SET value = ? WHERE key = ?";
	
	if(sqlite3_prepare_v2(sqlite, sql, sizeof(sql), & stmt, 0)){sqlite3_finalize(stmt);return false;}
	
	if(sqlite3_bind_text (stmt, 1, value->data,value->length,0)){sqlite3_finalize(stmt);return false;}
	if(sqlite3_bind_text (stmt, 2, key->data,key->length,0)){sqlite3_finalize(stmt);return false;}
	
	if(SQLITE_DONE != sqlite3_step(stmt)){sqlite3_finalize(stmt);return false;}
	
	return true;
}

char *storage_local_settings_get_value(void *restrict storage, const struct string *key)
{
	char *result;

	sqlite3_stmt * stmt=0;
	const char *val = 0;
	int retval;
	sqlite3 *sqlite = storage;

	char sql[] = "SELECT value FROM local_settings WHERE key = ?";
	if(sqlite3_prepare_v2(sqlite, sql, sizeof(sql), & stmt, 0)){sqlite3_finalize(stmt);return 0;}

	if(sqlite3_bind_text (stmt, 1, key->data,key->length,0)){sqlite3_finalize(stmt);return 0;}

	retval = sqlite3_step(stmt);
	if(retval == SQLITE_ROW)
	{
		val = (const char *)sqlite3_column_text(stmt, 0);
		result = strdup(val);
		sqlite3_finalize(stmt);

		return result;
	}

	sqlite3_finalize(stmt);
	return 0;
}

//The storage function for inserting in the tables

//local_users
bool storage_local_users_set(void *restrict storage, struct string *user_name, struct string *password)
{
	sqlite3_stmt * stmt=0;
	sqlite3 *sqlite = storage;

	const char sql[] = "UPDATE local_users SET password=? where user_name=?";
	
	if(sqlite3_prepare_v2(sqlite, sql, sizeof(sql), & stmt, 0)){sqlite3_finalize(stmt);return false;}
	
	if(sqlite3_bind_blob (stmt, 1, password->data, password->length, 0)) {sqlite3_finalize(stmt);return false;}
	if(sqlite3_bind_text (stmt, 2, user_name->data, user_name->length, 0)){sqlite3_finalize(stmt);return false;}
	
	if(SQLITE_DONE != sqlite3_step(stmt)){sqlite3_finalize(stmt);return false;}

	return true;
}

bool storage_local_users_add(void *restrict storage, struct string *user_name, struct string *password)
{
	sqlite3_stmt * stmt=0;
	sqlite3 *sqlite = storage;

	const char sql[] = "INSERT INTO local_users (user_name,password) values (?,?)";
	
	if(sqlite3_prepare_v2(sqlite, sql, sizeof(sql), & stmt, 0)){sqlite3_finalize(stmt);return false;}
	
	if(sqlite3_bind_text (stmt, 1, user_name->data, user_name->length, 0)){sqlite3_finalize(stmt);return false;}
	if(sqlite3_bind_blob (stmt, 2, password->data, password->length, 0)) {sqlite3_finalize(stmt);return false;}
	
	if(SQLITE_DONE != sqlite3_step(stmt)){sqlite3_finalize(stmt);return false;}

	return true;
}

#if !defined(FAILSAFE)
int storage_local_users_get_id(void *restrict storage, struct string *user_name, struct string *password)
{
	sqlite3_stmt * stmt=0;
	sqlite3 *sqlite = storage;
	int val = 0;
	int retval;

	//struct string hash;

	char sql[] = "SELECT id,password FROM local_users WHERE user_name = ?";
	if(sqlite3_prepare_v2(sqlite, sql, sizeof(sql), & stmt, 0)){sqlite3_finalize(stmt);return 0;}

	if(sqlite3_bind_text (stmt, 1, user_name->data,user_name->length,0)){sqlite3_finalize(stmt);return 0;}
	//if(sqlite3_bind_text (stmt, 2, password->data,password->length,0)){sqlite3_finalize(stmt);return 0;}
	retval = sqlite3_step(stmt);
	if(retval == SQLITE_ROW)
	{
		const char *password_real = sqlite3_column_text(stmt, 1);
		if (security_authorize(password, password_real)) val = sqlite3_column_int(stmt, 0);
		sqlite3_finalize(stmt); // TODO password_real should be zeroed for security reasons; will this do it?

		return val;
	}

	sqlite3_finalize(stmt);
	return 0;
}
#endif

//passport_users


bool storage_passport_users_add(void *restrict storage, unsigned passport_id)
{
	sqlite3_stmt * stmt;
	sqlite3 *sqlite = storage;

	const char sql[] = "INSERT INTO passport_users values (?)";

	if(sqlite3_prepare_v2(sqlite, sql, sizeof(sql), & stmt, 0)){sqlite3_finalize(stmt);return false;}

	if(sqlite3_bind_int (stmt, 1, (int)passport_id)){sqlite3_finalize(stmt);return false;}

	if(SQLITE_DONE != sqlite3_step(stmt)){sqlite3_finalize(stmt);return false;}

	return true;
}

bool storage_passport_users_check(void *restrict storage, struct string *passport_id)
{
	sqlite3_stmt * stmt=0;
	int row_count=0;
	sqlite3 *sqlite = storage;

	const char sql[] = "SELECT count(*) FROM passport_users WHERE passport_id = ?";
	if(sqlite3_prepare_v2(sqlite, sql, sizeof(sql), & stmt, 0))
	{
	fprintf(stderr,"%s\n",sqlite3_errmsg(sqlite));
	sqlite3_finalize(stmt);
	return false;
	}
	
	if(sqlite3_bind_text (stmt, 1, passport_id->data,passport_id->length,0)){sqlite3_finalize(stmt);return false;}

	if (sqlite3_step (stmt) == SQLITE_ROW) {
	row_count=sqlite3_column_int(stmt, 0);
	}
	else
	{
		sqlite3_finalize(stmt);
		return false;
	}
	sqlite3_finalize(stmt);

	if(row_count>0)return true;

	return false;
}

bool storage_blocks_truncate(void *restrict storage)
{
	sqlite3 *sqlite = storage;

	if(sqlite3_exec(sqlite, "DELETE FROM blocks",0,0,0))
	{
		printf("The blocks list can not be truncated! \n");	
	}
	
	if(sqlite3_exec(sqlite, "DELETE FROM auth",0,0,0))
	{
		printf("The auth list can not be truncated! \n");	
	}
	
	if(sqlite3_exec(sqlite, "UPDATE sqlite_sequence SET seq = 0 where name='blocks'",0,0,0))
	{
		printf("The blocks list can not be truncated! \n");	
	}
	
	if(sqlite3_exec(sqlite, "UPDATE sqlite_sequence SET seq = 0 where name='auth'",0,0,0))
	{
		printf("The blocks list can not be truncated! \n");	
	}

	return true;	
}

bool storage_blocks_reinit(void *restrict storage) // in some devices we don't want to remove the auths
{
	sqlite3 *sqlite = storage;

	if(sqlite3_exec(sqlite, "DELETE FROM blocks",0,0,0))
	{
		printf("The blocks list can not be truncated! \n");	
	}
	
	if(sqlite3_exec(sqlite, "UPDATE sqlite_sequence SET seq = 0 where name='blocks'",0,0,0))
	{
		printf("The blocks list can not be truncated! \n");	
	}

	return true;	
}

bool storage_blocks_del(void *restrict storage, int user_id, int block_id)
{
	sqlite3_stmt *stmt=0;
	sqlite3 *sqlite = storage;

	const char sql[] = "DELETE FROM blocks WHERE user_id = ? and block_id = ?";
	
	if(sqlite3_prepare_v2(sqlite, sql, sizeof(sql), & stmt, 0)){sqlite3_finalize(stmt);return false;}
	
	if(sqlite3_bind_int (stmt, 1, (int)user_id)){sqlite3_finalize(stmt);return false;}
	if(sqlite3_bind_int (stmt, 2, (int)block_id)){sqlite3_finalize(stmt);return false;}
	
	if(SQLITE_DONE != sqlite3_step(stmt)){sqlite3_finalize(stmt);return false;}

	
	const char sql_auth[] = "DELETE FROM auth WHERE user_id = ? and block_id = ?";
	
	if(sqlite3_prepare_v2(sqlite, sql_auth, sizeof(sql_auth), & stmt, 0)){sqlite3_finalize(stmt);return true;}
	
	if(sqlite3_bind_int (stmt, 1, (int)user_id)){sqlite3_finalize(stmt);return true;}
	if(sqlite3_bind_int (stmt, 2, (int)block_id)){sqlite3_finalize(stmt);return true;}
	
	if(SQLITE_DONE != sqlite3_step(stmt)){sqlite3_finalize(stmt);return true;}
	
return true;
}

bool storage_blocks_add(void *restrict storage, long block_id, int user_id, struct string *location, struct string *size) //size v momenta ne se polzva da go napravq da se polazva
{
	sqlite3_stmt * stmt=0;
	sqlite3 *sqlite = storage;

	const char sql[] = "INSERT INTO blocks (user_id,location,size) values (?,?,0)";
	const char sql2[] = "INSERT INTO blocks (user_id,location,block_id,size) values (?,?,?,0)";

	if(block_id)
	{
		if(sqlite3_prepare_v2(sqlite, sql2, sizeof(sql2), & stmt, 0)){sqlite3_finalize(stmt);return false;}
	}
	else
	{
		if(sqlite3_prepare_v2(sqlite, sql, sizeof(sql), & stmt, 0)){sqlite3_finalize(stmt);return false;}
	}
		
	if(sqlite3_bind_int (stmt, 1, (int)user_id)){sqlite3_finalize(stmt);return false;}
	if(sqlite3_bind_text (stmt, 2, location->data,location->length,0)){sqlite3_finalize(stmt);return false;}
	
	if(block_id)if(sqlite3_bind_int (stmt, 3, (int)block_id)){sqlite3_finalize(stmt);return false;}

	if(SQLITE_DONE != sqlite3_step(stmt)){sqlite3_finalize(stmt);return false;}

	return true;
}

struct blocks *storage_blocks_get_by_block_id(void *restrict storage, long block_id, long user_id)
{
	sqlite3_stmt * stmt=0;
	sqlite3 *sqlite = storage;
	char *val = 0;
	int retval;
	struct blocks *block=NULL;
	int row_count = 0;

/*
block=(struct blocks *)malloc(sizeof(struct blocks));
        block->block_id = block_id;
                block->user_id = 0;
                block->location = (struct string *)malloc(sizeof(struct string));
                block->location->data=strdup("/shared2/");
                block->location->length=9;
                block->size = 0;

return block;
*/
const char *sql;
int sql_length=0;
	if(user_id<0)
	{
	sql = "SELECT count(*) FROM blocks WHERE block_id = ?";
	sql_length = sizeof("SELECT count(*) FROM blocks WHERE block_id = ?")-1;
	}
	else
	{
	sql = "SELECT count(*) FROM blocks WHERE block_id = ? AND user_id = ?";
	sql_length = sizeof("SELECT count(*) FROM blocks WHERE block_id = ? AND user_id = ?") -1 ;
	}
	if(sqlite3_prepare_v2(sqlite, sql, sql_length, & stmt, 0)){sqlite3_finalize(stmt);return 0;}
	if(sqlite3_bind_int (stmt, 1, (int)block_id)){sqlite3_finalize(stmt);return 0;}
	if(user_id>=0)if(sqlite3_bind_int (stmt, 2, (int)user_id)){sqlite3_finalize(stmt);return 0;}			

	retval = sqlite3_step (stmt);
        if (retval == SQLITE_ROW) {
		row_count=sqlite3_column_int(stmt, 0);
		}
		else
		{
			sqlite3_finalize(stmt);
			return 0;
		}
			
	sqlite3_finalize(stmt);	
		if(!row_count)
		{
			return 0;	
		}

	    if(user_id<0)
    {
    sql = "SELECT user_id,location,size,name FROM blocks WHERE block_id = ?";
    sql_length = sizeof("SELECT user_id,location,size,name FROM blocks WHERE block_id = ?")-1;
    }
    else
    {
    sql = "SELECT user_id,location,size,name FROM blocks WHERE block_id = ? AND user_id = ?";
    sql_length = sizeof("SELECT user_id,location,size,name FROM blocks WHERE block_id = ? AND user_id = ?") -1 ;
    }
    if(sqlite3_prepare_v2(sqlite, sql, sql_length, & stmt, 0)){sqlite3_finalize(stmt);return 0;}

	if(sqlite3_bind_int (stmt, 1, (int)block_id)){sqlite3_finalize(stmt);return 0;}
	if(user_id>=0)if(sqlite3_bind_int (stmt, 2, (long)user_id)){sqlite3_finalize(stmt);return 0;}
	block=(struct blocks *)malloc(sizeof(struct blocks));
	block->block_id = block_id;
	retval = sqlite3_step(stmt);
	if(retval == SQLITE_ROW)
	{
		block->user_id = sqlite3_column_int(stmt, 0);
		block->block_id = block_id;
		block->location = string_alloc((char *)sqlite3_column_text(stmt, 1),(size_t) sqlite3_column_bytes(stmt,1));
		block->location_id = block_id;
		block->size = sqlite3_column_int(stmt, 2);
		block->name = string_alloc((char *)sqlite3_column_text(stmt, 3),(size_t) sqlite3_column_bytes(stmt,3));
	}
	else block->location = 0;
	sqlite3_finalize(stmt);

	return block;
}

struct blocks_array *storage_blocks_get_blocks(void *restrict storage, long user_id)
{
	sqlite3_stmt * stmt=0;
	sqlite3 *sqlite = storage;
	int row_count = 0;
	int retval =0;
	int i = 0;
	struct blocks_array *blocks_array=NULL;

	char sql_count[] = "SELECT count(*) FROM blocks where user_id = ?";
	if(sqlite3_prepare_v2(sqlite, sql_count, sizeof(sql_count), & stmt, 0)){sqlite3_finalize(stmt);return 0;}
	if(sqlite3_bind_int (stmt, 1, user_id)){sqlite3_finalize(stmt);return 0;}
			
	retval = sqlite3_step (stmt);
        if (retval == SQLITE_ROW) {
		row_count=sqlite3_column_int(stmt, 0);
		}
		else
		{
			sqlite3_finalize(stmt);
			return 0;
		}
		
		if(!row_count)
		{
		blocks_array=(struct blocks_array *)malloc(sizeof(struct blocks_array));
		blocks_array->blocks_size=0;
		
		sqlite3_finalize(stmt);
		return blocks_array;
		}
		
		sqlite3_finalize(stmt);

	char sql[] = "SELECT block_id,user_id,location,size,name FROM blocks where user_id = ?";
	if(sqlite3_prepare_v2(sqlite, sql, sizeof(sql), & stmt, 0)){sqlite3_finalize(stmt);return 0;}
	if(sqlite3_bind_int (stmt, 1, user_id)){sqlite3_finalize(stmt);return 0;}
	
	blocks_array=(struct blocks_array *)malloc(sizeof(struct blocks_array) + (sizeof(struct blocks *) * row_count));
	blocks_array->blocks_size=row_count;
	for (i=0;i<row_count;i++) {
        int retval;

        retval = sqlite3_step (stmt);
        if (retval == SQLITE_ROW) {
            blocks_array->blocks[i]=(struct blocks *)malloc(sizeof(struct blocks));
            blocks_array->blocks[i]->block_id = sqlite3_column_int(stmt, 0);
			blocks_array->blocks[i]->user_id = sqlite3_column_int(stmt, 1);
			blocks_array->blocks[i]->location = string_alloc((char *)sqlite3_column_text(stmt, 2),(size_t) sqlite3_column_bytes(stmt,2));
			blocks_array->blocks[i]->location_id = sqlite3_column_int(stmt, 0);
			blocks_array->blocks[i]->size = sqlite3_column_int(stmt, 3);
			blocks_array->blocks[i]->name = string_alloc((char *)sqlite3_column_text(stmt, 4),(size_t) sqlite3_column_bytes(stmt,4));
            
        }
        else if (retval == SQLITE_DONE) {
            break;
        }
        else {
			// TODO: this is not the right action for this case
            fprintf (stderr, "Failed.\n");
			return 0;
        }
    }

	sqlite3_finalize(stmt);
	return blocks_array;
}

//proxy list

/*bool storage_insert_proxy_list(void *restrict storage, struct proxy_list *proxy_list)
{
sqlite3_stmt * stmt=0;
sqlite3 *sqlite = storage;
int i=0,retval=0;

	if(sqlite3_exec(sqlite,"DELETE FROM proxy_list",0,0,0))
	{
		printf("The proxy list can not be truncated! \n");	
	}

	const char sql[] = "INSERT INTO proxy_list (host,port) values (?,?)";
	if(sqlite3_prepare_v2(sqlite, sql, sizeof(sql), & stmt, 0)){sqlite3_finalize(stmt);return 0;}

	for(;i<proxy_list->size;i++)
	{
	if(sqlite3_bind_text (stmt, 1, proxy_list->proxy[i]->host->data,proxy_list->proxy[i]->host->length,0)){sqlite3_finalize(stmt);return 0;}
	if(sqlite3_bind_int (stmt, 2, proxy_list->proxy[i]->port)){sqlite3_finalize(stmt);return 0;}

	retval = sqlite3_step(stmt);
	if(retval != SQLITE_DONE)
		printf("Proxy is not inserted properly error: %s\n",sqlite3_errmsg(sqlite));
	
	sqlite3_reset(stmt);
	}
	
	sqlite3_finalize(stmt);
return true;
}


struct proxy_list *storage_get_proxy_list(void *restrict storage)
{
sqlite3_stmt * stmt=0;
sqlite3 *sqlite=storage;
int i=0,retval=0;
int row_count = 0;
struct proxy_list *proxy_list=NULL;
	
char sql_count[] = "SELECT count(*) FROM proxy_list";
if(sqlite3_prepare_v2(sqlite, sql_count, sizeof(sql_count), & stmt, 0)){sqlite3_finalize(stmt);return 0;}

	
	retval = sqlite3_step (stmt);
        if (retval == SQLITE_ROW) {
		row_count=sqlite3_column_int(stmt, 0);
		}
		else
		{
			sqlite3_finalize(stmt);
			return 0;
		}
		
		if(!row_count)
		{
		proxy_list=(struct proxy_list *)malloc(sizeof(struct proxy_list));
		proxy_list->size=0;
		sqlite3_finalize(stmt);
		return proxy_list;
		}
		
		sqlite3_finalize(stmt);

	char sql[] = "SELECT host,port FROM proxy_list";
	if(sqlite3_prepare_v2(sqlite, sql, sizeof(sql), & stmt, 0)){sqlite3_finalize(stmt);return proxy_list;}

	proxy_list=(struct proxy_list *)malloc(sizeof(struct proxy_list) * (sizeof(struct proxy *) * row_count));
	proxy_list->size=row_count;
	for (i=0;i<row_count;i++) {
        int retval;

        retval = sqlite3_step (stmt);
        if (retval == SQLITE_ROW) {
			
            proxy_list->proxy[i]=(struct proxy *)malloc(sizeof(struct proxy));
			proxy_list->proxy[i]->port = sqlite3_column_int(stmt, 1);
			proxy_list->proxy[i]->host = string_alloc((char *)sqlite3_column_text(stmt, 0),(size_t) sqlite3_column_bytes(stmt,0));
            
        }
        else if (retval == SQLITE_DONE) {
            break;
        }
        else {
			// TODO: this is not the right action for this case
            fprintf (stderr, "Failed.\n");
            return 0;
        }
    }

	sqlite3_finalize(stmt);
	return proxy_list;	
}*/

//------- auth functions

struct string *storage_auth_add(void *restrict storage,struct string *auth_id, struct blocks_array *blocks_array, int rw, int count, int user_id,struct string *name ,struct string *data )
{
//TODO to check for memory leaks mainly in the loop
	sqlite3_stmt *stmt;
	sqlite3 *sqlite = storage;
	struct string key;
	struct string *json_serialized=0;
	union json *root,*item,*array,*temp;
	unsigned i=0;
	
	const char sql[] = "INSERT INTO auth (auth_id,user_id,block_id,location,rw,count,name,data) VALUES (?,?,?,?,?,?,?,?)";
	if(sqlite3_prepare_v2(sqlite, sql, sizeof (sql), & stmt, 0))
	{
		sqlite3_finalize(stmt);
		return 0;
	}
	
	root = json_object_old(false);
	item = json_string_old(auth_id);
	key = string("auth_id");
	json_object_insert_old(root, &key, item);
	
	item = json_string_old(name);
	key = string("name");
	json_object_insert_old(root, &key, item);
	
	item = json_string_old(name);
	key = string("data");
	json_object_insert_old(root, &key, item);
	
	array=json_array();
	if(!array)goto error;
	for(i=0;i<blocks_array->blocks_size;i++)	
	{
	
	if(sqlite3_bind_text (stmt, 1, auth_id->data,auth_id->length,0)){sqlite3_finalize(stmt);goto error;}
	if(sqlite3_bind_int  (stmt, 2, user_id)){sqlite3_finalize(stmt);goto error;}
	if(sqlite3_bind_int  (stmt, 3, blocks_array->blocks[i]->block_id)){sqlite3_finalize(stmt);goto error;}
	if(sqlite3_bind_text (stmt, 4, blocks_array->blocks[i]->location->data,blocks_array->blocks[i]->location->length,0)){sqlite3_finalize(stmt);goto error;}
	if(sqlite3_bind_int  (stmt, 5, rw)){sqlite3_finalize(stmt);goto error;}
	if(sqlite3_bind_int (stmt, 6, count)){sqlite3_finalize(stmt);return false;}
	if(sqlite3_bind_text (stmt, 7, name->data,name->length,0)){sqlite3_finalize(stmt);return false;}
	if(sqlite3_bind_text (stmt, 8, data->data,data->length,0)){sqlite3_finalize(stmt);return false;}
	

	if(SQLITE_DONE != sqlite3_step(stmt)){sqlite3_finalize(stmt);goto error;}
	
	temp=json_object_old(false);
	if(!temp)goto error;
	item = json_string_old(blocks_array->blocks[i]->location);
	if(!item)goto error;
	key = string("location");
	json_object_insert_old(temp, &key, item);
	
	item = json_integer(sqlite3_last_insert_rowid(sqlite));
	if(!item)goto error;
	key = string("id");
	json_object_insert_old(temp, &key, item);

	item = json_integer(blocks_array->blocks[i]->block_id);
	key = string("block_id");
	json_object_insert_old(temp, &key, item);

	sqlite3_reset(stmt);
	

	if(json_array_insert_old(array,temp))goto error;
	}
	
	key = string("locations");
	json_object_insert_old(root, &key, array);
	
	sqlite3_finalize(stmt);
	
	json_serialized=json_serialize(root);
	json_free(root);
	return json_serialized;
	error:
	json_free(root);
	return 0;
}

bool storage_auth_set_count(void *restrict storage,struct string *auth_id,int count)
{
	sqlite3_stmt *stmt=0;
	sqlite3 *sqlite = storage;
	
	const char sql[] = "UPDATE auth SET count=? WHERE auth_id = ?";
	
	if(sqlite3_prepare_v2(sqlite, sql, sizeof(sql), & stmt, 0)){sqlite3_finalize(stmt);return false;}

	if(sqlite3_bind_int (stmt, 1, count)){sqlite3_finalize(stmt);return false;}
	
	if(sqlite3_bind_text (stmt, 2, auth_id->data,auth_id->length,0)){sqlite3_finalize(stmt);return false;}
	
	if(SQLITE_DONE != sqlite3_step(stmt)){sqlite3_finalize(stmt);return false;}

	
return true;
}

bool storage_auth_delete_value(void *restrict storage,struct string *auth_id)
{
	sqlite3_stmt *stmt=0;
	sqlite3 *sqlite = storage;

	const char sql[] = "DELETE FROM auth WHERE auth_id = ?";
	
	if(sqlite3_prepare_v2(sqlite, sql, sizeof(sql), & stmt, 0)){sqlite3_finalize(stmt);return false;}
	
	if(sqlite3_bind_text (stmt, 1, auth_id->data,auth_id->length,0)){sqlite3_finalize(stmt);return false;}
	
	if(SQLITE_DONE != sqlite3_step(stmt)){sqlite3_finalize(stmt);return false;}

	
return true;
}

bool storage_auth_delete_location(void *restrict storage,struct string *auth_id,int location_id)
{
	sqlite3_stmt *stmt=0;
	sqlite3 *sqlite = storage;

	const char sql[] = "DELETE FROM auth WHERE auth_id = ? AND location_id = ?";
	
	if(sqlite3_prepare_v2(sqlite, sql, sizeof(sql), & stmt, 0)){sqlite3_finalize(stmt);return false;}
	
	if(sqlite3_bind_text (stmt, 1, auth_id->data,auth_id->length,0)){sqlite3_finalize(stmt);return false;}
	if(sqlite3_bind_int (stmt, 2, location_id)){sqlite3_finalize(stmt);return false;}
	
	if(SQLITE_DONE != sqlite3_step(stmt)){sqlite3_finalize(stmt);return false;}

	
return true;
}


struct auth *storage_auth_get(void *restrict storage,struct string *auth_id)
{
sqlite3_stmt * stmt=0;
sqlite3 *sqlite=storage;
int i=0,retval=0;
int row_count = 0;
struct auth *auth=NULL;

time_t cur_time;
time_t max_time;

//remove old auths
time(&cur_time);
max_time=cur_time-1800;
char sql_del[] = "DELETE FROM auth WHERE count>1 AND count<?";
if(sqlite3_prepare_v2(sqlite, sql_del, sizeof(sql_del), & stmt, 0)){sqlite3_finalize(stmt);return 0;}

if(sqlite3_bind_int (stmt, 1, max_time)){sqlite3_finalize(stmt);return 0;}
retval = sqlite3_step (stmt);
sqlite3_finalize(stmt);

//end of remove
	
char sql_count[] = "SELECT count(*) FROM auth WHERE auth_id=? AND count!=0";
if(sqlite3_prepare_v2(sqlite, sql_count, sizeof(sql_count), & stmt, 0)){sqlite3_finalize(stmt);return 0;}

if(sqlite3_bind_text (stmt, 1, auth_id->data,auth_id->length,0)){sqlite3_finalize(stmt);return 0;}
	
	retval = sqlite3_step (stmt);
        if (retval == SQLITE_ROW) {
		row_count=sqlite3_column_int(stmt, 0);
		}
		else
		{
			sqlite3_finalize(stmt);
			return 0;
		}
		
		if(!row_count)
		{
		sqlite3_finalize(stmt);
		return 0;
		}
		
sqlite3_finalize(stmt);

	char sql[] = "SELECT user_id,block_id,location,location_id,rw,count,name,data FROM auth WHERE auth_id=? AND count!=0";
	if(sqlite3_prepare_v2(sqlite, sql, sizeof(sql), & stmt, 0)){sqlite3_finalize(stmt);return 0;}

	if(sqlite3_bind_text (stmt, 1, auth_id->data,auth_id->length,0)){sqlite3_finalize(stmt);return 0;}
	
	auth=(struct auth *)malloc(sizeof(struct auth));
	if(!auth)return 0;
	auth->blocks_array=(struct blocks_array *)malloc(sizeof(struct blocks_array)+(sizeof(struct blocks *)*row_count));
	if(!auth->blocks_array)return 0;
    auth->blocks_array->blocks_size=row_count;

	for(i=0;i<row_count;i++)
	{
        retval = sqlite3_step (stmt);
        if (retval == SQLITE_ROW) {
			
			auth->auth_id = auth_id;
			auth->blocks_array->blocks[i]=(struct blocks *)malloc(sizeof(struct blocks));
			auth->blocks_array->blocks[i]->user_id = sqlite3_column_int(stmt, 0);
			auth->blocks_array->blocks[i]->block_id = sqlite3_column_int(stmt, 1);
			auth->blocks_array->blocks[i]->location = string_alloc((char *)sqlite3_column_text(stmt, 2),(size_t) sqlite3_column_bytes(stmt,2));
			auth->blocks_array->blocks[i]->location_id = (unsigned long)sqlite3_column_int(stmt, 3);
			auth->blocks_array->blocks[i]->name = string_alloc("",0);
			auth->rw = sqlite3_column_int(stmt, 4);
			auth->count = sqlite3_column_int(stmt, 5);
			auth->name = string_alloc((char *)sqlite3_column_text(stmt, 6),(size_t) sqlite3_column_bytes(stmt,6));	
			auth->data = string_alloc((char *)sqlite3_column_text(stmt, 7),(size_t) sqlite3_column_bytes(stmt,7));
            				
			
        }
        else if (retval == SQLITE_DONE) {
        break;
        }
        else {
			// TODO: this is not the right action for this case
            fprintf (stderr, "Failed.\n");
            return 0;
        }
    }
	
	sqlite3_finalize(stmt);
	
	if(auth->count == 1)
	{
		char sql_count[] = "UPDATE auth SET count=? WHERE auth_id=?";
		if(sqlite3_prepare_v2(sqlite, sql_count, sizeof(sql_count), & stmt, 0)){sqlite3_finalize(stmt);return 0;}

		if(sqlite3_bind_int (stmt, 1, cur_time)){sqlite3_finalize(stmt);return 0;}
		if(sqlite3_bind_text (stmt, 2, auth_id->data,auth_id->length,0)){sqlite3_finalize(stmt);return 0;}
		retval = sqlite3_step (stmt);
		sqlite3_finalize(stmt);
	}

	
	/*
			if(!auth->count)//This must not happens
			{
				free(auth);
				
				storage_auth_delete_value(storage,auth_id);
				
				return 0;
			}
			
			if(auth->count==1)storage_auth_delete_value(storage,auth_id);
			else if(auth->count>1) storage_auth_set_count(storage,auth_id,(auth->count-1)); //if -1 to not decrease
	*/		
	
	return auth;	
}


struct auth_array *storage_auth_list(void *restrict storage, int user_id, int count)
{
sqlite3_stmt * stmt=0;
sqlite3 *sqlite=storage;
int i=0,retval=0;
int row_count = 0;
struct auth_array *auth_array=NULL;

const char *sql_count;
size_t sql_count_length;
if(count)
{
	sql_count = "SELECT count(*) FROM auth WHERE user_id=? AND count=?";
	sql_count_length = sizeof("SELECT count(*) FROM auth WHERE user_id=? AND count=?") - 1;
}
else
{
	sql_count = "SELECT count(*) FROM auth WHERE user_id=?";
	sql_count_length = sizeof("SELECT count(*) FROM auth WHERE user_id=?") - 1;
}

if(sqlite3_prepare_v2(sqlite, sql_count, sql_count_length, & stmt, 0)){sqlite3_finalize(stmt);return 0;}

if(sqlite3_bind_int (stmt, 1, user_id)){sqlite3_finalize(stmt);return 0;}
if(count)if(sqlite3_bind_int (stmt, 2, count)){sqlite3_finalize(stmt);return 0;}

	retval = sqlite3_step (stmt);
        if (retval == SQLITE_ROW) {
		row_count=sqlite3_column_int(stmt, 0);
		}
		else
		{
			sqlite3_finalize(stmt);
			return 0;
		}
		
		if(!row_count)
		{
		sqlite3_finalize(stmt);
		auth_array=(struct auth_array *)malloc(sizeof(struct auth_array)+sizeof(struct auth *)*row_count);
		if(!auth_array)return 0;
		auth_array->count=0;
		return auth_array;
		}
		
sqlite3_finalize(stmt);

const char *sql;
size_t sql_length;

	if(count)
	{
	sql = "SELECT auth_id,rw,count,block_id,location,location_id,name,data FROM auth WHERE user_id=? and count=?";
	sql_length = sizeof("SELECT auth_id,rw,count,block_id,location,location_id,name,data FROM auth WHERE user_id=? and count=?") - 1;
	}
	else
	{
	sql = "SELECT auth_id,rw,count,block_id,location,location_id,name,data FROM auth WHERE user_id=?";
	sql_length = sizeof("SELECT auth_id,rw,count,block_id,location,location_id,name,data FROM auth WHERE user_id=?") - 1;
	}
	if(sqlite3_prepare_v2(sqlite, sql, sql_length, & stmt, 0)){printf("%s\n",sqlite3_errmsg(sqlite));sqlite3_finalize(stmt);return 0;}

	if(sqlite3_bind_int (stmt, 1, user_id)){sqlite3_finalize(stmt);return 0;}
	if(count)if(sqlite3_bind_int (stmt, 2, count)){sqlite3_finalize(stmt);return 0;}
	
	auth_array=(struct auth_array *)malloc(sizeof(struct auth_array)+sizeof(struct auth *)*row_count);
	if(!auth_array)return 0;
	auth_array->count=0;

	for(i=0;i<row_count;i++)
	{
        retval = sqlite3_step (stmt);
        if (retval == SQLITE_ROW) {
			auth_array->auth[i]=(struct auth *)malloc(sizeof(struct auth));
			if(!auth_array->auth[i])break;
			auth_array->count++;
			
			auth_array->auth[i]->blocks_array=(struct blocks_array *)malloc(sizeof(struct blocks_array)+(sizeof(struct blocks *)));
			if(!auth_array->auth[i]->blocks_array)break;
			auth_array->auth[i]->blocks_array->blocks_size=0;
			auth_array->auth[i]->auth_id = string_alloc((char *)sqlite3_column_text(stmt, 0),(size_t) sqlite3_column_bytes(stmt,0));
			auth_array->auth[i]->rw = sqlite3_column_int(stmt, 1);
			auth_array->auth[i]->count = sqlite3_column_int(stmt, 2);
			auth_array->auth[i]->blocks_array->blocks[0]=(struct blocks *)malloc(sizeof(struct blocks));
			if(!auth_array->auth[i]->blocks_array->blocks[0])
				{
				free(auth_array->auth[i]->auth_id);auth_array->auth[i]->auth_id=0;
				free(auth_array->auth[i]);
				auth_array->auth[i]=0;
				break;
				}
			auth_array->auth[i]->blocks_array->blocks_size=1;
			auth_array->auth[i]->blocks_array->blocks[0]->user_id = user_id;
			auth_array->auth[i]->blocks_array->blocks[0]->block_id = sqlite3_column_int(stmt, 3);
			auth_array->auth[i]->blocks_array->blocks[0]->location = string_alloc((char *)sqlite3_column_text(stmt, 4),(size_t) sqlite3_column_bytes(stmt,4));
            auth_array->auth[i]->blocks_array->blocks[0]->location_id = sqlite3_column_int(stmt, 5);		
			auth_array->auth[i]->name = string_alloc((char *)sqlite3_column_text(stmt, 6),(size_t) sqlite3_column_bytes(stmt,6));
			auth_array->auth[i]->data = string_alloc((char *)sqlite3_column_text(stmt, 6),(size_t) sqlite3_column_bytes(stmt,6));

        }
        else if (retval == SQLITE_DONE) {
        break;
        }
        else {
			// TODO: this is not the right action for this case
            fprintf (stderr, "Failed.\n");
            return 0;
        }
    }

	sqlite3_finalize(stmt);

			
	
	return auth_array;	
}


