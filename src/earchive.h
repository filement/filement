#define EARCHIVE_MAGIC_STREAM	0x00020000
#define EARCHIVE_MAGIC_BLOCK	0x00020001

#define EARCHIVE_PIPE			0010000
#define EARCHIVE_DIRECTORY		0040000
#define EARCHIVE_REGULAR		0100000
#define EARCHIVE_LINK			0120000
#define EARCHIVE_SOCKET			0140000

struct metadata
{
	uint16_t mode;
	uint16_t _;
	uint8_t user_length, group_length;
	uint16_t path_length;
	uint64_t mtime;
	uint64_t offset;
};

bool metadata_parse(struct metadata *restrict metadata, const char buffer[restrict sizeof(struct metadata)]);
void metadata_format(char buffer[restrict sizeof(struct metadata)], const struct file *restrict file, uint16_t path_length, uint64_t offset);

uint64_t earchive_size_parse(const char buffer[restrict sizeof(uint64_t)]);
