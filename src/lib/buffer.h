struct buffer
{
	char *data;
	size_t length;
	size_t _size;
};

bool buffer_adjust(struct buffer *restrict buffer, size_t size);
