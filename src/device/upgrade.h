#define FAILSAFE_PATH PREFIX "/bin/filement_failsafe"

#if !defined(OS_WINDOWS)
bool filement_upgrade(const char *exec);
#endif

#ifdef OS_WINDOWS
struct string *upgrade_failsafe(void);

bool remove_directory(const char *dir);
void windows_upgrade();
#endif
