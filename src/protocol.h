#define UUID_SIZE		16					/* size of UUID value */
#define UUID_LENGTH		(UUID_SIZE * 2)		/* length of UUID string representation */

#define SECRET_SIZE_MAX	64

// 24B
#define REQUEST_SIZE (UUID_SIZE + sizeof(uint16_t) * 4)

#define ID_LENGTH_MIN				1
#define ID_LENGTH_MAX				127

#define DEVNAME_LENGTH_MIN			1
#define DEVNAME_LENGTH_MAX			63

#if defined(BELKIN) || defined(DISTRIBUTE)
# define SERIAL_LENGTH_MAX			32
#endif

#define CMD_REGISTER_EMAIL			1
#define CMD_REGISTER_AUTH			2
#define CMD_UNREGISTER				3
// 									4
#define CMD_UPGRADE_LIST			5
#define CMD_UPGRADE_FINISH			6
#define CMD_PROXY_LIST				7
#define CMD_NAME					8
#define CMD_RENAME					9
#define CMD_ADDUSER_OLD				32763
#define CMD_RMUSER_OLD				32764
#define CMD_TOKEN_AUTH_OLD			32765
//#define CMD_PROXY_LIST_OLD			32766

#if !defined(TEST)
# define HOST_DISTRIBUTE			"distribute.filement.com"
# define HOST_DISTRIBUTE_HTTP		"distribute2.filement.com"
# define HOST_DISTRIBUTE_EVENT		"eventserver.filement.com"
# define HOST_DISTRIBUTE_REMOTE     "webserver.filement.com"
# define PORT_DISTRIBUTE_DEVICE		80
# define TEST 0
#else
# define HOST_DISTRIBUTE			"distribute.flmntdev.com"
# define HOST_DISTRIBUTE_HTTP		"distribute.flmntdev.com"
# define HOST_DISTRIBUTE_EVENT		"distribute.flmntdev.com"
# define HOST_DISTRIBUTE_REMOTE     "flmntdev.com"
# define PORT_DISTRIBUTE_DEVICE		143
#endif

#define PORT_DISTRIBUTE_DEVICE_TLS	443
#define PORT_DISTRIBUTE_HTTP		80
#define PORT_DISTRIBUTE_HTTPS		443 /* not used */
#define PORT_DISTRIBUTE_EVENT		110
#define PORT_PROXY_DEVICE			443
#define PORT_PROXY_HTTP				80
#define PORT_PROXY_HTTPS			443

#if defined(DEVICE) | defined(FAILSAFE)
# define PORT_HTTP_MIN				(4080 + TEST)
# define PORT_HTTP_MAX				(4083 + TEST)
//# define PORT_HTTP_MIN                (8200 + TEST)
//# define PORT_HTTP_MAX                (8200 + TEST)
# define PORT_DIFF (PORT_HTTP_MAX - PORT_HTTP_MIN + 1)
#elif defined(DISTRIBUTE)
# define PORT_HTTP_MIN				80
# define PORT_HTTP_MAX				80
# define PORT_DIFF (PORT_HTTP_MAX - PORT_HTTP_MIN + 1)
#elif defined(CLOUD)
# define PORT_HTTP_MIN				(4081 + TEST)
# define PORT_HTTP_MAX				(4081 + TEST)
# define PORT_DIFF (PORT_HTTP_MAX - PORT_HTTP_MIN + 1)
#elif defined(PUBCLOUD)
# define PORT_HTTP_MIN				(4084 + TEST)
# define PORT_HTTP_MAX				(4084 + TEST)
# define PORT_DIFF (PORT_HTTP_MAX - PORT_HTTP_MIN + 1)
#elif defined(FTP)
# define PORT_HTTP_MIN				(4083 + TEST)
# define PORT_HTTP_MAX				(4083 + TEST)
# define PORT_DIFF (PORT_HTTP_MAX - PORT_HTTP_MIN + 1)
#endif

// TODO deprecated
#define write16(buffer, value) (*(uint16_t *)(buffer) = htons(value))
#define write32(buffer, value) (*(uint32_t *)(buffer) = htonl(value))
