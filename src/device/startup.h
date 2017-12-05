extern struct string startup_filement;

bool startup_init(void);
bool startup_cmd_add(const struct string *command);
bool startup_cmd_remove(const struct string *command);
void startup_term(void);

#if !defined(OS_WINDOWS)
extern bool (*startup_add)(const struct string *);
extern bool (*startup_remove)(const struct string *);
#else
bool startup_add(const struct string *command);
bool startup_remove(const struct string *command);
#endif
