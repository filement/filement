#define HAVE_WINSOCK 1

#include <io.h>


#define ENOTCONN        WSAENOTCONN
#define EWOULDBLOCK     WSAEWOULDBLOCK
#define ENOBUFS         WSAENOBUFS
#define ECONNRESET      WSAECONNRESET
#define ESHUTDOWN       WSAESHUTDOWN
#define EAFNOSUPPORT    WSAEAFNOSUPPORT
#define EPROTONOSUPPORT WSAEPROTONOSUPPORT
#define EINPROGRESS     WSAEINPROGRESS
#define EISCONN         WSAEISCONN

//#undef off_t
//#define off_t __int64

#define S_IFLNK 0120000 /* Symbolic link */
#define S_ISLNK(x) (((x) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(x) 0

#define S_IRGRP 0
#define S_IWGRP 0
#define S_IXGRP 0
#define S_IRWXG (S_IRGRP | S_IWGRP | S_IXGRP)
#define S_IROTH 0
#define S_IWOTH 0
#define S_IXOTH 0
#define S_IRWXO (S_IROTH | S_IWOTH | S_IXOTH)

#define S_ISUID 0004000
#define S_ISGID 0002000
#define S_ISVTX 0001000

#define WIFEXITED(x) 1
#define WIFSIGNALED(x) 0
#define WEXITSTATUS(x) ((x) & 0xff)
#define WTERMSIG(x) SIGTERM

#define SHUT_WR SD_SEND

#define SIGHUP 1
#define SIGQUIT 3
#define SIGKILL 9
#define SIGPIPE 13
#define SIGALRM 14
#define SIGCHLD 17

#define F_GETFD 1
#define F_SETFD 2
#define FD_CLOEXEC 0x1

#define EAFNOSUPPORT WSAEAFNOSUPPORT
#define ECONNABORTED WSAECONNABORTED

#define POLLIN      0x0001    /* There is data to read */
#define POLLPRI     0x0002    /* There is urgent data to read */
#define POLLOUT     0x0004    /* Writing now will not block */
#define POLLERR     0x0008    /* Error condition */
#define POLLHUP     0x0010    /* Hung up */
#define POLLNVAL    0x0020    /* Invalid request: fd not open */
struct pollfd {
    SOCKET fd;        /* file descriptor */
    short events;     /* requested events */
    short revents;    /* returned events */
};
#define poll(x, y, z)        mingw_poll(x, y, z)


#define socket(x, y, z)      mingw_socket(x, y, z)
//#define connect(x, y, z)     mingw_connect(x, y, z)
#define accept(x, y, z)      mingw_accept(x, y, z)
#define shutdown(x, y)       mingw_shutdown(x, y)
#define getpeername(x, y, z) mingw_getpeername(x, y, z)

/* Wrapper macros to call misc. functions mingw is missing */
#define sleep(x)             mingw_sleep(x)
#define inet_aton(x, y)      mingw_inet_aton(x, y)
#define gettimeofday(x, y)   mingw_gettimeofday(x, y)
//#define stat(x, y)           mingw_stat(x, y)

#define STLINK_MMAP_H


#define PROT_READ (1<<0)
#define PROT_WRITE (1<<1)

#define MAP_SHARED (1<<0)
#define MAP_PRIVATE (1<<1)

#define MAP_ANONYMOUS (1<<5)

#define MAP_FAILED ((void *)-1)

//void *mmap(void *addr, size_t len, int prot, int flags, int fd, long long offset);
//int munmap(void *addr, size_t len);

/* Winsock uses int instead of the usual socklen_t */
typedef int socklen_t;

/* Function prototypes for functions in mingw.c */
int pipe(int filedes[2]);
unsigned int mingw_sleep(unsigned int);
int     mingw_inet_aton(const char *, struct in_addr *);
int     mingw_gettimeofday(struct timeval *, char *);
int     mingw_poll(struct pollfd *, unsigned int, int);
SOCKET  mingw_socket(int, int, int);
int     mingw_connect(SOCKET, struct sockaddr*, socklen_t);
SOCKET  mingw_accept(SOCKET, struct sockaddr*, socklen_t *);
int     mingw_shutdown(SOCKET, int);
int     mingw_getpeername(SOCKET, struct sockaddr*, socklen_t *);

/* Three socket specific macros */
#define READ(x, y, z)  mingw_read_socket(x, y, z)
#define WRITE(x, y, z) mingw_write_socket(x, y, z)
#define CLOSE(x)       mingw_close_socket(x)

size_t strnlen(const char *buf,size_t len);
int mingw_read_socket(SOCKET, void *, int);
int mingw_write_socket(SOCKET,const void *, int);
int mingw_close_socket(SOCKET);

int mingw_setnonblocking(SOCKET, int);
//int mingw_stat(const char*, struct stat*);
BOOL DeleteDirectory(const TCHAR* sPath);

struct iovec {
    void *iov_base;   /* Starting address */
    size_t iov_len;   /* Number of bytes */
};
#define WRITEV(x, y, z) polipo_writev(x, y, z)
#define READV(x, y, z)  polipo_readv(x, y, z)
int readv(int fd, const struct iovec *vector, int count);
int writev(int fd, const struct iovec *vector, int count);

// GIT implementation

#define DIRENT_H




typedef struct DIR DIR;

#define DT_UNKNOWN 0
#define DT_DIR 1
#define DT_REG 2
#define DT_LNK 3

struct dirent {
unsigned char d_type; /* file type to prevent lstat after readdir */
char d_name[MAX_PATH * 3]; /* file name (* 3 for UTF-8 conversion) */
};

DIR *mingw_opendir(const char *dirname);
struct dirent *mingw_readdir(DIR *dir);
int mingw_closedir(DIR *dir);

int mingw_lstat(const char *file_name, struct _stati64 *buf);
int mingw_stat(const char *file_name, struct _stati64 *buf);
int mingw_fstat(int fd, struct _stati64 *buf);
int mingw_mkdir(const char *path, int mode);
int mingw_open (const char *filename, int oflags, ...);
int mingw_remove(const char *pathname);
struct tm *mingw_gmtime_r(const time_t *tp, struct tm *result);


#define opendir(x)      mingw_opendir(x)
#define readdir(x)      mingw_readdir(x)
#define closedir(x)     mingw_closedir(x)
#define mkdir(x,y) 		mingw_mkdir(x,y)
#define stat(x,y) 		mingw_stat(x,y)
#define fstat(x,y) 		mingw_fstat(x,y)
#define lstat(x,y) 		mingw_lstat(x,y)
#define open 			mingw_open
#define remove(x) 		mingw_remove(x)
#define gmtime_r(x,y)	mingw_gmtime_r(x,y)
/*
DIR *opendir(const char *dirname);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);
*/

struct DIR {
struct dirent dd_dir; /* includes d_type */
HANDLE dd_handle; /* FindFirstFile handle */
int dd_stat; /* 0-based index */
};


#define is_dir_sep(c) ((c) == '/' || (c) == '\\')
static inline char *mingw_find_last_dir_sep(const char *path)
{
char *ret = NULL;
for (; *path; ++path)
if (is_dir_sep(*path))
ret = (char *)path;
return ret;
}
#define find_last_dir_sep mingw_find_last_dir_sep

int err_win_to_posix(DWORD winerr);

int xutftowcsn(wchar_t *wcs, const char *utf, size_t wcslen, int utflen);

/**
* Simplified variant of xutftowcsn, assumes input string is \0-terminated.
*/
static inline int xutftowcs(wchar_t *wcs, const char *utf, size_t wcslen)
{
return xutftowcsn(wcs, utf, wcslen, -1);
}

/**
* Simplified file system specific variant of xutftowcsn, assumes output
* buffer size is MAX_PATH wide chars and input string is \0-terminated,
* fails with ENAMETOOLONG if input string is too long.
*/
static inline int xutftowcs_path(wchar_t *wcs, const char *utf)
{
int result = xutftowcsn(wcs, utf, MAX_PATH, -1);
if (result < 0)
errno = 888;
return result;
}

/**
* Converts UTF-16LE encoded string to UTF-8.
*
* Maximum space requirement for the target buffer is three UTF-8 chars per
* wide char ((_wcslen(wcs) * 3) + 1).
*
* The maximum space is needed only if the entire input string consists of
* UTF-16 words in range 0x0800-0xd7ff or 0xe000-0xffff (i.e. \u0800-\uffff
* modulo surrogate pairs), as per the following table:
*
* | | UTF-16 | UTF-8 |
* Code point | UTF-16 sequence | words | bytes | ratio
* --------------+-----------------------+--------+-------+-------
* 000000-00007f | 0000-007f | 1 | 1 | 1
* 000080-0007ff | 0080-07ff | 1 | 2 | 2
* 000800-00ffff | 0800-d7ff / e000-ffff | 1 | 3 | 3
* 010000-10ffff | d800-dbff + dc00-dfff | 2 | 4 | 2
*
* Note that invalid code points > 10ffff cannot be represented in UTF-16.
*
* Parameters:
* utf: target buffer
* wcs: wide string to convert
* utflen: size of target buffer
*
* Returns:
* length of converted string, or -1 on failure
*
* Errors:
* EINVAL: one of the input parameters is invalid (e.g. NULL)
* ERANGE: the output buffer is too small
*/
int xwcstoutf(char *utf, const wchar_t *wcs, size_t utflen);

_CRTIMP char* __cdecl __MINGW_NOTHROW 	_strdup (const char*) __MINGW_ATTRIB_MALLOC;
_CRTIMP char* __cdecl __MINGW_NOTHROW	strdup (const char*) __MINGW_ATTRIB_MALLOC;

__CRT_INLINE int __cdecl __MINGW_NOTHROW isascii(int c) {return ((c & ~0x7F) == 0);}
_CRTIMP char* __cdecl __MINGW_NOTHROW	_i64toa(__int64, char *, int);
size_t __int64_length(__int64 number, unsigned base);

 const char *inet_ntop(int af, const void *src, char *dst, socklen_t cnt);
