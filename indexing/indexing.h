#define HEADER "\x00\x03\x00\00\x00\x00\x00\x00"

struct file
{
	uint16_t mode;
	uint16_t path_length;
	uint32_t content;
	uint64_t mtime;
    uint64_t size;
};
