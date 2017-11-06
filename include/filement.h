#include "types.h"

bool filement_init(void);
void filement_term(void);

void filement_daemon(void);

#if !defined(OS_WINDOWS)
bool filement_register(const struct string *email, const struct string *name, const struct string *password);
#endif

bool filement_start(void);
void filement_stop(void);
void filement_serve(void);

bool filement_upgrade(void);
void filement_reset(void);

#ifdef OS_WINDOWS
extern struct string app_location;
extern struct string app_revision;

int xwcstoutf(char *utf, const wchar_t *wcs, size_t utflen);
int xutftowcsn(wchar_t *wcs, const char *utf, size_t wcslen, int utflen); //utflen e -1

void filement_set_location(struct string *location);
struct string *filement_get_version();

bool filement_upgrade_windows(void *storage);

bool filement_register(const struct string *email, const struct string *name, const struct string *password);
#endif
