#define MAGIC_SIZE 16

#define content_text		0xff000000
#define content_archive		0x00020000
#define content_document	0x00040000
#define content_image		0x00050000
#define content_audio		0x00080000
#define content_video		0x00090000
#define content_multimedia	0x000c0000

uint32_t content(const unsigned char *magic, size_t size);
