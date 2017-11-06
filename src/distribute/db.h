#include <mysql/mysql.h>				// libmysql

void *restrict db_init(void);
void db_term(void *restrict db);

struct string *db_sql_alloc(const char *format, ...);
