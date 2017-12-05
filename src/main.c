#if defined(DEVICE)
# include <fcntl.h>
# include <string.h>
# include <termios.h>
#endif
#include <unistd.h>

#include "filement.h"
#include "log.h"
#if defined(DEVICE)
# include "device/startup.h"
#endif

#define STRING(s) (s), sizeof(s)

#define PASSWORD "Device password: "

#define USAGE \
	"Usage: filement                         Start a registered device.\n" \
	"  or: filement [ device_name email ]    Register a device.\n" \
	"  or: filement --reset                  Reset device registration.\n"

#if defined(DEVICE)
// http://en.wikibooks.org/wiki/Serial_Programming/termios
static size_t password(char *restrict buffer, size_t length)
{
	struct termios old, new;

	size_t index = 0, total = 0;
	ssize_t size;

	// TODO check whether 0 and 1 point to the same terminal
	if (!isatty(0)) return 0;

	int flags = fcntl(0, F_GETFL);

	// Get current terminal configuration. Turn echo off. Flush any pending data.
	fcntl(0, F_SETFL, flags & ~O_NONBLOCK);
	tcgetattr(0, &old);
	new = old;
	new.c_lflag &= ~(ECHO | IEXTEN);
	if (tcsetattr(0, TCSAFLUSH, &new) < 0) return 0;

	if (write(1, PASSWORD, sizeof(PASSWORD) - 1) != sizeof(PASSWORD) - 1) goto finally; // output error

	// Read password from standard input into buffer.
	while (true)
	{
		size = read(0, buffer + total, length - total);
		if (!size) break;
		else if (size < 0)
		{
			index = 0;
			break; // input error
		}

		total += size;

		// Find line end.
		do if (buffer[index] == '\n') goto finally;
		while (++index < total);

		if (total == length)
		{
			index = 0;
			break; // input too long
		}
	}

finally:

	if (write(1, "\n", 1) != 1) index = 0; // output error

	// Restore old terminal configuration.
	tcsetattr(0, TCSAFLUSH, &old);
	fcntl(0, F_SETFL, flags);

	return index;
}
#endif

int main(int argc, char *argv[])
{
	bool registered = filement_init();

	startup_add = &startup_cmd_add;
	startup_remove = &startup_cmd_remove;

#if defined(DEVICE)
	switch (argc)
	{
	case 1:
		if (!registered)
		{
			error(logs("Device not registered."));
			write(2, USAGE, sizeof(USAGE) - 1);
			return -2;
		}
		break;

	case 3:
		{
			char buffer[64];
			size_t length = password(buffer, sizeof(buffer));
			if (!length)
			{
				error(logs("Invalid password."));
				return -3;
			}

			struct string devname = string(argv[1], strlen(argv[1]));
			struct string email = string(argv[2], strlen(argv[2]));
			struct string password = string(buffer, length);
			if (!filement_register(&email, &devname, &password))
			{
				error(logs("Registration error."));
				write(2, USAGE, sizeof(USAGE) - 1);
				return -4;
			}

			startup_add(&startup_filement);
		}
		break;

	case 2:
		if (!memcmp(argv[1], STRING("--reset")))
		{
			if (registered)
			{
				startup_remove(&startup_filement);
				filement_reset(); // terminates the program
			}
			return 0;
		}
		else if (!memcmp(argv[1], STRING("--version")))
		{
			// TODO print version information
			return 0;
		}
	default:
		write(2, USAGE, sizeof(USAGE) - 1);
		return -1;
	}
#endif

#if defined(FILEMENT_UPGRADE) && !defined(OS_MAC) /* upgrade on mac is not supported with this executable */
	// Check for new version of the Filement device software.
	/*if (!filement_upgrade("filement"))
		error(logs("Upgrade failed"));*/
#endif

	filement_daemon();

	filement_serve();

	return 0;
}
