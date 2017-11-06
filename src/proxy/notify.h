#define NAME "FilementProxy"
#define VERSION "0.18.0"

#define NOTIFY_BUFFER (sizeof(struct stream *) + sizeof(unsigned) + 500)

#define EVENT_OFF		'-'
#define EVENT_ON		'+'
#define EVENT_RESET		'X'
#define EVENT_MESSAGE	'.'

void notify_not_found(struct stream *restrict stream);
void notify_distribute(const struct string *uuid, char event);
bool notify_init(void);
