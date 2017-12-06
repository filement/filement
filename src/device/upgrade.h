struct string *upgrade_failsafe(void);

bool filement_upgrade(const struct string *cmd);

#ifdef OS_WINDOWS
bool remove_directory(const char *dir);
void windows_upgrade();
#endif
