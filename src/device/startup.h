extern struct string startup_filement;

bool startup_init(void);
bool startup_add(const struct string *command);
bool startup_remove(const struct string *command);
void startup_term(void);
