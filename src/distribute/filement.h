#define SECRET_SIZE 16

#define LOCATION_SIZE_MAX (128 + 1 + 5 + 1 + 5) // example.com:80,443

struct connection
{
	struct sockaddr_storage address;
	int socket;
};
