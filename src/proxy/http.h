#define PORT_HTTP 80
#define PORT_HTTPS 443

#define HTTP_DATE_LENGTH 29

#define UUID_LENGTH 32

struct stream;

void http_date(char buffer[HTTP_DATE_LENGTH + 1], time_t timestamp);
char *restrict http_uuid(struct stream *restrict stream);
