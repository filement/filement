#include <zlib.h>

#include "arch.h"

#define htole8(value) (value)

#if !defined(OS_WINDOWS)
# define ZIP_VERSION_CREATED			((3 << 8) | 61)	/* UNIX 6.1 */
#else
# define ZIP_VERSION_CREATED			((3 << 8) | 61)	/* UNIX 6.1 */
#endif

#define ZIP_VERSION_REGULAR				10 /* 1.0 */
#define ZIP_VERSION_DIRECTORY			20 /* 2.0 */
#define ZIP_VERSION_64					45 /* 4.5 */

#define ZIP_LOCALFILE_SIGNATURE				0x504b0304
#define ZIP_DATADESCRIPTOR_SIGNATURE		0x504b0708
#define ZIP_CENTRALFILE_SIGNATURE			0x504b0102
#define ZIP_CENTRALDIR_END_SIGNATURE		0x504b0506

#define ZIP64_CENTRALDIR_END_SIGNATURE		0x504b0606
#define ZIP64_CENTRALDIR_LOCATOR_SIGNATURE	0x504b0607

#define ZIP_CRC32_BASE 0

#define ZIP_WINDOW_BITS 15
#define ZIP_DATA_DESCRIPTOR_SIZE	12

#define zip_read(var, buffer, size) do \
	{ \
		endian_little##size(&var, buffer); \
		buffer += size >> 3; \
	} while (0)
#define zip_write(buffer, value, size) do \
	{ \
		uint##size##_t number = (value); \
		if (number != (value)) number = -1; \
		endian_little##size(buffer, (char *)&number); \
		(buffer) += size >> 3; \
	} while (0)

char *zip_date(char *restrict buffer, time_t timestamp);
time_t unzip_date(const char *restrict buffer);

int zip_crc(void *arg, unsigned char *buffer, unsigned size);

#if !defined(OS_WINDOWS)
int evfs_zip(const struct string *restrict location, unsigned depth, const struct string *restrict buffer, struct buffer *restrict cwd, evfs_callback_t callback, void *argument, unsigned flags);
#else
int evfs_zip(const struct string *restrict location, struct string *restrict buffer, struct buffer *restrict cwd, evfs_callback_t callback, void *argument, unsigned depth, int buffer_fd);
#endif
