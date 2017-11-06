#ifdef OS_BSD
# include <arpa/inet.h>
#else
# include <windows.h>
# include <winsock2.h>
# include <ws2tcpip.h>
#endif

#include "stream.h"
#include "json.h"
#include "cache.h"
#include "server.h"
#include "http.h"
#include "http_parse.h"
#include "http_response.h"
#include "actions_sorted.h"

bool remote_json_send(const struct http_request *request,struct http_response *restrict response, struct resources *restrict resources,struct string *restrict json_serialized);
bool remote_json_chunked_start(struct stream *restrict stream, const struct http_request *request, struct http_response *restrict response);
bool remote_json_chunked_end(struct stream *restrict stream, struct http_response *restrict response);
int remote_json_error(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, int code);

// TODO deprecated
bool remote_json_chunked_init(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources);
bool remote_json_chunked_close(struct http_response *restrict response, struct resources *restrict resources);

union json *fs_get_blocks_json(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query,int *local_errno);
union json *session_actions_json(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *options, int *local_errno);
union json *config_get_interfaces_json(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query, int *local_errno);
union json *config_info_json(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *query, int *local_errno);
