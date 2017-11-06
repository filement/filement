
#include <limits.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <stdbool.h>
#include <windows.h>
#include <winsock2.h>
#include <direct.h>
#include <wchar.h>
//#include <iconv.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include "mingw.h"

#undef poll
#undef socket
#undef connect
#undef accept
#undef shutdown
#undef getpeername
#undef sleep
#undef inet_aton
#undef gettimeofday
#undef stat

#include <ctype.h>

 
#include "log.h"
#if RUN_MODE > Debug
#undef assert
#define assert(arg)
#endif



 int pipe(int filedes[2])
{
	HANDLE h[2];

	/* this creates non-inheritable handles */
	if (!CreatePipe(&h[0], &h[1], NULL, 8192)) {
		errno = err_win_to_posix(GetLastError());
		return -1;
	}
	filedes[0] = _open_osfhandle((int)h[0], O_NOINHERIT);
	if (filedes[0] < 0) {
		CloseHandle(h[0]);
		CloseHandle(h[1]);
		return -1;
	}
	filedes[1] = _open_osfhandle((int)h[1], O_NOINHERIT);
	if (filedes[0] < 0) {
		close(filedes[0]);
		CloseHandle(h[1]);
		return -1;
	}
	
	 // unsigned long flags = 1;
	 // ioctlsocket(filedes[0], FIONBIO, &flags);
	 // flags=1;
	 // ioctlsocket(filedes[1], FIONBIO, &flags);
	return 0;
}


int
mingw_inet_aton(const char *cp, struct in_addr *addr)
{
    register unsigned int val;
    register int base, n;
    register char c;
    unsigned int parts[4];
    register unsigned int *pp = parts;

    assert(sizeof(val) == 4);

    c = *cp;
    while(1) {
        
        if(!isdigit(c))
            return (0);
        val = 0; base = 10;
        if(c == '0') {
            c = *++cp;
            if(c == 'x' || c == 'X')
                base = 16, c = *++cp;
            else
                base = 8;
        }
        while(1) {
            if(isascii(c) && isdigit(c)) {
                val = (val * base) + (c - '0');
                c = *++cp;
            } else if(base == 16 && isascii(c) && isxdigit(c)) {
                val = (val << 4) |
                    (c + 10 - (islower(c) ? 'a' : 'A'));
                c = *++cp;
            } else
                break;
        }
        if(c == '.') {
           
            if(pp >= parts + 3)
                return (0);
            *pp++ = val;
            c = *++cp;
        } else
            break;
    }
    
    if(c != '\0' && (!isascii(c) || !isspace(c)))
        return (0);
    
    n = pp - parts + 1;
    switch(n) {

    case 0:
        return (0);        /* initial nondigit */

    case 1:                /* a -- 32 bits */
        break;

    case 2:                /* a.b -- 8.24 bits */
        if((val > 0xffffff) || (parts[0] > 0xff))
            return (0);
        val |= parts[0] << 24;
        break;

    case 3:                /* a.b.c -- 8.8.16 bits */
        if((val > 0xffff) || (parts[0] > 0xff) || (parts[1] > 0xff))
            return (0);
        val |= (parts[0] << 24) | (parts[1] << 16);
        break;

    case 4:                /* a.b.c.d -- 8.8.8.8 bits */
        if((val > 0xff) || (parts[0] > 0xff) ||
           (parts[1] > 0xff) || (parts[2] > 0xff))
            return (0);
        val |= (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8);
        break;
    }
    if(addr)
        addr->s_addr = htonl(val);
    return (1);
}

unsigned int
mingw_sleep(unsigned int seconds)
{
    Sleep(seconds * 1000);
    return 0;
}

int
mingw_gettimeofday(struct timeval *tv, char *tz)
{
    const long long EPOCHFILETIME = (116444736000000000LL);
    FILETIME        ft;
    LARGE_INTEGER   li;
    long long        t;

    assert(tz == NULL);
    assert(sizeof(t) == 8);

    if(tv) {
        GetSystemTimeAsFileTime(&ft);
        li.LowPart  = ft.dwLowDateTime;
        li.HighPart = ft.dwHighDateTime;
        t  = li.QuadPart;       /* In 100-nanosecond intervals */
        t -= EPOCHFILETIME;     /* Offset to the Epoch time */
        t /= 10;                /* In microseconds */
        tv->tv_sec  = (long)(t / 1000000);
        tv->tv_usec = (long)(t % 1000000);
    }
    return 0;
}

int mingw_poll(struct pollfd *fds, unsigned int nfds, int timo)
{
    struct timeval timeout, *toptr;
    fd_set ifds, ofds, efds, *ip, *op;
    int i, rc;

    /* Set up the file-descriptor sets in ifds, ofds and efds. */
    FD_ZERO(&ifds);
    FD_ZERO(&ofds);
    FD_ZERO(&efds);
    for (i = 0, op = ip = 0; i < nfds; ++i) {
	fds[i].revents = 0;
	if((int)fds[i].fd<0)continue;
	if(fds[i].events & (POLLIN|POLLPRI)) {
		ip = &ifds;
		FD_SET(fds[i].fd, ip);
	}
	if(fds[i].events & POLLOUT) {
		op = &ofds;
		FD_SET(fds[i].fd, op);
	}
	FD_SET(fds[i].fd, &efds);
    } 

    /* Set up the timeval structure for the timeout parameter */
    if(timo < 0) {
	toptr = 0;
    } else {
	toptr = &timeout;
	timeout.tv_sec = timo / 1000;
	timeout.tv_usec = (timo - timeout.tv_sec * 1000) * 1000;
    }

#ifdef DEBUG_POLL
    printf("Entering select() sec=%ld usec=%ld ip=%lx op=%lx\n",
           (long)timeout.tv_sec, (long)timeout.tv_usec, (long)ip, (long)op);
#endif
    rc = select(nfds+1, ip, op, &efds, toptr);
#ifdef DEBUG_POLL
    printf("Exiting select rc=%d\n", rc);
#endif

    if(rc <= 0)
	{
	int err=WSAGetLastError();
	return rc;
	}

    if(rc > 0) {
        for (i = 0; i < nfds; ++i) {
		if((int)fds[i].fd<0)continue;
            int fd = fds[i].fd;
    	if(fds[i].events & (POLLIN|POLLPRI) && FD_ISSET(fd, &ifds))
    		fds[i].revents |= POLLIN;
    	if(fds[i].events & POLLOUT && FD_ISSET(fd, &ofds))
    		fds[i].revents |= POLLOUT;
    	if(FD_ISSET(fd, &efds))
    		/* Some error was detected ... should be some way to know. */
    		fds[i].revents |= POLLHUP;
#ifdef DEBUG_POLL
        printf("%d %d %d revent = %x\n", 
                FD_ISSET(fd, &ifds), FD_ISSET(fd, &ofds), FD_ISSET(fd, &efds), 
                fds[i].revents
        );
#endif
        }
    }
    return rc;
}

int mingw_close_socket(SOCKET fd) {
    int rc;
	if(fd<0)return 0;
	//printf("CLOSED %d\n",fd);
	
	if((int)fd<0)return 0;
    rc = closesocket(fd);
#if RUN_MODE <= Debug
	if(rc)
	{
	//int test=_get_osfhandle(fd);
	fprintf(stderr,"The socket error is %d %d\n",WSAGetLastError(),fd);
	assert(rc == 0);
	}
#endif
    
    return 0;
}

static void
set_errno(int winsock_err)
{
    switch(winsock_err) {
        case WSAEWOULDBLOCK:
            errno = EAGAIN;
            break;
        default:
            errno = winsock_err;
            break;
    }
}

size_t strnlen(const char *buf,size_t len)
{
size_t clen=0;
	for(;*(buf+clen)!='\0' && clen<len;clen++);
	
return clen;
}

int mingw_write_socket(SOCKET fd,const void *buf, int n)
{
    int rc = send(fd, buf, n, 0);
    if(rc == SOCKET_ERROR) {
        set_errno(WSAGetLastError());
    }
    return rc;
}

int mingw_read_socket(SOCKET fd, void *buf, int n)
{
    int rc = recv(fd, buf, n, 0);
    if(rc == SOCKET_ERROR) {
		//printf("WSAGetLastError() %d %d\n",WSAGetLastError(),SOCKET_ERROR);
        set_errno(WSAGetLastError());
    }
    return rc;
}



int
mingw_setnonblocking(SOCKET fd, int nonblocking)
{
    int rc;

    unsigned long mode = 1;
    rc = ioctlsocket(fd, FIONBIO, &mode);
    if(rc != 0) {
        set_errno(WSAGetLastError());
    }
    return (rc == 0 ? 0 : -1);
}


SOCKET
mingw_socket(int domain, int type, int protocol)
{
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0) return -1;

    SOCKET fd = socket(domain, type, protocol);
    if(fd == INVALID_SOCKET) {
        set_errno(WSAGetLastError());
		return -1;
    }
    return fd;
}

/*
static void
set_connect_errno(int winsock_err)
{
    switch(winsock_err) {
        case WSAEINVAL:
        case WSAEALREADY:
        case WSAEWOULDBLOCK:
            errno = EINPROGRESS;
            break;
        default:
            errno = winsock_err;
            break;
    }
}
*/

/*
int
mingw_connect(SOCKET fd, struct sockaddr *addr, socklen_t addr_len)
{
    int rc = connect(fd, addr, addr_len);
    assert(rc == 0 || rc == SOCKET_ERROR);
    if(rc == SOCKET_ERROR) {
        set_connect_errno(WSAGetLastError());
    }
    return rc;
}
*/

SOCKET
mingw_accept(SOCKET fd, struct sockaddr *addr, socklen_t *addr_len)
{
    SOCKET newfd = accept(fd, addr, addr_len);
    if(newfd == INVALID_SOCKET) {
        set_errno(WSAGetLastError());
        newfd = -1;
    }
    return newfd;
}


int
mingw_shutdown(SOCKET fd, int mode)
{
    int rc = shutdown(fd, mode);
    assert(rc == 0 || rc == SOCKET_ERROR);
    if(rc == SOCKET_ERROR) {
        set_errno(WSAGetLastError());
    }
    return rc;
}


int
mingw_getpeername(SOCKET fd, struct sockaddr *name, socklen_t *namelen)
{
    int rc = getpeername(fd, name, namelen);
    assert(rc == 0 || rc == SOCKET_ERROR);
    if(rc == SOCKET_ERROR) {
        set_errno(WSAGetLastError());
    }
    return rc;
}


/*
int
mingw_stat(const char *filename, struct stat *ss)
{
    int len, rc, saved_errno;
    char *noslash;

    len = strlen(filename);
    if(len <= 1 || filename[len - 1] != '/')
        return stat(filename, ss);

    noslash = malloc(len);
    if(noslash == NULL)
        return -1;

    memcpy(noslash, filename, len - 1);
    noslash[len - 1] = '\0';

    rc = stat(noslash, ss);
    saved_errno = errno;
    free(noslash);
    errno = saved_errno;
    return rc;
}
*/

static inline int file_attr_to_st_mode (DWORD attr)
{
int fMode = S_IREAD;
if (attr & FILE_ATTRIBUTE_DIRECTORY)
{
fMode |= S_IEXEC;
fMode |= S_IFDIR;
}
//else if (!(attr & FILE_ATTRIBUTE_HIDDEN))
else fMode |= S_IFREG;

if (!(attr & FILE_ATTRIBUTE_READONLY))
fMode |= S_IWRITE;
return fMode;
}

static inline int get_file_attr(const char *fname, WIN32_FILE_ATTRIBUTE_DATA *fdata)
{
if (GetFileAttributesExA(fname, GetFileExInfoStandard, fdata))
return 0;

switch (GetLastError()) {
case ERROR_ACCESS_DENIED:
case ERROR_SHARING_VIOLATION:
case ERROR_LOCK_VIOLATION:
case ERROR_SHARING_BUFFER_EXCEEDED:
return EACCES;
case ERROR_BUFFER_OVERFLOW:
return ENAMETOOLONG;
case ERROR_NOT_ENOUGH_MEMORY:
return ENOMEM;
default:
return ENOENT;
}
}
/*
bool mmap_readall(int file, char *restrict buffer, size_t total)
{
	size_t index;
	ssize_t size;
	for(index = 0; index < total; index += size)
	{
		size = read(file, buffer + index, total - index);
		if (size <= 0) return false;
	}
	return true;
}

void *mmap (void *addr, size_t len, int prot, int flags, int fd, long long offset) {

void *buf;
ssize_t count;

if ( addr || fd == -1 || (prot & PROT_WRITE))
{
errno = EINVAL; // TODO: this is not right...
	return MAP_FAILED;
}

buf = malloc(len);
if ( NULL == buf )
{
	errno = ENOMEM;
	return MAP_FAILED;
}

/ *
if (lseek(fd,offset,SEEK_SET) != offset)
{
	errno = EINVAL;
	return MAP_FAILED;
}
* /

if (!mmap_readall(fd, buf, len)) {
free (buf);
errno = EINVAL; // TODO: this is not right...
return MAP_FAILED;
}

return buf;
}
*/

int munmap (void *addr, size_t len) {
free (addr);
return 0;
}

static bool WRITEALL(int file, char *buffer, size_t total)
{
	size_t written;
	ssize_t size;
	for(written = 0; written < total; written += size)
	{
		size = WRITE(file, buffer + written, total - written);
		if (size < 0) return false;
	}
	return true;
}

int writev(int fd, const struct iovec *vector, int count)
{
	size_t i;
	int total = 0;
	for(i = 0; i < count; i++) {
		if (!WRITEALL(fd, vector[i].iov_base, vector[i].iov_len)) return -1;
		total += vector[i].iov_len;
	}
	return total;
}

/*
int writev(int fd, const struct iovec *vector, int count)
{
    int rc;                     // Return Code 
    if(count == 1) {
        rc = WRITE(fd, vector->iov_base, vector->iov_len);
    } else {
        int n = 0;              // Total bytes to write 
        char *buf = 0;          // Buffer to copy to before writing 
        int i;                 // Counter var for looping over vector[] 
        int offset = 0;        //Offset for copying to buf 

        // Figure out the required buffer size 
        for(i = 0; i < count; i++) {
            n += vector[i].iov_len;
        }

        // Allocate the buffer. If the allocation fails, bail out 
        buf = malloc(n);
        if(!buf) {
            errno = ENOMEM;
            return -1;
        }

        //Copy the contents of the vector array to the buffer 
        for(i = 0; i < count; i++) {
            memcpy(&buf[offset], vector[i].iov_base, vector[i].iov_len);
            offset += vector[i].iov_len;
        }
        assert(offset == n);

        // Write the entire buffer to the socket and free the allocation 
        rc = WRITE(fd, buf, n);
        free(buf);
    }
    return rc;
}
*/


int readv(int fd, const struct iovec *vector, int count)
{
    int ret = 0;                     /* Return value */
    int i;
    for(i = 0; i < count; i++) {
        int n = vector[i].iov_len;
        int rc = READ(fd, vector[i].iov_base, n);
        if(rc == n) {
            ret += rc;
        } else {
            if(rc < 0) {
                ret = (ret == 0 ? rc : ret);
            } else {
                ret += rc;
            }
            break;
        }
    }
    return ret;
}

//GIT implementation

int err_win_to_posix(DWORD winerr)
{
int error = ENOSYS;
switch(winerr) {
case ERROR_ACCESS_DENIED: error = EACCES; break;
case ERROR_ACCOUNT_DISABLED: error = EACCES; break;
case ERROR_ACCOUNT_RESTRICTION: error = EACCES; break;
case ERROR_ALREADY_ASSIGNED: error = EBUSY; break;
case ERROR_ALREADY_EXISTS: error = EEXIST; break;
case ERROR_ARITHMETIC_OVERFLOW: error = ERANGE; break;
case ERROR_BAD_COMMAND: error = EIO; break;
case ERROR_BAD_DEVICE: error = ENODEV; break;
case ERROR_BAD_DRIVER_LEVEL: error = ENXIO; break;
case ERROR_BAD_EXE_FORMAT: error = ENOEXEC; break;
case ERROR_BAD_FORMAT: error = ENOEXEC; break;
case ERROR_BAD_LENGTH: error = EINVAL; break;
case ERROR_BAD_PATHNAME: error = ENOENT; break;
case ERROR_BAD_PIPE: error = EPIPE; break;
case ERROR_BAD_UNIT: error = ENODEV; break;
case ERROR_BAD_USERNAME: error = EINVAL; break;
case ERROR_BROKEN_PIPE: error = EPIPE; break;
case ERROR_BUFFER_OVERFLOW: error = ENAMETOOLONG; break;
case ERROR_BUSY: error = EBUSY; break;
case ERROR_BUSY_DRIVE: error = EBUSY; break;
case ERROR_CALL_NOT_IMPLEMENTED: error = ENOSYS; break;
case ERROR_CANNOT_MAKE: error = EACCES; break;
case ERROR_CANTOPEN: error = EIO; break;
case ERROR_CANTREAD: error = EIO; break;
case ERROR_CANTWRITE: error = EIO; break;
case ERROR_CRC: error = EIO; break;
case ERROR_CURRENT_DIRECTORY: error = EACCES; break;
case ERROR_DEVICE_IN_USE: error = EBUSY; break;
case ERROR_DEV_NOT_EXIST: error = ENODEV; break;
case ERROR_DIRECTORY: error = EINVAL; break;
case ERROR_DIR_NOT_EMPTY: error = ENOTEMPTY; break;
case ERROR_DISK_CHANGE: error = EIO; break;
case ERROR_DISK_FULL: error = ENOSPC; break;
case ERROR_DRIVE_LOCKED: error = EBUSY; break;
case ERROR_ENVVAR_NOT_FOUND: error = EINVAL; break;
case ERROR_EXE_MARKED_INVALID: error = ENOEXEC; break;
case ERROR_FILENAME_EXCED_RANGE: error = ENAMETOOLONG; break;
case ERROR_FILE_EXISTS: error = EEXIST; break;
case ERROR_FILE_INVALID: error = ENODEV; break;
case ERROR_FILE_NOT_FOUND: error = ENOENT; break;
case ERROR_GEN_FAILURE: error = EIO; break;
case ERROR_HANDLE_DISK_FULL: error = ENOSPC; break;
case ERROR_INSUFFICIENT_BUFFER: error = ENOMEM; break;
case ERROR_INVALID_ACCESS: error = EACCES; break;
case ERROR_INVALID_ADDRESS: error = EFAULT; break;
case ERROR_INVALID_BLOCK: error = EFAULT; break;
case ERROR_INVALID_DATA: error = EINVAL; break;
case ERROR_INVALID_DRIVE: error = ENODEV; break;
case ERROR_INVALID_EXE_SIGNATURE: error = ENOEXEC; break;
case ERROR_INVALID_FLAGS: error = EINVAL; break;
case ERROR_INVALID_FUNCTION: error = ENOSYS; break;
case ERROR_INVALID_HANDLE: error = EBADF; break;
case ERROR_INVALID_LOGON_HOURS: error = EACCES; break;
case ERROR_INVALID_NAME: error = EINVAL; break;
case ERROR_INVALID_OWNER: error = EINVAL; break;
case ERROR_INVALID_PARAMETER: error = EINVAL; break;
case ERROR_INVALID_PASSWORD: error = EPERM; break;
case ERROR_INVALID_PRIMARY_GROUP: error = EINVAL; break;
case ERROR_INVALID_SIGNAL_NUMBER: error = EINVAL; break;
case ERROR_INVALID_TARGET_HANDLE: error = EIO; break;
case ERROR_INVALID_WORKSTATION: error = EACCES; break;
case ERROR_IO_DEVICE: error = EIO; break;
case ERROR_IO_INCOMPLETE: error = EINTR; break;
case ERROR_LOCKED: error = EBUSY; break;
case ERROR_LOCK_VIOLATION: error = EACCES; break;
case ERROR_LOGON_FAILURE: error = EACCES; break;
case ERROR_MAPPED_ALIGNMENT: error = EINVAL; break;
case ERROR_META_EXPANSION_TOO_LONG: error = E2BIG; break;
case ERROR_MORE_DATA: error = EPIPE; break;
case ERROR_NEGATIVE_SEEK: error = ESPIPE; break;
case ERROR_NOACCESS: error = EFAULT; break;
case ERROR_NONE_MAPPED: error = EINVAL; break;
case ERROR_NOT_ENOUGH_MEMORY: error = ENOMEM; break;
case ERROR_NOT_READY: error = EAGAIN; break;
case ERROR_NOT_SAME_DEVICE: error = EXDEV; break;
case ERROR_NO_DATA: error = EPIPE; break;
case ERROR_NO_MORE_SEARCH_HANDLES: error = EIO; break;
case ERROR_NO_PROC_SLOTS: error = EAGAIN; break;
case ERROR_NO_SUCH_PRIVILEGE: error = EACCES; break;
case ERROR_OPEN_FAILED: error = EIO; break;
case ERROR_OPEN_FILES: error = EBUSY; break;
case ERROR_OPERATION_ABORTED: error = EINTR; break;
case ERROR_OUTOFMEMORY: error = ENOMEM; break;
case ERROR_PASSWORD_EXPIRED: error = EACCES; break;
case ERROR_PATH_BUSY: error = EBUSY; break;
case ERROR_PATH_NOT_FOUND: error = ENOENT; break;
case ERROR_PIPE_BUSY: error = EBUSY; break;
case ERROR_PIPE_CONNECTED: error = EPIPE; break;
case ERROR_PIPE_LISTENING: error = EPIPE; break;
case ERROR_PIPE_NOT_CONNECTED: error = EPIPE; break;
case ERROR_PRIVILEGE_NOT_HELD: error = EACCES; break;
case ERROR_READ_FAULT: error = EIO; break;
case ERROR_SEEK: error = EIO; break;
case ERROR_SEEK_ON_DEVICE: error = ESPIPE; break;
case ERROR_SHARING_BUFFER_EXCEEDED: error = ENFILE; break;
case ERROR_SHARING_VIOLATION: error = EACCES; break;
case ERROR_STACK_OVERFLOW: error = ENOMEM; break;
case ERROR_SWAPERROR: error = ENOENT; break;
case ERROR_TOO_MANY_MODULES: error = EMFILE; break;
case ERROR_TOO_MANY_OPEN_FILES: error = EMFILE; break;
case ERROR_UNRECOGNIZED_MEDIA: error = ENXIO; break;
case ERROR_UNRECOGNIZED_VOLUME: error = ENODEV; break;
case ERROR_WAIT_NO_CHILDREN: error = ECHILD; break;
case ERROR_WRITE_FAULT: error = EIO; break;
case ERROR_WRITE_PROTECT: error = EROFS; break;
}
return error;
}

int xutftowcsn(wchar_t *wcs, const char *utfs, size_t wcslen, int utflen)
{
int upos = 0, wpos = 0;
const unsigned char *utf = (const unsigned char*) utfs;
if (!utf || !wcs || wcslen < 1) {
errno = EINVAL;
return -1;
}
/* reserve space for \0 */
wcslen--;
if (utflen < 0)
utflen = INT_MAX;

while (upos < utflen) {
int c = utf[upos++] & 0xff;
if (utflen == INT_MAX && c == 0)
break;

if (wpos >= wcslen) {
wcs[wpos] = 0;
errno = ERANGE;
return -1;
}

if (c < 0x80) {
/* ASCII */
wcs[wpos++] = c;
} else if (c >= 0xc2 && c < 0xe0 && upos < utflen &&
(utf[upos] & 0xc0) == 0x80) {
/* 2-byte utf-8 */
c = ((c & 0x1f) << 6);
c |= (utf[upos++] & 0x3f);
wcs[wpos++] = c;
} else if (c >= 0xe0 && c < 0xf0 && upos + 1 < utflen &&
!(c == 0xe0 && utf[upos] < 0xa0) && /* over-long encoding */
(utf[upos] & 0xc0) == 0x80 &&
(utf[upos + 1] & 0xc0) == 0x80) {
/* 3-byte utf-8 */
c = ((c & 0x0f) << 12);
c |= ((utf[upos++] & 0x3f) << 6);
c |= (utf[upos++] & 0x3f);
wcs[wpos++] = c;
} else if (c >= 0xf0 && c < 0xf5 && upos + 2 < utflen &&
wpos + 1 < wcslen &&
!(c == 0xf0 && utf[upos] < 0x90) && /* over-long encoding */
!(c == 0xf4 && utf[upos] >= 0x90) && /* > \u10ffff */
(utf[upos] & 0xc0) == 0x80 &&
(utf[upos + 1] & 0xc0) == 0x80 &&
(utf[upos + 2] & 0xc0) == 0x80) {
/* 4-byte utf-8: convert to \ud8xx \udcxx surrogate pair */
c = ((c & 0x07) << 18);
c |= ((utf[upos++] & 0x3f) << 12);
c |= ((utf[upos++] & 0x3f) << 6);
c |= (utf[upos++] & 0x3f);
c -= 0x10000;
wcs[wpos++] = 0xd800 | (c >> 10);
wcs[wpos++] = 0xdc00 | (c & 0x3ff);
} else if (c >= 0xa0) {
/* invalid utf-8 byte, printable unicode char: convert 1:1 */
wcs[wpos++] = c;
} else {
/* invalid utf-8 byte, non-printable unicode: convert to hex */
static const char *hex = "0123456789abcdef";
wcs[wpos++] = hex[c >> 4];
if (wpos < wcslen)
wcs[wpos++] = hex[c & 0x0f];
}
}
wcs[wpos] = 0;
return wpos;
}

int xwcstoutf(char *utf, const wchar_t *wcs, size_t utflen)
{
if (!wcs || !utf || utflen < 1) {
errno = EINVAL;
return -1;
}
utflen = WideCharToMultiByte(CP_UTF8, 0, wcs, -1, utf, utflen, NULL, NULL);
if (utflen)
return utflen - 1;
errno = ERANGE;
return -1;
}

static inline void finddata2dirent(struct dirent *ent, WIN32_FIND_DATAW *fdata)
{
/* convert UTF-16 name to UTF-8 */
xwcstoutf(ent->d_name, fdata->cFileName, sizeof(ent->d_name));

/* Set file type, based on WIN32_FIND_DATA */
if (fdata->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
ent->d_type = DT_DIR;
else 
ent->d_type = DT_REG;
}

struct tm *
mingw_gmtime_r(const time_t *tp, struct tm *result)
{
  struct tm *t = gmtime(tp);
  if (t) *result = *t;
  return t;
} 

DIR *mingw_opendir(const char *name)
{
wchar_t pattern[MAX_PATH + 2]; /* + 2 for '/' '*' */
WIN32_FIND_DATAW fdata;
HANDLE h;
int len;
DIR *dir;

/* convert name to UTF-16 and check length < MAX_PATH */
if ((len = xutftowcs_path(pattern, name)) < 0)
return NULL;

/* append optional '/' and wildcard '*' */
if (len && !is_dir_sep(pattern[len - 1]))
pattern[len++] = '/';
pattern[len++] = '*';
pattern[len] = 0;

/* open find handle */
h = FindFirstFileW(pattern, &fdata);
if (h == INVALID_HANDLE_VALUE) {
DWORD err = GetLastError();
errno = (err == ERROR_DIRECTORY) ? ENOTDIR : err_win_to_posix(err);
return NULL;
}

/* initialize DIR structure and copy first dir entry */
dir = malloc(sizeof(DIR));
dir->dd_handle = h;
dir->dd_stat = 0;
finddata2dirent(&dir->dd_dir, &fdata);
return dir;
}

struct dirent *mingw_readdir(DIR *dir)
{
if (!dir) {
errno = EBADF; /* No set_errno for mingw */
return NULL;
}

/* if first entry, dirent has already been set up by opendir */
if (dir->dd_stat) {
/* get next entry and convert from WIN32_FIND_DATA to dirent */
WIN32_FIND_DATAW fdata;
if (FindNextFileW(dir->dd_handle, &fdata)) {
finddata2dirent(&dir->dd_dir, &fdata);
} else {
DWORD lasterr = GetLastError();
/* POSIX says you shouldn't set errno when readdir can't
find any more files; so, if another error we leave it set. */
if (lasterr != ERROR_NO_MORE_FILES)
errno = err_win_to_posix(lasterr);
return NULL;
}
}

++dir->dd_stat;
return &dir->dd_dir;
}

int mingw_closedir(DIR *dir)
{
if (!dir) {
errno = EBADF;
return -1;
}

FindClose(dir->dd_handle);
free(dir);
return 0;
}


/*
* The unit of FILETIME is 100-nanoseconds since January 1, 1601, UTC.
* Returns the 100-nanoseconds ("hekto nanoseconds") since the epoch.
*/
static inline long long filetime_to_hnsec(const FILETIME *ft)
{
long long winTime = ((long long)ft->dwHighDateTime << 32) + ft->dwLowDateTime;
/* Windows to Unix Epoch conversion */
return winTime - 116444736000000000LL;
}

static inline time_t filetime_to_time_t(const FILETIME *ft)
{
time_t time = filetime_to_hnsec(ft) / 10000000;
if(time<0)return 0;

return time;
}

static inline int readlink(const char *path, char *buf, size_t bufsiz)
{ errno = ENOSYS; return -1; }

/* We keep the do_lstat code in a separate function to avoid recursion.
* When a path ends with a slash, the stat will fail with ENOENT. In
* this case, we strip the trailing slashes and stat again.
*
* If follow is true then act like stat() and report on the link
* target. Otherwise report on the link itself.
*/
static int do_lstat(int follow, const char *file_name, struct _stati64 *buf)
{
WIN32_FILE_ATTRIBUTE_DATA fdata;
wchar_t wfilename[MAX_PATH];
if (xutftowcs_path(wfilename, file_name) < 0)
return -1;

if (GetFileAttributesExW(wfilename, GetFileExInfoStandard, &fdata)) {
buf->st_ino = 0;
buf->st_gid = 0;
buf->st_uid = 0;
buf->st_nlink = 1;
buf->st_mode = file_attr_to_st_mode(fdata.dwFileAttributes);
buf->st_size = fdata.nFileSizeLow | (((int64_t)fdata.nFileSizeHigh)<<32);
buf->st_dev = buf->st_rdev = 0; /* not used by Git */
buf->st_atime = filetime_to_time_t(&(fdata.ftLastAccessTime));
buf->st_mtime = filetime_to_time_t(&(fdata.ftLastWriteTime));
buf->st_ctime = filetime_to_time_t(&(fdata.ftCreationTime));
if (fdata.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
WIN32_FIND_DATAW findbuf;
HANDLE handle = FindFirstFileW(wfilename, &findbuf);
if (handle != INVALID_HANDLE_VALUE) {
if ((findbuf.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
(findbuf.dwReserved0 == IO_REPARSE_TAG_SYMLINK)) {
if (follow) {
buf->st_size = 0;
//link
//char buffer[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
//buf->st_size = readlink(file_name, buffer, MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
} else {
buf->st_mode = S_IFLNK;
}
buf->st_mode |= S_IREAD;
if (!(findbuf.dwFileAttributes & FILE_ATTRIBUTE_READONLY))
buf->st_mode |= S_IWRITE;
}
FindClose(handle);
}
}
return 0;
}
switch (GetLastError()) {
case ERROR_ACCESS_DENIED:
case ERROR_SHARING_VIOLATION:
case ERROR_LOCK_VIOLATION:
case ERROR_SHARING_BUFFER_EXCEEDED:
errno = EACCES;
break;
case ERROR_BUFFER_OVERFLOW:
errno = ENAMETOOLONG;
break;
case ERROR_NOT_ENOUGH_MEMORY:
errno = ENOMEM;
break;
default:
errno = ENOENT;
break;
}
return -1;
}

int mingw_fstat(int fd, struct _stati64 *buf)
{
HANDLE fh = (HANDLE)_get_osfhandle(fd);
BY_HANDLE_FILE_INFORMATION fdata;

if (fh == INVALID_HANDLE_VALUE) {
errno = EBADF;
return -1;
}
/* direct non-file handles to MS's fstat() */
if (GetFileType(fh) != FILE_TYPE_DISK)
return _fstati64(fd, buf);

if (GetFileInformationByHandle(fh, &fdata)) {
buf->st_ino = 0;
buf->st_gid = 0;
buf->st_uid = 0;
buf->st_nlink = 1;
buf->st_mode = file_attr_to_st_mode(fdata.dwFileAttributes);
buf->st_size = fdata.nFileSizeLow | (((int64_t)fdata.nFileSizeHigh)<<32);
buf->st_dev = buf->st_rdev = 0; /* not used by Git */
buf->st_atime = filetime_to_time_t(&(fdata.ftLastAccessTime));
buf->st_mtime = filetime_to_time_t(&(fdata.ftLastWriteTime));
buf->st_ctime = filetime_to_time_t(&(fdata.ftCreationTime));
return 0;
}
errno = EBADF;
return -1;
}

/* We provide our own lstat/fstat functions, since the provided
* lstat/fstat functions are so slow. These stat functions are
* tailored for Git's usage (read: fast), and are not meant to be
* complete. Note that Git stat()s are redirected to mingw_lstat()
* too, since Windows doesn't really handle symlinks that well.
*/
static int do_stat_internal(int follow, const char *file_name, struct _stati64 *buf)
{
int namelen;
char alt_name[PATH_MAX];

if (!do_lstat(follow, file_name, buf))
return 0;

/* if file_name ended in a '/', Windows returned ENOENT;
* try again without trailing slashes
*/
if (errno != ENOENT)
return -1;

namelen = strlen(file_name);
if (namelen && file_name[namelen-1] != '/')
return -1;
while (namelen && file_name[namelen-1] == '/')
--namelen;
if (!namelen || namelen >= PATH_MAX)
return -1;

memcpy(alt_name, file_name, namelen);
alt_name[namelen] = 0;
return do_lstat(follow, alt_name, buf);
}

int mingw_lstat(const char *file_name, struct _stati64 *buf)
{
return do_stat_internal(0, file_name, buf);
}
int mingw_stat(const char *file_name, struct _stati64 *buf)
{
return do_stat_internal(1, file_name, buf);
}


//TODO unicode
/*
static char *recode(char *data, size_t size)
{
    iconv_t icdsc;
    char *outbuf;

    
        if ((icdsc = iconv_open("char", "UTF-8")) == (iconv_t) (-1)) {
            return 0;
        } 

    {
        size_t osize = size;
        size_t ileft = size;
        size_t oleft = size - 1;
        char *ip;
        char *op;
        size_t rc;
        int clear = 0;

        outbuf = malloc(osize);
        ip = data;
        op = outbuf;

        while (1) {
            if (ileft)
                rc = iconv(icdsc, &ip, &ileft, &op, &oleft);
            else {              // clear the conversion state and leave
                clear = 1;
                rc = iconv(icdsc, NULL, NULL, &op, &oleft);
            }
            if (rc == (size_t) (-1)) {
                if (errno == E2BIG) {
                    size_t offset = op - outbuf;
                    outbuf = (char *) realloc(outbuf, osize + size);
                    op = outbuf + offset;
                    osize += size;
                    oleft += size;
                } else {
                    return NULL;
                }
            } else if (clear)
                break;
        }
        outbuf[osize - oleft - 1] = 0;
    }

    if (icdsc != (iconv_t) (-1)) {
        (void) iconv_close(icdsc);
        icdsc = (iconv_t) (-1);
    }

    return outbuf;
}
*/

int mingw_mkdir(const char *path, int mode)
{
int ret;
char *mkdirpath=(char *)path;
/*
	int inbytes=strlen(path);
	char *fname_buf=malloc(inbytes*sizeof(char)+1);
	fname_buf=memcpy(fname_buf,path,inbytes+1);
char *mkdirpath=recode(fname_buf,strlen(path));
	free(fname_buf);
*/
wchar_t wpath[MAX_PATH];
if (xutftowcs_path(wpath, mkdirpath) < 0)
return -1;
ret = _wmkdir(wpath);
return ret;
}

int mingw_open (const char *filename, int oflags, ...)
{
va_list args;
unsigned mode;
int fd;
wchar_t wfilename[MAX_PATH];

va_start(args, oflags);
mode = va_arg(args, int);
va_end(args);
char *filetoopen=0;
/*
char *fname_buf=0;
size_t inbytes;
iconv_t iconvctx;
*/

if(!filename)return -1;

if(oflags & O_CREAT) 
{
/*
	inbytes=strlen(filename);
	fname_buf=malloc(inbytes*sizeof(char)+1);
	fname_buf=memcpy(fname_buf,filename,inbytes+1);
	filetoopen=recode(fname_buf,strlen(filename));
	free(fname_buf);
*/
filetoopen=(char *)filename;
}
else
{
filetoopen=(char *)filename;
}

if (filetoopen && !strcmp(filetoopen, "/dev/null"))
filetoopen = "nul";

if (xutftowcs_path(wfilename, filetoopen) < 0)
return -1;
fd = _wopen(wfilename, oflags, mode);

if (fd < 0 && (oflags & O_CREAT) && errno == EACCES) {
DWORD attrs = GetFileAttributesW(wfilename);
if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
errno = EISDIR;
}

return fd;
}

static inline int is_file_in_use_error(DWORD errcode)
{
switch (errcode) {
case ERROR_SHARING_VIOLATION:
case ERROR_ACCESS_DENIED:
return 1;
}

return 0;
}

int mingw_remove(const char *pathname)
{
int ret;
wchar_t wpathname[MAX_PATH];
if (xutftowcs_path(wpathname, pathname) < 0)
return -1;

/* read-only files cannot be removed */
_wchmod(wpathname, 0666);
if ((ret = _wunlink(wpathname)) == -1 ) {
if (is_file_in_use_error(GetLastError()))errno = EBUSY;

errno = err_win_to_posix(GetLastError());
}

return ret;
}

static BOOL IsDots(const wchar_t* str) {
    if(wcscmp(str,L".") && wcscmp(str,L"..")) return FALSE;
    return TRUE;
}
 
 
 
BOOL DeleteDirectory_rec(wchar_t* wpathname) {

    HANDLE hFind; // file handle
    WIN32_FIND_DATAW FindFileData;
     
    wchar_t DirPath[MAX_PATH];
    wchar_t FileName[MAX_PATH];
     
    wcscpy(DirPath,wpathname);
    wcscat(DirPath,L"\\*"); // searching all files
    wcscpy(FileName,wpathname);
    wcscat(FileName,L"\\");
     
    // find the first file
    hFind = FindFirstFileW(DirPath,&FindFileData);
    if(hFind == INVALID_HANDLE_VALUE) return FALSE;
    wcscpy(DirPath,FileName);
     
    bool bSearch = true;
    while(bSearch) { // until we find an entry
    if(FindNextFileW(hFind,&FindFileData)) {
    if(IsDots(FindFileData.cFileName)) continue;
    wcscat(FileName,FindFileData.cFileName);
    if((FindFileData.dwFileAttributes &
    FILE_ATTRIBUTE_DIRECTORY)) {
     
    // we have found a directory, recurse
    if(!DeleteDirectory_rec(FileName)) {
    FindClose(hFind);
    return FALSE; // directory couldn't be deleted
    }
    // remove the empty directory
    RemoveDirectoryW(FileName);
    wcscpy(FileName,DirPath);
    }
    else {
    if(FindFileData.dwFileAttributes &
    FILE_ATTRIBUTE_READONLY)
    // change read-only file mode
    _wchmod(FileName, _S_IWRITE);
    if(!DeleteFileW(FileName)) { // delete the file
    FindClose(hFind);
    return FALSE;
    }
    wcscpy(FileName,DirPath);
    }
    }
    else {
    // no more files there
    if(GetLastError() == ERROR_NO_MORE_FILES)
    bSearch = false;
    else {
    // some error occurred; close the handle and return FALSE
    FindClose(hFind);
    return FALSE;
    }
     
    }
     
    }
    FindClose(hFind); // close the file handle
     
    return RemoveDirectoryW(wpathname); // remove the empty directory
     
}

BOOL DeleteDirectory(const char* pathname) {
 wchar_t wpathname[MAX_PATH];
if (xutftowcs_path(wpathname, pathname) < 0)
return -1;
	
	return DeleteDirectory_rec(wpathname);
	
 }
 
