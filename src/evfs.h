#include "buffer.h"

#define EVFS_REGULAR				1
#define EVFS_DIRECTORY				2

#define EVFS_ENCODING_IDENTITY		0
#define EVFS_ENCODING_DEFLATED		1
//#define EVFS_ENCODING_BZIP2		2
#define EVFS_ENCODING_FD			3 /* TODO this is not an encoding */

#define EVFS_READ					0x1
#define EVFS_APPEND					0x2
#define EVFS_MODIFY					0x4

#define EVFS_STRICT					0x1
#define EVFS_EXTRACT				0x2
#define EVFS_NESTED					0x4

// TODO find a way to simplify this
#define EVFS_IN(f, d) (((d).length <= (f).length) && (!(d).length || !memcmp((f).data, (d).data, (d).length)) && (((d).length == (f).length) || ((f).data[(d).length] == '/') || !(d).length))

struct file
{
	struct string name;
#if !defined(OS_WINDOWS)
	off_t size;
#else
	int64_t size;
#endif
	time_t mtime;
	unsigned char type;
	unsigned char access;

	struct string _content;
	unsigned _encoding;
	uint32_t _crc;				// checksum for encoded files
	int _fd;

#if defined(OS_WINDOWS)
	struct
	{
		int fd;	
		uint64_t length;
		int64_t offset;
	} content;
#endif
};

typedef int (*evfs_callback_t)(const struct file *restrict, void *);

#if !defined(OS_WINDOWS)
typedef int (*evfs_protocol_t)(const struct string *restrict, unsigned, const struct string *restrict, struct buffer *restrict, evfs_callback_t, void *, unsigned);
#else
typedef int (*evfs_protocol_t)(const struct string *restrict, struct string *restrict, struct buffer *restrict, evfs_callback_t, void *, unsigned, int);
#endif

evfs_protocol_t evfs_recognize(struct file *restrict file);

bool buffer_adjust(struct buffer *restrict buffer, size_t size);

bool path_malicious(const struct string *restrict filename);

int evfs_browse(struct string *restrict location, unsigned depth, evfs_callback_t callback, void *argument, unsigned flags);
int evfs_file(const struct file *restrict file, int callback(void *, unsigned char *, unsigned), void *argument);

#if !defined(OS_WINDOWS)
int evfs_extract(const struct string *restrict path, const struct file *restrict file, struct string *restrict archive);
#else
int evfs_extract(const struct string *restrict path, const struct file *restrict file, int *restrict archive);
bool evfs_inflate_windows(int fd, int64_t offset, size_t size,int (*callback)(void *, unsigned char *, unsigned), void *arg);
#endif

// WARNING: SEPARATOR must be 1B long string
#ifdef OS_BSD
# define SEPARATOR "/"
#else
# define SEPARATOR "\\"
#endif
extern const struct string separator;

#include "tar.h"
#include "zip.h"
