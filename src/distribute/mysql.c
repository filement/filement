#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <mysql/mysql.h>                // libmysqlclient

#include "types.h"
#include "db.h"

#if !TEST
# define MY_HOSTNAME	"172.16.1.200"
#else
# define MY_HOSTNAME	"127.0.0.1"
#endif
#define MY_PORT		3306
#define MY_USERNAME	"root"
#define MY_PASSWORD	"parola"
#define MY_SCHEMA	"filement_distribute"

void *restrict db_init(void)
{
	MYSQL *mysql = mysql_init(0);
	if (!mysql) return 0;

	if (!mysql_real_connect(mysql, MY_HOSTNAME, MY_USERNAME, MY_PASSWORD, MY_SCHEMA, MY_PORT, 0, 0) || mysql_set_character_set(mysql, "utf8"))
	{
		mysql_close(mysql);
		return 0;
	}

	return mysql;
}

void db_term(void *restrict db)
{
	mysql_close((MYSQL *)db);
}

struct string *db_sql_alloc(const char *format, ...)
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
