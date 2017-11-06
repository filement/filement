#define DEVICE_ON "insert into devices_locations(uuid,host,port) values('%s','%s',0)"
#define DEVICE_OFF "delete from devices_locations where uuid='%s' and host='%s'"

struct header
{
	uint8_t uuid[UUID_SIZE];
	uint16_t version_major, version_minor, revision;
	uint16_t cmd;
};

bool device_event(const char *restrict state, const char *uuid_hex, const struct string *restrict address);

union json *devices_locations(const struct vector *restrict hosts);

void *main_device(void *arg);
void *main_device_tls(void *arg);

uint32_t ip4_address(const struct sockaddr *address);

// Checks whether a given device supports a given version.
static inline bool version_support(const struct header *restrict device, uint16_t major, uint16_t minor)
{
	if (device->version_major > major) return true;
	else return ((device->version_major == major) && (device->version_minor >= minor));
}
