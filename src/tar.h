#define TAR_BLOCK_SIZE 512

ssize_t tar_field(const char *restrict buffer, size_t size);

#if !defined(OS_WINDOWS)
int evfs_tar(const struct string *restrict location, unsigned depth, const struct string *restrict buffer, struct buffer *restrict cwd, evfs_callback_t callback, void *argument, unsigned flags);
#else
int evfs_tar(const struct string *restrict location,  struct string *restrict buffer, struct buffer *restrict cwd, evfs_callback_t callback, void *argument, unsigned depth, int buffer_fd);
#endif
