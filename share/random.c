#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

bool readall(int fd, char *restrict buffer, size_t total)
{
    size_t index;
    ssize_t size;
    for(index = 0; index < total; index += size)
    {
        size = read(fd, buffer + index, total - index);
        if (size <= 0) return false;
    }
    return true;
}

char *format_bin(char *restrict buffer, const uint8_t *restrict bin, size_t length)
{
	static const char digit[36] = "0123456789abcdefghijklmnopqrstuvwxyz";
    size_t i;
    for(i = 0; i < length; ++i)
    {
        *buffer++ = digit[(bin[i] & 0xf0) >> 4];
        *buffer++ = digit[bin[i] & 0x0f];
    }
    return buffer;
}

int main(void)
{
	uint8_t value[16], result[33];

	int pool = open("/dev/random", O_RDONLY);
	readall(pool, value, sizeof(value));
	close(pool);

	*format_bin(result, value, 16) = 0;
	printf("%s\n", result);

	return 0;
}
