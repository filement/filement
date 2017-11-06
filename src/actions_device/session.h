int session_login(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *options);
bool session_is_logged_in(struct resources *restrict resources);
bool auth_id_check(struct resources *restrict resources);
