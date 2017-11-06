struct buffer {
  uint8_t* p;
  size_t len;
  size_t size;
  off_t begin_offset;
};

struct buffer * buf_alloc();
void buf_free(struct buffer* buf);
int buf_resize(struct buffer *buf, size_t len);
int buf_add_mem(struct buffer *buf, const void *data, size_t len);
void buf_null_terminate(struct buffer *buf);
size_t read_data(void *ptr, size_t size, size_t nmemb, void *data);
size_t null_write(void *buffer, size_t size, size_t nmemb, void *arg);
int curl_set_login(CURL * curl,const union json *session);
void replace_2F(char *data,int length);
