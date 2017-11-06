// TODO: use this for pipe check and when sending messages
// TODO: choose good value here
// TODO: the way the messages are stored (linked list) limits INBOX_RECIPIENTS_LIMIT to 1
#define INBOX_RECIPIENTS_LIMIT 1

#define SUBSCRIBE_UUID		1
#define SUBSCRIBE_CLIENT	2

bool subscribe_init(void);
void subscribe_term(void);

bool subscribe_connect(const struct string *uuid, int fd, int control, unsigned target);
bool subscribe_message(const struct string *text, const struct vector *to);
