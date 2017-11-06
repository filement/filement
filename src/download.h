#define ARCHIVE_EARCHIVE 1
#define ARCHIVE_ZIP 2
#define ARCHIVE_ZIP64 3

struct download
{
	struct stream *stream;
	const struct http_request *request;
	struct http_response *response;
	int archive; // whether to create an archive; < 0 if not determined yet
	struct string prefix;
	char magic[MAGIC_SIZE];
	size_t magic_size;
	#if !defined(OS_WINDOWS)
	off_t offset, total;	// TODO rename this?
	size_t index; // TODO rename this to file_index
	#else
	int64_t offset, total;	
	uint64_t index; // TODO rename this to file_index
	#endif
	struct vector files;
	time_t last_modified;
};

int http_download(const struct string *path, const struct http_request *request, struct http_response *restrict response, struct stream *restrict stream);
int download(const struct vector *restrict paths, struct download *restrict info, unsigned archive);
