#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <mysql/mysql.h>                // libmysqlclient

#include "types.h"
#include "io.h"
#include "json.h"
#include "storage_mysql.h"

//TODO stmt on return false is not closed (possible memory leak)

void *restrict storage_init(void)
{
	MYSQL *mysql = malloc(sizeof(MYSQL));
	if (!mysql) return 0;

	if (!mysql_init(mysql))
		return 0;

	if (!mysql_real_connect(mysql, MY_HOSTNAME, MY_USERNAME, MY_PASSWORD, MY_SCHEMA, MY_PORT, 0, 0) || mysql_set_character_set(mysql, "utf8"))
        return 0;

	return mysql;
}

void storage_term(void *restrict storage)
{
	mysql_close((MYSQL *)storage);
	free(storage);
}

bool storage_create(void *restrict storage){return true;}//COMPATABILIY
bool storage_latest(void *restrict storage){return true;}//COMPATABILIY
bool storage_setup(void *restrict storage){return true;}//COMPATABILIY
void storage_reset(void *restrict storage){return ;}//COMPATABILIY

struct string *storage_sql_alloc(const char *format, ...)
{
	size_t length;
	struct string *buffer;
	va_list args;

	va_start(args, format);
	length = (size_t)vsnprintf(0, 0, format, args);
    va_end(args);

	buffer = malloc(sizeof(struct string) + sizeof(char) * (length + 1));
	if (!buffer) return 0;
	buffer->data = (char *)(buffer + 1);

	va_start(args, format);
	buffer->length = vsnprintf(buffer->data, length + 1, format, args);
    va_end(args);

	return buffer;
}

bool storage_local_settings_add_value(void *restrict storage, const struct string *key, const struct string *value)
{
	MYSQL_STMT *stmt = 0;
	storage=storage_init();
	if(!storage)return false;
	const char sql[] = "INSERT INTO local_settings(`key`,`value`) values(?,?)";
	stmt = mysql_stmt_init(storage);
	if (!stmt)goto error;
	if (mysql_stmt_prepare(stmt, sql, sizeof(sql)-1)) goto error;
	
	unsigned long key_length = key->length;
	unsigned long value_length = value->length;
	MYSQL_BIND params[] = {
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = key->data,
			.buffer_length = key_length + 1,
			.length = &key_length
		},
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = value->data,
			.buffer_length = value_length + 1,
			.length = &value_length			
		}
		};
	if (mysql_stmt_bind_param(stmt, params) || mysql_stmt_execute(stmt)) goto error;
	if (!mysql_stmt_affected_rows(stmt)) goto error;

	mysql_stmt_close(stmt);
	
	storage_term(storage);
	return true;
	
error:
storage_term(storage);
return false;
}

bool storage_local_settings_delete_value(void *restrict storage, const struct string *key)
{
	MYSQL_STMT *stmt = 0;
	storage=storage_init();
	if(!storage)return false;
	const char sql[] = "DELETE FROM local_settings WHERE `key` = ?";
	if (!stmt) goto error;
	if (mysql_stmt_prepare(stmt, sql, sizeof(sql)-1)) goto error;
	
	unsigned long key_length = key->length;
	MYSQL_BIND params = 
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = key->data,
			.buffer_length = key_length + 1,
			.length = &key_length
		};
	if (mysql_stmt_bind_param(stmt, &params) || mysql_stmt_execute(stmt)) goto error;
	if (!mysql_stmt_affected_rows(stmt)) goto error;

	mysql_stmt_close(stmt);
	storage_term(storage);
return true;

error:
storage_term(storage);
return false;
}

bool storage_local_settings_set_value(void *restrict storage, const struct string *key, const struct string *value)
{
	MYSQL_STMT *stmt = 0;
	storage=storage_init();
	if(!storage)return false;
	const char sql[] = "UPDATE local_settings SET `value` = ? WHERE `key` = ?";
	stmt = mysql_stmt_init(storage);
	if (!stmt) goto error;
	if (mysql_stmt_prepare(stmt, sql, sizeof(sql)-1)) goto error;
	
	unsigned long key_length = key->length;
	unsigned long value_length = value->length;
	MYSQL_BIND params[] = {
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = key->data,
			.buffer_length = key_length + 1,
			.length = &key_length
		},
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = value->data,
			.buffer_length = value_length + 1,
			.length = &value_length			
		}
		};
	if (mysql_stmt_bind_param(stmt, params) || mysql_stmt_execute(stmt)) goto error;
	if (!mysql_stmt_affected_rows(stmt)) goto error;

	mysql_stmt_close(stmt);
	storage_term(storage);
	return true;
	
error:
storage_term(storage);
return false;
}

char *storage_local_settings_get_value(void *restrict storage, const struct string *key)
{
	
	MYSQL_STMT *stmt = 0;
	storage=storage_init();
	if(!storage)return false;

	char sql[] = "SELECT `value` FROM local_settings WHERE `key` = ?";
	stmt = mysql_stmt_init(storage);
	if (!stmt) goto error;
	if (mysql_stmt_prepare(stmt, sql, sizeof(sql)-1)) goto error;
	
	unsigned long key_length = key->length;
	MYSQL_BIND params = 
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = key->data,
			.buffer_length = key_length + 1,
			.length = &key_length
		};
	if (mysql_stmt_bind_param(stmt, &params))goto error;
	
	char result_data[1024];
	unsigned long result_length=0;
	MYSQL_BIND result = {
		.buffer_type = MYSQL_TYPE_STRING,
		.buffer = result_data,
		.buffer_length = 1024,
		.is_null = 0,
		.length = &result_length
		
	};
	if (mysql_stmt_bind_result(stmt, &result) != 0) goto error;
	
	if (mysql_stmt_execute(stmt) || mysql_stmt_store_result(stmt) || (mysql_stmt_fetch(stmt) == MYSQL_NO_DATA))
		goto error;

	mysql_stmt_close(stmt);
	storage_term(storage);
	return strdup(result_data); 
	
error:
storage_term(storage);
return 0;
}

//local_users

bool storage_local_users_set(void *restrict storage, struct string *user_name, struct string *password)
{
	MYSQL_STMT *stmt = 0;
	storage=storage_init();
	if(!storage)return false;
	const char sql[] = "UPDATE local_users SET password=? WHERE user_name=?";
	stmt = mysql_stmt_init(storage);
	if (!stmt) goto error;
	if (mysql_stmt_prepare(stmt, sql, sizeof(sql)-1)) goto error;
	
	unsigned long user_name_length = user_name->length;
	unsigned long password_length = password->length;
	MYSQL_BIND params[] = {
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = password->data,
			.buffer_length = user_name_length + 1,
			.length = &password_length
		},
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = user_name->data,
			.buffer_length = password_length + 1,
			.length = &user_name_length			
		}
		};
	if (mysql_stmt_bind_param(stmt, params) || mysql_stmt_execute(stmt)) goto error;
	if (!mysql_stmt_affected_rows(stmt)) goto error;

	mysql_stmt_close(stmt);
	storage_term(storage);
	return true;
	
error:
storage_term(storage);
return false;
}

bool storage_local_users_add(void *restrict storage, struct string *user_name, struct string *password)
{
	MYSQL_STMT *stmt = 0;
	storage=storage_init();
	if(!storage)return false;
	const char sql[] = "INSERT INTO local_users (user_name,password) values (?,?)";
	stmt = mysql_stmt_init(storage);
	if (!stmt) goto error;
	if (mysql_stmt_prepare(stmt, sql, sizeof(sql)-1)) goto error;
	
	unsigned long user_name_length = user_name->length;
	unsigned long password_length = password->length;
	MYSQL_BIND params[] = {
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = user_name->data,
			.buffer_length = user_name_length + 1,
			.length = &user_name_length
		},
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = password->data,
			.buffer_length = password_length + 1,
			.length = &password_length			
		}
		};
	if (mysql_stmt_bind_param(stmt, params) || mysql_stmt_execute(stmt)) goto error;
	if (!mysql_stmt_affected_rows(stmt)) goto error;

	mysql_stmt_close(stmt);
	storage_term(storage);
	return true;
	
error:
storage_term(storage);
return false;
}

int storage_local_users_get_id(void *restrict storage, struct string *user_name, struct string *password)
{
	MYSQL_STMT *stmt = 0;
	storage=storage_init();
	if(!storage)return false;
	char sql[] = "SELECT id FROM local_users WHERE user_name = ? AND password = ?";
	stmt = mysql_stmt_init(storage);
	if (!stmt) goto error;
	if (mysql_stmt_prepare(stmt, sql, sizeof(sql)-1)) goto error;
	
	unsigned long user_name_length = user_name->length;
	unsigned long password_length = password->length;
	MYSQL_BIND params[] = {
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = user_name->data,
			.buffer_length = user_name_length + 1,
			.length = &user_name_length
		},
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = password->data,
			.buffer_length = password_length + 1,
			.length = &password_length			
		}
		};
		
		if (mysql_stmt_bind_param(stmt, params))goto error;
	
	int result_id=0;
	MYSQL_BIND result = {
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &result_id,
		.buffer_length = (unsigned long)sizeof(result_id)
	};
	if (mysql_stmt_bind_result(stmt, &result) != 0) goto error;
	
	if (mysql_stmt_execute(stmt) || mysql_stmt_store_result(stmt) || (mysql_stmt_fetch(stmt) == MYSQL_NO_DATA))
		goto error;

	mysql_stmt_close(stmt);
	storage_term(storage);
	return result_id;
	
error:
storage_term(storage);
return 0;
}

//passport_users


bool storage_passport_users_add(void *restrict storage, unsigned passport_id)
{
	MYSQL_STMT *stmt = 0;
	storage=storage_init();
	if(!storage)return false;
	const char sql[] = "INSERT INTO passport_users (passport_id) values (?)";
	stmt = mysql_stmt_init(storage);
	if (!stmt) goto error;
	if (mysql_stmt_prepare(stmt, sql, sizeof(sql)-1)) goto error;
	
	MYSQL_BIND params[] = {
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &passport_id,
			.buffer_length = (unsigned long)sizeof(passport_id)
		}
		};

	if (mysql_stmt_bind_param(stmt, params) || mysql_stmt_execute(stmt)) goto error;
	if (!mysql_stmt_affected_rows(stmt)) goto error;
	storage_term(storage);
	return true;
	
error:
storage_term(storage);
return false;
}

bool storage_passport_users_check(void *restrict storage, struct string *passport_id)
{
	MYSQL_STMT *stmt = 0;
	storage=storage_init();
	if(!storage)return false;
	const char sql[] = "SELECT 1 FROM passport_users WHERE passport_id = ?";
	stmt = mysql_stmt_init(storage);
	if (!stmt) goto error;
	if (mysql_stmt_prepare(stmt, sql, sizeof(sql)-1)) goto error;
	
	unsigned long passport_id_length = passport_id->length;
	MYSQL_BIND param = 
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = passport_id->data,
			.buffer_length = passport_id_length + 1,
			.length = &passport_id_length
		};
	if (mysql_stmt_bind_param(stmt, &param))goto error;
	
	int result_id=0;
	MYSQL_BIND result = {
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &result_id,
		.buffer_length = (unsigned long)sizeof(result_id)
	};
	if (mysql_stmt_bind_result(stmt, &result) != 0) goto error;
	
	if (mysql_stmt_execute(stmt) || mysql_stmt_store_result(stmt) || (mysql_stmt_fetch(stmt) == MYSQL_NO_DATA))
		goto error;

	mysql_stmt_close(stmt);
	storage_term(storage);
	if(result_id==1)return true;

	return false;
	
	
error:
storage_term(storage);
return false;
}

//storage blocks

bool storage_blocks_del(void *restrict storage, int user_id, int block_id)
{
MYSQL_STMT *stmt = 0;
	storage=storage_init();
	if(!storage)return false;

	const char sql[] = "DELETE FROM blocks WHERE user_id = ? and block_id = ?";
	stmt = mysql_stmt_init(storage);
	if (!stmt) goto error;
	if (mysql_stmt_prepare(stmt, sql, sizeof(sql)-1)) goto error;
	
	MYSQL_BIND params[] = {
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &user_id,
			.buffer_length = (unsigned long)sizeof(user_id)
		},
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &block_id,
			.buffer_length = (unsigned long)sizeof(block_id)		
		}
		};
		
	if (mysql_stmt_bind_param(stmt, params) || mysql_stmt_execute(stmt)) goto error;
	mysql_stmt_close(stmt);
	
	const char sql_auth[] = "DELETE FROM auth WHERE user_id = ? and block_id = ?";
	stmt = mysql_stmt_init(storage);
	if (!stmt) goto error;
	if (mysql_stmt_prepare(stmt, sql_auth, sizeof(sql_auth)-1)) goto error;
	MYSQL_BIND params2[] = {
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &user_id,
			.buffer_length = (unsigned long)sizeof(user_id)
		},
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &block_id,
			.buffer_length = (unsigned long)sizeof(block_id)		
		}
		};
		
	mysql_stmt_bind_param(stmt, params2);
	mysql_stmt_execute(stmt);
	mysql_stmt_close(stmt);
	
	
	storage_term(storage);
	return true;
	
error:
storage_term(storage);
return false;
}

bool storage_blocks_add(void *restrict storage, long block_id , int user_id, struct string *location, struct string *size)
{
MYSQL_STMT *stmt = 0;
	storage=storage_init();
	if(!storage)return false;
	const char sql[] = "INSERT INTO blocks (user_id,location,size) values (?,?,?)";
	const char sql2[] = "INSERT INTO blocks (user_id,location,size,block_id) values (?,?,?,?)";
	stmt = mysql_stmt_init(storage);
	if (!stmt) goto error;
	
	
	unsigned long location_length = location->length;
	long int converted_size=strtol(size->data,0,10);
	
	if(block_id)
	{
		if (mysql_stmt_prepare(stmt, sql2, sizeof(sql2)-1)) goto error;
	}
	else
	{
		if (mysql_stmt_prepare(stmt, sql, sizeof(sql)-1)) goto error;
	}
	
	MYSQL_BIND params[] = {
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &user_id,
			.buffer_length = (unsigned long)sizeof(user_id)
		},
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = location->data,
			.buffer_length = location_length + 1,
			.length = &location_length			
		},
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &converted_size,
			.buffer_length = (unsigned long)sizeof(converted_size)		
		}
		};
		
	MYSQL_BIND params2[] = {
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &user_id,
			.buffer_length = (unsigned long)sizeof(user_id)
		},
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = location->data,
			.buffer_length = location_length + 1,
			.length = &location_length			
		},
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &converted_size,
			.buffer_length = (unsigned long)sizeof(converted_size)		
		},
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &block_id,
			.buffer_length = (unsigned long)sizeof(block_id)
		}
		};
		
	if(block_id)
	{
		if (mysql_stmt_bind_param(stmt, params2) || mysql_stmt_execute(stmt)) goto error;
	}
	else
	{
		if (mysql_stmt_bind_param(stmt, params) || mysql_stmt_execute(stmt)) goto error;
	}
	
	if (!mysql_stmt_affected_rows(stmt)) goto error;

	mysql_stmt_close(stmt);
	storage_term(storage);
	return true;
	
error:
storage_term(storage);
return false;
}

struct blocks *storage_blocks_get_by_block_id(void *restrict storage,long int block_id,long int user_id)
{
	MYSQL_STMT *stmt = 0;
		storage=storage_init();
	if(!storage)return 0;
	struct blocks *block=NULL;

	#ifdef PUBCLOUD
	char sql[] = "SELECT location,size,name,uid,gid FROM blocks WHERE block_id = ? and user_id = ?";
	#else
	char sql[] = "SELECT location,size,name FROM blocks WHERE block_id = ? and user_id = ?";
	#endif
	stmt = mysql_stmt_init(storage);
	if (!stmt) goto error;
	if (mysql_stmt_prepare(stmt, sql, sizeof(sql)-1)) goto error;
	
	MYSQL_BIND params[] = {
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &block_id,
			.buffer_length = (unsigned long)sizeof(block_id)
		},
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &user_id,
			.buffer_length = (unsigned long)sizeof(user_id)
		}
		};
	if (mysql_stmt_bind_param(stmt, params)) goto error;
	
	long int size=0;
	char location_data[1024];
	unsigned long location_length=0;
	char name_data[256];
	unsigned long name_length=0;
#ifdef PUBCLOUD
	unsigned uid=65534;
	unsigned gid=65534;
	MYSQL_BIND result[] = {
		{
		.buffer_type = MYSQL_TYPE_STRING,
		.buffer = location_data,
		.buffer_length = 1023,
		.is_null = 0,
		.length = &location_length
		},
		{
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &size,
		.buffer_length = (unsigned long)sizeof(size)
		},
		{
		.buffer_type = MYSQL_TYPE_STRING,
		.buffer = name_data,
		.buffer_length = 255,
		.is_null = 0,
		.length = &name_length
		},
		{
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &uid,
		.buffer_length = (unsigned long)sizeof(uid)
		},
		{
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &gid,
		.buffer_length = (unsigned long)sizeof(gid)
		}
	};
#else
	MYSQL_BIND result[] = {
		{
		.buffer_type = MYSQL_TYPE_STRING,
		.buffer = location_data,
		.buffer_length = 1023,
		.is_null = 0,
		.length = &location_length
		},
		{
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &size,
		.buffer_length = (unsigned long)sizeof(size)
		},
		{
		.buffer_type = MYSQL_TYPE_STRING,
		.buffer = name_data,
		.buffer_length = 255,
		.is_null = 0,
		.length = &name_length
		}
	};
#endif
	if (mysql_stmt_bind_result(stmt, result) != 0) goto error;
	
	if (mysql_stmt_execute(stmt) || mysql_stmt_store_result(stmt) || (mysql_stmt_fetch(stmt) == MYSQL_NO_DATA))
		goto error;

	mysql_stmt_close(stmt);
	
	
		block=(struct blocks *)malloc(sizeof(struct blocks));
		block->user_id = user_id;
		block->block_id = block_id;
		block->location = string_alloc(location_data,location_length);
		block->location_id = block_id;
		block->size = size;
		block->name = string_alloc(name_data,name_length);
#ifdef PUBCLOUD
		block->uid = uid;
		block->gid = gid;
#endif
		
	storage_term(storage);
	return block;

error:
storage_term(storage);
return false;
}

struct blocks_array *storage_blocks_get_blocks(void *restrict storage, int user_id)
{
	MYSQL_STMT *stmt = 0;
	storage=storage_init();
	if(!storage)return false;
	unsigned long long i = 0;
	unsigned long long row_count = 0;
	struct blocks_array *blocks_array=NULL;
	

#ifdef PUBCLOUD
	char sql[] = "SELECT block_id,location,size,name,uid,gid FROM blocks where user_id=?";
#else
	char sql[] = "SELECT block_id,location,size,name FROM blocks where user_id=?";
#endif
	stmt = mysql_stmt_init(storage);
	if (!stmt) goto error;
	if (mysql_stmt_prepare(stmt, sql, sizeof(sql)-1)) goto error;
	
	MYSQL_BIND param = 
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &user_id,
			.buffer_length = (unsigned long)sizeof(user_id)
		};
	if (mysql_stmt_bind_param(stmt, &param)) goto error;
	
	long int size=0;
	long int block_id=0;
	char location_data[1024];
	unsigned long location_length=0;
	char name_data[256];
	unsigned long name_length=0;
#ifdef PUBCLOUD
	unsigned uid=65534;
	unsigned gid=65534;
	MYSQL_BIND result[] = {
		{
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &block_id,
		.buffer_length = (unsigned long)sizeof(block_id)
		},
		{
		.buffer_type = MYSQL_TYPE_STRING,
		.buffer = location_data,
		.buffer_length = 1024,
		.is_null = 0,
		.length = &location_length
		},
		{
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &size,
		.buffer_length = (unsigned long)sizeof(size)
		},
		{
		.buffer_type = MYSQL_TYPE_STRING,
		.buffer = name_data,
		.buffer_length = 255,
		.is_null = 0,
		.length = &name_length
		},
		{
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &uid,
		.buffer_length = (unsigned long)sizeof(uid)
		},
		{
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &gid,
		.buffer_length = (unsigned long)sizeof(gid)
		}
	};
#else
	MYSQL_BIND result[] = {
		{
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &block_id,
		.buffer_length = (unsigned long)sizeof(block_id)
		},
		{
		.buffer_type = MYSQL_TYPE_STRING,
		.buffer = location_data,
		.buffer_length = 1024,
		.is_null = 0,
		.length = &location_length
		},
		{
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &size,
		.buffer_length = (unsigned long)sizeof(size)
		},
		{
		.buffer_type = MYSQL_TYPE_STRING,
		.buffer = name_data,
		.buffer_length = 255,
		.is_null = 0,
		.length = &name_length
		}
	};
#endif
	if (mysql_stmt_bind_result(stmt, result) != 0) goto error;
	if (mysql_stmt_execute(stmt) || mysql_stmt_store_result(stmt))goto error;
	
	row_count=mysql_stmt_num_rows(stmt);
	
	if(!row_count)
		{
		blocks_array=(struct blocks_array *)malloc(sizeof(struct blocks_array));
		blocks_array->blocks_size=0;
		
		mysql_stmt_close(stmt);
		storage_term(storage);
		return blocks_array;
		}
	
	blocks_array=(struct blocks_array *)malloc(sizeof(struct blocks_array) + (sizeof(struct blocks *) * row_count));
	if(!blocks_array){mysql_stmt_close(stmt);goto error;}
	blocks_array->blocks_size=0;
	for (i=0;!mysql_stmt_fetch(stmt);i++)
	{
        blocks_array->blocks_size++;
            blocks_array->blocks[i]=(struct blocks *)malloc(sizeof(struct blocks));
			if(!blocks_array->blocks[i]){mysql_stmt_close(stmt);goto error;}
            blocks_array->blocks[i]->block_id = block_id;
			blocks_array->blocks[i]->user_id = user_id;
			blocks_array->blocks[i]->location = string_alloc(location_data,location_length);
			blocks_array->blocks[i]->location_id = block_id;
			blocks_array->blocks[i]->size = size;
			blocks_array->blocks[i]->name = string_alloc(name_data,name_length);
#ifdef PUBCLOUD
			blocks_array->blocks[i]->uid = uid;
			blocks_array->blocks[i]->gid = gid;
#endif           
    }	

	mysql_stmt_close(stmt);
	storage_term(storage);
	return blocks_array;
	
error:
storage_term(storage);
return false;
}

bool storage_blocks_truncate(void *restrict storage) //Compatability
{
return true;	
}

/*bool storage_insert_proxy_list(void *restrict storage, struct proxy_list *proxy_list)
{
return true;
}

struct proxy_list *storage_get_proxy_list(void *restrict storage)
{
return 0;
}*/

//------- auth functions

struct string *storage_auth_add(void *restrict storage,struct string *auth_id, struct blocks_array *blocks_array, int rw, int count, int user_id, struct string *name, struct string *data)
{
//TODO to check for memory leaks mainly in the loop
	MYSQL_STMT *stmt = 0;
		storage=storage_init();
	if(!storage)return false;
	struct string key;
	struct string *json_serialized=0;
	union json *root=0,*item,*array,*temp;
	unsigned i=0;
	
	const char sql[] = "INSERT INTO auth (auth_id,user_id,block_id,location,rw,count,name,data,uid,gid) VALUES (?,?,?,?,?,?,?,?,?,?)";
	stmt = mysql_stmt_init(storage);
	if (!stmt)goto error;
	if (mysql_stmt_prepare(stmt, sql, sizeof(sql)-1))goto error;
	unsigned long auth_id_length = 0;
	unsigned long name_length = 0;
	unsigned long data_length = 0;
	unsigned long location_length = 0;
	
	root = json_object_old(false);
	item = json_string_old(auth_id);
	key = string("auth_id");
	json_object_insert_old(root, &key, item);
	
	array=json_array();
	if(!array)goto error;
	for(i=0;i<blocks_array->blocks_size;i++)	
	{
	auth_id_length=auth_id->length;
	name_length=name->length;
	data_length=data->length;
	location_length=blocks_array->blocks[i]->location->length;
#ifdef PUBCLOUD
	MYSQL_BIND params[] = {
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = auth_id->data,
			.buffer_length = auth_id_length + 1,
			.length = &auth_id_length
		},
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &user_id,
			.buffer_length = (unsigned long)sizeof(user_id)
		},
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &blocks_array->blocks[i]->block_id,
			.buffer_length = (unsigned long)sizeof(blocks_array->blocks[i]->block_id)
		},
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = blocks_array->blocks[i]->location->data,
			.buffer_length = location_length + 1,
			.length = &location_length			
		},
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &rw,
			.buffer_length = (unsigned long)sizeof(rw)
		},
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &count,
			.buffer_length = (unsigned long)sizeof(count)
		},
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = name->data,
			.buffer_length = name_length + 1,
			.length = &name_length			
		},
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = data->data,
			.buffer_length = data_length + 1,
			.length = &data_length			
		},
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &blocks_array->blocks[i]->uid,
			.buffer_length = (unsigned long)sizeof(blocks_array->blocks[i]->uid)
		},
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &blocks_array->blocks[i]->gid,
			.buffer_length = (unsigned long)sizeof(blocks_array->blocks[i]->gid)
		}
		};
#else
	MYSQL_BIND params[] = {
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = auth_id->data,
			.buffer_length = auth_id_length + 1,
			.length = &auth_id_length
		},
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &user_id,
			.buffer_length = (unsigned long)sizeof(user_id)
		},
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &blocks_array->blocks[i]->block_id,
			.buffer_length = (unsigned long)sizeof(blocks_array->blocks[i]->block_id)
		},
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = blocks_array->blocks[i]->location->data,
			.buffer_length = location_length + 1,
			.length = &location_length			
		},
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &rw,
			.buffer_length = (unsigned long)sizeof(rw)
		},
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &count,
			.buffer_length = (unsigned long)sizeof(count)
		},
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = name->data,
			.buffer_length = name_length + 1,
			.length = &name_length			
		},
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = data->data,
			.buffer_length = data_length + 1,
			.length = &data_length			
		}
		};
#endif
	if (mysql_stmt_bind_param(stmt, params) || mysql_stmt_execute(stmt))goto error;
	if (!mysql_stmt_affected_rows(stmt))goto error;
	
	
	temp=json_object_old(false);
	if(!temp)goto error;
	item = json_string_old(blocks_array->blocks[i]->location);
	if(!item)goto error;
	key = string("location");
	json_object_insert_old(temp, &key, item);
	
	item = json_integer(mysql_stmt_insert_id(stmt));
	if(!item)goto error;
	key = string("id");
	json_object_insert_old(temp, &key, item);

	item = json_integer(blocks_array->blocks[i]->block_id);
	key = string("block_id");
	json_object_insert_old(temp, &key, item);
#ifdef PUBCLOUD
	item = json_integer(blocks_array->blocks[i]->uid);
	key = string("uid");
	json_object_insert_old(temp, &key, item);

	item = json_integer(blocks_array->blocks[i]->gid);
	key = string("gid");
	json_object_insert_old(temp, &key, item);
#endif	
	item = json_string_old(name);
	if(!item)goto error;
	key = string("name");
	json_object_insert_old(temp, &key, item);

	item = json_string_old(data);
	if(!item)goto error;
	key = string("data");
	json_object_insert_old(temp, &key, item);
	
	if(json_array_insert_old(array,temp))goto error;
	
	}
	
	key = string("locations");
	json_object_insert_old(root, &key, array);
	
	mysql_stmt_close(stmt);
	
	json_serialized=json_serialize(root);
	if(root)json_free(root);
	storage_term(storage);
	return json_serialized;
	error:
	if(root)json_free(root);
	storage_term(storage);
	return 0;
	
}

bool storage_auth_set_count(void *restrict storage,struct string *auth_id,int count)
{
MYSQL_STMT *stmt = 0;
	storage=storage_init();
	if(!storage)return false;
	const char sql[] = "UPDATE auth SET count=? WHERE auth_id = ?";
	stmt = mysql_stmt_init(storage);
	if (!stmt) goto error;
	if (mysql_stmt_prepare(stmt, sql, sizeof(sql)-1)) goto error;
	
	unsigned long auth_id_length = auth_id->length;
	MYSQL_BIND params[] = {
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &count,
			.buffer_length = (unsigned long)sizeof(count)
		},
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = auth_id->data,
			.buffer_length = auth_id_length + 1,
			.length = &auth_id_length			
		}
		};
	if (mysql_stmt_bind_param(stmt, params) || mysql_stmt_execute(stmt)) goto error;
	if (!mysql_stmt_affected_rows(stmt)) goto error;

	mysql_stmt_close(stmt);
	storage_term(storage);
return true;

error:
storage_term(storage);
return false;
}

bool storage_auth_delete_value(void *restrict storage,struct string *auth_id)
{
MYSQL_STMT *stmt = 0;
	storage=storage_init();
	if(!storage)return false;
	const char sql[] = "DELETE FROM auth WHERE auth_id = ?";
	stmt = mysql_stmt_init(storage);
	if (!stmt) goto error;
	if (mysql_stmt_prepare(stmt, sql, sizeof(sql)-1)) goto error;
	
	unsigned long auth_id_length = auth_id->length;
	MYSQL_BIND param = 
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = auth_id->data,
			.buffer_length = auth_id_length + 1,
			.length = &auth_id_length
		};
	if (mysql_stmt_bind_param(stmt, &param) || mysql_stmt_execute(stmt)) goto error;
	if (!mysql_stmt_affected_rows(stmt)) goto error;
	
	mysql_stmt_close(stmt);
	storage_term(storage);
return true;

error:
storage_term(storage);
return false;
}

bool storage_auth_delete_location(void *restrict storage,struct string *auth_id,int location_id)
{
MYSQL_STMT *stmt = 0;
	storage=storage_init();
	if(!storage)return false;
	const char sql[] = "DELETE FROM auth WHERE auth_id = ? AND location_id=?";
	stmt = mysql_stmt_init(storage);
	if (!stmt) goto error;
	if (mysql_stmt_prepare(stmt, sql, sizeof(sql)-1)) goto error;
	
	unsigned long auth_id_length = auth_id->length;
	MYSQL_BIND params[] = {
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = auth_id->data,
			.buffer_length = auth_id_length + 1,
			.length = &auth_id_length
		},
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &location_id,
			.buffer_length = (unsigned long)sizeof(location_id)
		}
		
		};
	if (mysql_stmt_bind_param(stmt, params) || mysql_stmt_execute(stmt)) goto error;
	if (!mysql_stmt_affected_rows(stmt)) goto error;
	
	mysql_stmt_close(stmt);
	storage_term(storage);
return true;

error:
storage_term(storage);
return false;
}

struct auth *storage_auth_get(void *restrict storage,struct string *auth_id)
{
MYSQL_STMT *stmt = 0;
	storage=storage_init();
	if(!storage)return false;
int i=0,retval=0;
int row_count = 0;
struct auth *auth=NULL;

time_t cur_time;
time_t max_time;

//remove old auths
time(&cur_time);
max_time=cur_time-1800;
char sql_del[] = "DELETE FROM auth WHERE count>1 AND count<?";
stmt = mysql_stmt_init(storage);
	if (!stmt) goto error;
	if (mysql_stmt_prepare(stmt, sql_del, sizeof(sql_del)-1)) goto error;

	MYSQL_BIND param = 
		{
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &max_time,
		.buffer_length = (unsigned long)sizeof(max_time)
		};
	
	if (mysql_stmt_bind_param(stmt, &param)) goto error;
	if (mysql_stmt_execute(stmt))goto error;
	mysql_stmt_close(stmt);


//end of remove	

	char sql[] = "SELECT user_id,block_id,location,location_id,rw,count,name,data,uid,gid FROM auth WHERE auth_id=?";
	stmt = mysql_stmt_init(storage);
	if (!stmt) goto error;
	if (mysql_stmt_prepare(stmt, sql, sizeof(sql)-1)) goto error;
	
	unsigned long auth_id_length = auth_id->length;
	MYSQL_BIND param2 = 
		{
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = auth_id->data,
			.buffer_length = auth_id_length + 1,
			.length = &auth_id_length
		};
	
	if (mysql_stmt_bind_param(stmt, &param2)) goto error;
	
	long int user_id=0;
	long int block_id=0;
	char location_data[1024];
	unsigned long location_length=0;
	char name_data[1024];
	char data_data[4096];
	unsigned long name_length=0;
	unsigned long data_length=0;
	long int location_id=0;
	int rw=0;
	int count=0;
	int uid=65534;
	int gid=65534;
	MYSQL_BIND result[] = {
		{
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &user_id,
		.buffer_length = (unsigned long)sizeof(user_id)
		},
		{
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &block_id,
		.buffer_length = (unsigned long)sizeof(block_id)
		},
		{
		.buffer_type = MYSQL_TYPE_STRING,
		.buffer = location_data,
		.buffer_length = 1024,
		.is_null = 0,
		.length = &location_length
		},
		{
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &location_id,
		.buffer_length = (unsigned long)sizeof(location_id)
		},
		{
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &rw,
		.buffer_length = (unsigned long)sizeof(rw)
		},
		{
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &count,
		.buffer_length = (unsigned long)sizeof(count)
		},
		{
		.buffer_type = MYSQL_TYPE_STRING,
		.buffer = name_data,
		.buffer_length = 1024,
		.is_null = 0,
		.length = &name_length
		},
		{
		.buffer_type = MYSQL_TYPE_STRING,
		.buffer = data_data,
		.buffer_length = 4096,
		.is_null = 0,
		.length = &data_length
		},
		{
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &uid,
		.buffer_length = (unsigned long)sizeof(uid)
		},
		{
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &gid,
		.buffer_length = (unsigned long)sizeof(gid)
		}
	};
	if (mysql_stmt_bind_result(stmt, result) != 0) goto error;
	if (mysql_stmt_execute(stmt) || mysql_stmt_store_result(stmt))goto error;
	
	row_count=mysql_stmt_num_rows(stmt);	
	
	auth=(struct auth *)malloc(sizeof(struct auth));
	if(!auth)goto error;
	auth->blocks_array=(struct blocks_array *)malloc(sizeof(struct blocks_array)+(sizeof(struct blocks *)*row_count));
	if(!auth->blocks_array)goto error;
    auth->blocks_array->blocks_size=0;

	for(i=0;!mysql_stmt_fetch(stmt);i++)
	{
			auth->blocks_array->blocks_size++;
			
			auth->auth_id = auth_id;
			auth->blocks_array->blocks[i]=(struct blocks *)malloc(sizeof(struct blocks));
			auth->blocks_array->blocks[i]->user_id = user_id;
			auth->blocks_array->blocks[i]->block_id = block_id;
			auth->blocks_array->blocks[i]->location = string_alloc((char *)location_data,(size_t) location_length);
			auth->blocks_array->blocks[i]->location_id = location_id;
			auth->blocks_array->blocks[i]->name = string_alloc("",0);
			auth->rw = rw;
			auth->count = count;
			auth->name = string_alloc((char *)name_data,(size_t) name_length);
			auth->data = string_alloc((char *)data_data,(size_t) data_length);				
#if defined(PUBCLOUD)
			auth->uid = uid;
			auth->gid = gid;
#endif       
    }

	mysql_stmt_close(stmt);
	
	if(auth->count == 1)
	{
	storage_auth_set_count(storage,auth_id,cur_time);
	}
	
	/*
			if(!auth->count)//This must not happens
			{
				free(auth);
				
				storage_auth_delete_value(storage,auth_id);
				
				goto error;
			}
			
			if(auth->count==1)storage_auth_delete_value(storage,auth_id);
			else if(auth->count>1) storage_auth_set_count(storage,auth_id,(auth->count-1)); //if -1 to not decrease
	*/	
	storage_term(storage);
	return auth;
	
error:
storage_term(storage);
return false;
}

struct auth_array *storage_auth_list(void *restrict storage, int user_id, int count)
{
MYSQL_STMT *stmt = 0;
	storage=storage_init();
	if(!storage)return false;
	
int i=0,retval=0;
int row_count = 0;
struct auth_array *auth_array=NULL;

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

stmt = mysql_stmt_init(storage);
	if (!stmt) goto error;
	if (mysql_stmt_prepare(stmt, sql, sql_length)) goto error;
	//TODO make it in better way
	
	if(count)
	{
		MYSQL_BIND params[] = {
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &user_id,
			.buffer_length = (unsigned long)sizeof(user_id)
		},
		{
			.buffer_type = MYSQL_TYPE_LONG,
			.buffer = &count,
			.buffer_length = (unsigned long)sizeof(count)	
		}
		};
		if (mysql_stmt_bind_param(stmt, params))goto error;
	}
	else
	{
	MYSQL_BIND param = 
		{
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &user_id,
		.buffer_length = (unsigned long)sizeof(user_id)
		};
	if (mysql_stmt_bind_param(stmt, &param)) goto error;
	}


	long int block_id=0;
	char auth_id_data[1024];
	unsigned long auth_id_length=0;
	char location_data[1024];
	unsigned long location_length=0;
	char name_data[1024];
	unsigned long name_length=0;
	char data_data[4096];
	unsigned long data_length=0;
	int rw=0;
	unsigned long location_id=0;
	int ret_count=0;
	MYSQL_BIND result[] = {
		{
		.buffer_type = MYSQL_TYPE_STRING,
		.buffer = auth_id_data,
		.buffer_length = 1024,
		.is_null = 0,
		.length = &auth_id_length
		},
		{
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &rw,
		.buffer_length = (unsigned long)sizeof(rw)
		},
		{
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &ret_count,
		.buffer_length = (unsigned long)sizeof(ret_count)
		},
		{
		.buffer_type = MYSQL_TYPE_LONG,
		.buffer = &block_id,
		.buffer_length = (unsigned long)sizeof(block_id)
		},
		{
		.buffer_type = MYSQL_TYPE_STRING,
		.buffer = location_data,
		.buffer_length = 1024,
		.is_null = 0,
		.length = &location_length
		},
        {
        .buffer_type = MYSQL_TYPE_LONG,
        .buffer = &location_id,
        .buffer_length = (unsigned long)sizeof(location_id)
        },
		{
		.buffer_type = MYSQL_TYPE_STRING,
		.buffer = name_data,
		.buffer_length = 1024,
		.is_null = 0,
		.length = &name_length
		},
		{
		.buffer_type = MYSQL_TYPE_STRING,
		.buffer = data_data,
		.buffer_length = 4096,
		.is_null = 0,
		.length = &data_length
		}

		
	};
	if (mysql_stmt_bind_result(stmt, result) != 0) goto error;
	if (mysql_stmt_execute(stmt) || mysql_stmt_store_result(stmt))goto error;
	
	row_count=mysql_stmt_num_rows(stmt);	
	
	auth_array=(struct auth_array *)malloc(sizeof(struct auth_array)+sizeof(struct auth *)*row_count);
	if(!auth_array)goto error;
	auth_array->count=0;

	for (i=0;!mysql_stmt_fetch(stmt);i++)
	{
			auth_array->auth[i]=(struct auth *)malloc(sizeof(struct auth));
			if(!auth_array->auth[i])break;
			auth_array->count++;
			
			auth_array->auth[i]->blocks_array=(struct blocks_array *)malloc(sizeof(struct blocks_array)+(sizeof(struct blocks *)));
			if(!auth_array->auth[i]->blocks_array)break;
			auth_array->auth[i]->blocks_array->blocks_size=0;
			auth_array->auth[i]->auth_id = string_alloc((char *)auth_id_data,(size_t)auth_id_length);
			auth_array->auth[i]->name = string_alloc((char *)name_data,(size_t)name_length);
			auth_array->auth[i]->data = string_alloc((char *)data_data,(size_t)data_length);
			auth_array->auth[i]->rw = rw;
			auth_array->auth[i]->count = ret_count;
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
			auth_array->auth[i]->blocks_array->blocks[0]->block_id = block_id;
			auth_array->auth[i]->blocks_array->blocks[0]->location_id = block_id;
			auth_array->auth[i]->blocks_array->blocks[0]->size = 0;
			auth_array->auth[i]->blocks_array->blocks[0]->location = string_alloc((char *)location_data,(size_t) location_length);
                        auth_array->auth[i]->blocks_array->blocks[0]->location_id =location_id;

			//TODO size
    }

	mysql_stmt_close(stmt);

			
	storage_term(storage);
	return auth_array;	
	
error:
storage_term(storage);
return false;
}
