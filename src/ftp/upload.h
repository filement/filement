int http_upload(const struct string *root, const struct string *filename,struct resources *restrict resources , const struct http_request *request, struct stream *restrict stream, const struct string *status_key);
int http_transfer(const struct string *src, const struct string *dest, struct stream *restrict stream, const struct string *status_key);
