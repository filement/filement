#define TIMEOUT 10000 /* 10s */

int errno_error(int error);

bool readall(int file, char *restrict buffer, size_t total);

bool writeall(int file, const char *buffer, size_t total);

int socket_connect(const char *hostname, unsigned port);
ssize_t socket_read(int fd, char *restrict buffer, size_t total);
ssize_t socket_write(int fd, char *restrict buffer, size_t total);
